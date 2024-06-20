// SPDX-License-Identifier: GPL-2.0
/*
 * Test soft offline behavior for HugeTLB pages:
 * - if enable_soft_offline = 0, hugepages should stay intact and soft
 *   offlining failed with EINVAL.
 * - if enable_soft_offline = 1, a hugepage should be dissolved and
 *   nr_hugepages/free_hugepages should be reduced by 1.
 *
 * Before running, make sure more than 2 hugepages of default_hugepagesz
 * are allocated. For example, if /proc/meminfo/Hugepagesize is 2048kB:
 *   echo 8 > /sys/kernel/mm/hugepages/hugepages-2048kB/nr_hugepages
 */

#define _GNU_SOURCE
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <linux/magic.h>
#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/statfs.h>
#include <sys/types.h>

#ifndef MADV_SOFT_OFFLINE
#define MADV_SOFT_OFFLINE 101
#endif

#define PREFIX " ... "
#define EPREFIX " !!! "

enum test_status {
	TEST_PASS = 0,
	TEST_FAILED = 1,
	// From ${ksft_skip} in run_vmtests.sh.
	TEST_SKIPPED = 4,
};

static enum test_status do_soft_offline(int fd, size_t len, int expect_ret)
{
	char *filemap = NULL;
	char *hwp_addr = NULL;
	const unsigned long pagesize = getpagesize();
	int ret = 0;
	enum test_status status = TEST_SKIPPED;

	if (ftruncate(fd, len) < 0) {
		perror(EPREFIX "ftruncate to len failed");
		return status;
	}

	filemap = mmap(NULL, len, PROT_READ | PROT_WRITE,
		       MAP_SHARED | MAP_POPULATE, fd, 0);
	if (filemap == MAP_FAILED) {
		perror(EPREFIX "mmap failed");
		goto untruncate;
	}

	memset(filemap, 0xab, len);
	printf(PREFIX "Allocated %#lx bytes of hugetlb pages\n", len);

	hwp_addr = filemap + len / 2;
	ret = madvise(hwp_addr, pagesize, MADV_SOFT_OFFLINE);
	printf(PREFIX "MADV_SOFT_OFFLINE %p ret=%d, errno=%d\n",
	       hwp_addr, ret, errno);
	if (ret != 0)
		perror(EPREFIX "madvise failed");

	if (errno == expect_ret)
		status = TEST_PASS;
	else {
		printf(EPREFIX "MADV_SOFT_OFFLINE should ret %d\n", expect_ret);
		status = TEST_FAILED;
	}

	munmap(filemap, len);
untruncate:
	if (ftruncate(fd, 0) < 0)
		perror(EPREFIX "ftruncate back to 0 failed");

	return status;
}

static int set_enable_soft_offline(int value)
{
	char cmd[256] = {0};
	FILE *cmdfile = NULL;

	if (value != 0 && value != 1)
		return -EINVAL;

	sprintf(cmd, "echo %d > /proc/sys/vm/enable_soft_offline", value);
	cmdfile = popen(cmd, "r");

	if (cmdfile)
		printf(PREFIX "enable_soft_offline => %d\n", value);
	else {
		perror(EPREFIX "failed to set enable_soft_offline");
		return errno;
	}

	pclose(cmdfile);
	return 0;
}

static int read_nr_hugepages(unsigned long hugepage_size,
			     unsigned long *nr_hugepages)
{
	char buffer[256] = {0};
	char cmd[256] = {0};

	sprintf(cmd, "cat /sys/kernel/mm/hugepages/hugepages-%ldkB/nr_hugepages",
		hugepage_size);
	FILE *cmdfile = popen(cmd, "r");

	if (cmdfile == NULL) {
		perror(EPREFIX "failed to popen nr_hugepages");
		return -1;
	}

	if (!fgets(buffer, sizeof(buffer), cmdfile)) {
		perror(EPREFIX "failed to read nr_hugepages");
		pclose(cmdfile);
		return -1;
	}

	*nr_hugepages = atoll(buffer);
	pclose(cmdfile);
	return 0;
}

static int create_hugetlbfs_file(struct statfs *file_stat)
{
	int fd;

	fd = memfd_create("hugetlb_tmp", MFD_HUGETLB);
	if (fd < 0) {
		perror(EPREFIX "could not open hugetlbfs file");
		return -1;
	}

	memset(file_stat, 0, sizeof(*file_stat));
	if (fstatfs(fd, file_stat)) {
		perror(EPREFIX "fstatfs failed");
		goto close;
	}
	if (file_stat->f_type != HUGETLBFS_MAGIC) {
		printf(EPREFIX "not hugetlbfs file\n");
		goto close;
	}

	return fd;
close:
	close(fd);
	return -1;
}

static enum test_status test_soft_offline_common(int enable_soft_offline)
{
	int fd;
	int expect_ret = enable_soft_offline ? 0 : EOPNOTSUPP;
	struct statfs file_stat;
	unsigned long hugepagesize_kb = 0;
	unsigned long nr_hugepages_before = 0;
	unsigned long nr_hugepages_after = 0;
	enum test_status status = TEST_SKIPPED;

	printf("Test soft-offline when enabled_soft_offline=%d\n",
		enable_soft_offline);

	fd = create_hugetlbfs_file(&file_stat);
	if (fd < 0) {
		printf(EPREFIX "Failed to create hugetlbfs file\n");
		return status;
	}

	hugepagesize_kb = file_stat.f_bsize / 1024;
	printf(PREFIX "Hugepagesize is %ldkB\n", hugepagesize_kb);

	if (set_enable_soft_offline(enable_soft_offline))
		return TEST_FAILED;

	if (read_nr_hugepages(hugepagesize_kb, &nr_hugepages_before) != 0)
		return TEST_FAILED;

	printf(PREFIX "Before MADV_SOFT_OFFLINE nr_hugepages=%ld\n",
		nr_hugepages_before);

	status = do_soft_offline(fd, 2 * file_stat.f_bsize, expect_ret);

	if (read_nr_hugepages(hugepagesize_kb, &nr_hugepages_after) != 0)
		return TEST_FAILED;

	printf(PREFIX "After MADV_SOFT_OFFLINE nr_hugepages=%ld\n",
		nr_hugepages_after);

	if (enable_soft_offline) {
		if (nr_hugepages_before != nr_hugepages_after + 1) {
			printf(EPREFIX "MADV_SOFT_OFFLINE should reduced 1 hugepage\n");
			return TEST_FAILED;
		}
	} else {
		if (nr_hugepages_before != nr_hugepages_after) {
			printf(EPREFIX "MADV_SOFT_OFFLINE reduced %lu hugepages\n",
				nr_hugepages_before - nr_hugepages_after);
			return TEST_FAILED;
		}
	}

	return status;
}

int main(void)
{
	enum test_status status;

	status = test_soft_offline_common(1);
	if (status != TEST_PASS)
		return status;

	status = test_soft_offline_common(0);
	if (status != TEST_PASS)
		return status;

	printf("Soft-offline tests all good!\n");
	return TEST_PASS;
}
