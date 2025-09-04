// SPDX-License-Identifier: GPL-2.0
#define _GNU_SOURCE
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/syscall.h>

static void usage(void)
{
	printf("madvise TARGET ADVICE START END\n\n");
	printf("Arguments:\n");
	printf("\t<TARGET>\n");
	printf("\t\tA process ID or a file to give the advice to.\n\n");
	printf("\t\tUse \"./\" prefix if the file name is all digits.\n\n");
	printf("\t<ADVICE>\n");
	printf("\t\tcold\t\t- Deactivate a given range of pages\n");
	printf("\t\tcollapse\t- Collapse pages in a given range into THPs\n");
	printf("\t\tpageout\t\t- Reclaim a given range of pages\n");
	printf("\t\twillneed\t- The specified data will be accessed in the near future\n");
	printf("\n\t\tSee madvise(2) for more details.\n\n");
	printf("\t<START>/<END>\n");
	printf("\t\tStart and end addressed for the advice. Must be page-aligned.\n\n");
	printf("\t\tFor PID case, it is addresses in the target process address space.\n\n");
	printf("\t\tFor file case, it is offsets in the file.\n\n");
}

static void error(const char *fmt, ...)
{
	if (fmt) {
		va_list argp;

		va_start(argp, fmt);
		vfprintf(stderr, fmt, argp);
		va_end(argp);
		printf("\n");
	}

	usage();
	exit(-1);
}

#define PMD_SIZE_FILE_PATH "/sys/kernel/mm/transparent_hugepage/hpage_pmd_size"
static unsigned long read_pmd_pagesize(void)
{
	int fd;
	char buf[20];
	ssize_t num_read;

	fd = open(PMD_SIZE_FILE_PATH, O_RDONLY);
	if (fd == -1)
		return 0;

	num_read = read(fd, buf, 19);
	if (num_read < 1) {
		close(fd);
		return 0;
	}
	buf[num_read] = '\0';
	close(fd);

	return strtoul(buf, NULL, 10);
}

static int pidfd_open(pid_t pid, unsigned int flags)
{
	return syscall(SYS_pidfd_open, pid, flags);
}

int main(int argc, const char *argv[])
{
	unsigned long pid, start, end, page_size;
	int advice;
	char *err;
	int fd;

	if (argc != 5)
		error(NULL);

	pid = strtoul(argv[1], &err, 10);
	if (*err || err == argv[1] ||
	    pid > INT_MAX || (pid_t)pid <= 0) {
		// Not a PID, assume argv[1] is a file name
		pid = 0;
	}

	if (pid) {
		fd = pidfd_open(pid, 0);
		if (fd < 0)
			perror("pidfd_open()"), exit(-1);
	} else {
		fd = open(argv[1], O_RDWR);
		if (fd < 0)
			perror("open"), exit(-1);
	}

	if (!strcmp(argv[2], "cold"))
		advice = MADV_COLD;
	else if (!strcmp(argv[2], "collapse"))
		advice = MADV_COLLAPSE;
	else if (!strcmp(argv[2], "pageout"))
		advice = MADV_PAGEOUT;
	else if (!strcmp(argv[2], "willneed"))
		advice = MADV_WILLNEED;
	else
		error("Unknown advice: %s\n", argv[2]);

	page_size = sysconf(_SC_PAGE_SIZE);

	start = strtoul(argv[3], &err, 0);
	if (*err || err == argv[3])
		error("Cannot parse start address\n");
	if (start % page_size)
		error("Start address is not aligned to page size\n");
	end = strtoul(argv[4], &err, 0);
	if (*err || err == argv[4])
		error("Cannot parse end address\n");
	if (end % page_size)
		error("End address is not aligned to page size\n");

	if (pid) {
		struct iovec vec = {
			.iov_base = (void *)start,
			.iov_len = end - start,
		};
		ssize_t ret;

		ret = process_madvise(fd, &vec, 1, advice, 0);
		if (ret < 0)
			perror("process_madvise"), exit(-1);

		if ((unsigned long)ret != end - start)
			printf("Partial advice occurred. Stopped at %#lx\n", start + ret);
	} else {
		unsigned long addr, hpage_pmd_size;
		void *p;
		int ret;

		hpage_pmd_size = read_pmd_pagesize();
		if (!hpage_pmd_size) {
			printf("Reading PMD pagesize failed");
			exit(-1);
		}

		// Allocate virtual address space to align the target mmap to PMD size
		// Some advices require this.
		p = mmap(NULL, end - start + hpage_pmd_size, PROT_NONE,
			 MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED)
			perror("mmap0"), exit(-1);
		addr = (unsigned long)p;
		addr += hpage_pmd_size - 1;
		addr &= ~(hpage_pmd_size - 1);

		p = mmap((void *)addr, end - start,
			 PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED | MAP_POPULATE, fd, start);
		if (p == MAP_FAILED)
			perror("mmap"), exit(-1);

		ret = madvise(p, end - start, advice);
		if (ret)
			perror("madvise"), exit(-1);
	}

	return 0;
}
