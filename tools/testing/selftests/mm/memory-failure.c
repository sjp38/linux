// SPDX-License-Identifier: GPL-2.0
/*
 * Memory-failure functional tests.
 *
 * Author(s): Miaohe Lin <linmiaohe@huawei.com>
 */

#include "../kselftest_harness.h"

#include <sys/mman.h>
#include <linux/mman.h>
#include <linux/string.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>
#include <fcntl.h>
#include <sys/vfs.h>
#include <linux/magic.h>
#include <errno.h>
#include <sys/wait.h>
#include <stdlib.h>

#include "vm_util.h"

#define PANIC_SYSCTL "/proc/sys/vm/panic_on_unrecoverable_memory_failure"

enum inject_type {
	MADV_HARD,
	MADV_SOFT,
};

enum result_type {
	MADV_HARD_ANON,
	MADV_HARD_CLEAN_PAGECACHE,
	MADV_HARD_DIRTY_PAGECACHE,
	MADV_SOFT_ANON,
	MADV_SOFT_CLEAN_PAGECACHE,
	MADV_SOFT_DIRTY_PAGECACHE,
};

static jmp_buf signal_jmp_buf;
static siginfo_t siginfo;
const char *pagemap_proc = "/proc/self/pagemap";
const char *kpageflags_proc = "/proc/kpageflags";

FIXTURE(memory_failure)
{
	unsigned long page_size;
	unsigned long corrupted_size;
	unsigned long pfn;
	int pagemap_fd;
	int kpageflags_fd;
	bool triggered;
};

FIXTURE_VARIANT(memory_failure)
{
	enum inject_type type;
	int (*inject)(FIXTURE_DATA(memory_failure) * self, void *vaddr);
};

static int madv_hard_inject(FIXTURE_DATA(memory_failure) * self, void *vaddr)
{
	return madvise(vaddr, self->page_size, MADV_HWPOISON);
}

FIXTURE_VARIANT_ADD(memory_failure, madv_hard)
{
	.type = MADV_HARD,
	.inject = madv_hard_inject,
};

static int madv_soft_inject(FIXTURE_DATA(memory_failure) * self, void *vaddr)
{
	return madvise(vaddr, self->page_size, MADV_SOFT_OFFLINE);
}

FIXTURE_VARIANT_ADD(memory_failure, madv_soft)
{
	.type = MADV_SOFT,
	.inject = madv_soft_inject,
};

static void sigbus_action(int signo, siginfo_t *si, void *args)
{
	memcpy(&siginfo, si, sizeof(siginfo_t));
	siglongjmp(signal_jmp_buf, 1);
}

static int setup_sighandler(void)
{
	struct sigaction sa = {
		.sa_sigaction = sigbus_action,
		.sa_flags = SA_SIGINFO,
	};

	return sigaction(SIGBUS, &sa, NULL);
}

FIXTURE_SETUP(memory_failure)
{
	memset(self, 0, sizeof(*self));

	self->page_size = (unsigned long)sysconf(_SC_PAGESIZE);

	memset(&siginfo, 0, sizeof(siginfo));
	if (setup_sighandler())
		SKIP(return, "setup sighandler failed.\n");

	self->pagemap_fd = open(pagemap_proc, O_RDONLY);
	if (self->pagemap_fd == -1)
		SKIP(return, "open %s failed.\n", pagemap_proc);

	self->kpageflags_fd = open(kpageflags_proc, O_RDONLY);
	if (self->kpageflags_fd == -1)
		SKIP(return, "open %s failed.\n", kpageflags_proc);
}

static void teardown_sighandler(void)
{
	struct sigaction sa = {
		.sa_handler = SIG_DFL,
		.sa_flags = SA_SIGINFO,
	};

	sigaction(SIGBUS, &sa, NULL);
}

FIXTURE_TEARDOWN(memory_failure)
{
	close(self->kpageflags_fd);
	close(self->pagemap_fd);
	teardown_sighandler();
}

static void prepare(struct __test_metadata *_metadata, FIXTURE_DATA(memory_failure) * self,
		    void *vaddr)
{
	self->pfn = pagemap_get_pfn(self->pagemap_fd, vaddr);
	ASSERT_NE(self->pfn, -1UL);

	ASSERT_EQ(get_hardware_corrupted_size(&self->corrupted_size), 0);
}

static bool check_memory(void *vaddr, unsigned long size)
{
	char buf[64];

	memset(buf, 0xce, sizeof(buf));
	while (size >= sizeof(buf)) {
		if (memcmp(vaddr, buf, sizeof(buf)))
			return false;
		size -= sizeof(buf);
		vaddr += sizeof(buf);
	}

	return true;
}

static void check(struct __test_metadata *_metadata, FIXTURE_DATA(memory_failure) * self,
		  void *vaddr, enum result_type type, int setjmp)
{
	unsigned long size;
	uint64_t pfn_flags;

	switch (type) {
	case MADV_SOFT_ANON:
	case MADV_HARD_CLEAN_PAGECACHE:
	case MADV_SOFT_CLEAN_PAGECACHE:
	case MADV_SOFT_DIRTY_PAGECACHE:
		/* It is not expected to receive a SIGBUS signal. */
		ASSERT_EQ(setjmp, 0);

		/* The page content should remain unchanged. */
		ASSERT_TRUE(check_memory(vaddr, self->page_size));

		/* The backing pfn of addr should have changed. */
		ASSERT_NE(pagemap_get_pfn(self->pagemap_fd, vaddr), self->pfn);
		break;
	case MADV_HARD_ANON:
	case MADV_HARD_DIRTY_PAGECACHE:
		/* The SIGBUS signal should have been received. */
		ASSERT_EQ(setjmp, 1);

		/* Check if siginfo contains correct SIGBUS context. */
		ASSERT_EQ(siginfo.si_signo, SIGBUS);
		ASSERT_EQ(siginfo.si_code, BUS_MCEERR_AR);
		ASSERT_EQ(1UL << siginfo.si_addr_lsb, self->page_size);
		ASSERT_EQ(siginfo.si_addr, vaddr);

		/* XXX Check backing pte is hwpoison entry when supported. */
		ASSERT_TRUE(pagemap_is_swapped(self->pagemap_fd, vaddr));
		break;
	default:
		SKIP(return, "unexpected inject type %d.\n", type);
	}

	/* Check if the value of HardwareCorrupted has increased. */
	ASSERT_EQ(get_hardware_corrupted_size(&size), 0);
	ASSERT_EQ(size, self->corrupted_size + self->page_size / 1024);

	/* Check if HWPoison flag is set. */
	ASSERT_EQ(pageflags_get(self->pfn, self->kpageflags_fd, &pfn_flags), 0);
	ASSERT_EQ(pfn_flags & KPF_HWPOISON, KPF_HWPOISON);
}

static void cleanup(struct __test_metadata *_metadata, FIXTURE_DATA(memory_failure) * self,
		    void *vaddr)
{
	unsigned long size;
	uint64_t pfn_flags;

	ASSERT_EQ(unpoison_memory(self->pfn), 0);

	/* Check if HWPoison flag is cleared. */
	ASSERT_EQ(pageflags_get(self->pfn, self->kpageflags_fd, &pfn_flags), 0);
	ASSERT_NE(pfn_flags & KPF_HWPOISON, KPF_HWPOISON);

	/* Check if the value of HardwareCorrupted has decreased. */
	ASSERT_EQ(get_hardware_corrupted_size(&size), 0);
	ASSERT_EQ(size, self->corrupted_size);
}

TEST_F(memory_failure, anon)
{
	char *addr;
	int ret;

	addr = mmap(0, self->page_size, PROT_READ | PROT_WRITE,
		    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (addr == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");
	memset(addr, 0xce, self->page_size);

	prepare(_metadata, self, addr);

	ret = sigsetjmp(signal_jmp_buf, 1);
	if (!self->triggered) {
		self->triggered = true;
		ASSERT_EQ(variant->inject(self, addr), 0);
		FORCE_READ(*addr);
	}

	if (variant->type == MADV_HARD)
		check(_metadata, self, addr, MADV_HARD_ANON, ret);
	else
		check(_metadata, self, addr, MADV_SOFT_ANON, ret);

	cleanup(_metadata, self, addr);

	ASSERT_EQ(munmap(addr, self->page_size), 0);
}

static int prepare_file(const char *fname, unsigned long size)
{
	int fd;

	fd = open(fname, O_RDWR | O_CREAT, 0664);
	if (fd >= 0) {
		unlink(fname);
		ftruncate(fd, size);
	}
	return fd;
}

/* Borrowed from mm/gup_longterm.c. */
static int get_fs_type(int fd)
{
	struct statfs fs;
	int ret;

	do {
		ret = fstatfs(fd, &fs);
	} while (ret && errno == EINTR);

	return ret ? 0 : (int)fs.f_type;
}

TEST_F(memory_failure, clean_pagecache)
{
	int fd;
	char *addr;
	int ret;
	int fs_type;

	fd = prepare_file("./clean-page-cache-test-file", self->page_size);
	if (fd < 0)
		SKIP(return, "failed to open test file.\n");
	fs_type = get_fs_type(fd);
	if (!fs_type || fs_type == TMPFS_MAGIC)
		SKIP(return, "unsupported filesystem :%x\n", fs_type);

	addr = mmap(0, self->page_size, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");
	memset(addr, 0xce, self->page_size);
	fsync(fd);

	prepare(_metadata, self, addr);

	ret = sigsetjmp(signal_jmp_buf, 1);
	if (!self->triggered) {
		self->triggered = true;
		ASSERT_EQ(variant->inject(self, addr), 0);
		FORCE_READ(*addr);
	}

	if (variant->type == MADV_HARD)
		check(_metadata, self, addr, MADV_HARD_CLEAN_PAGECACHE, ret);
	else
		check(_metadata, self, addr, MADV_SOFT_CLEAN_PAGECACHE, ret);

	cleanup(_metadata, self, addr);

	ASSERT_EQ(munmap(addr, self->page_size), 0);

	ASSERT_EQ(close(fd), 0);
}

TEST_F(memory_failure, dirty_pagecache)
{
	int fd;
	char *addr;
	int ret;
	int fs_type;

	fd = prepare_file("./dirty-page-cache-test-file", self->page_size);
	if (fd < 0)
		SKIP(return, "failed to open test file.\n");
	fs_type = get_fs_type(fd);
	if (!fs_type || fs_type == TMPFS_MAGIC)
		SKIP(return, "unsupported filesystem :%x\n", fs_type);

	addr = mmap(0, self->page_size, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, 0);
	if (addr == MAP_FAILED)
		SKIP(return, "mmap failed, not enough memory.\n");
	memset(addr, 0xce, self->page_size);

	prepare(_metadata, self, addr);

	ret = sigsetjmp(signal_jmp_buf, 1);
	if (!self->triggered) {
		self->triggered = true;
		ASSERT_EQ(variant->inject(self, addr), 0);
		FORCE_READ(*addr);
	}

	if (variant->type == MADV_HARD)
		check(_metadata, self, addr, MADV_HARD_DIRTY_PAGECACHE, ret);
	else
		check(_metadata, self, addr, MADV_SOFT_DIRTY_PAGECACHE, ret);

	cleanup(_metadata, self, addr);

	ASSERT_EQ(munmap(addr, self->page_size), 0);

	ASSERT_EQ(close(fd), 0);
}

static int read_sysctl_int(const char *path, int *out)
{
	char buf[16];
	int fd, n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return -1;
	buf[n] = '\0';
	*out = atoi(buf);
	return 0;
}

static int write_sysctl_int(const char *path, int val)
{
	char buf[16];
	int fd, len, ret = 0;

	fd = open(path, O_WRONLY);
	if (fd < 0)
		return -1;
	len = snprintf(buf, sizeof(buf), "%d\n", val);
	if (write(fd, buf, len) != len)
		ret = -1;
	close(fd);
	return ret;
}

/*
 * Regression test for vm.panic_on_unrecoverable_memory_failure.
 *
 * With the sysctl on, hwpoison injection on a userspace anonymous page
 * must still be recovered via SIGBUS — it must not trigger a kernel
 * panic. This guards the panic_on_unrecoverable_mf() recheck that rules
 * out concurrent buddy allocations being misclassified as unrecoverable
 * kernel pages (MF_MSG_KERNEL_HIGH_ORDER).
 *
 * If the kernel regresses and panics, the host VM dies and the test
 * harness will report the binary as never having returned — which is
 * itself a clear failure signal.
 */
TEST(panic_on_unrecoverable_user_page)
{
	unsigned long page_size;
	int saved, status;
	void *addr;
	pid_t pid;

	if (read_sysctl_int(PANIC_SYSCTL, &saved))
		SKIP(return, "%s not available\n", PANIC_SYSCTL);
	if (write_sysctl_int(PANIC_SYSCTL, 1))
		SKIP(return, "cannot enable %s (need root?)\n", PANIC_SYSCTL);

	page_size = sysconf(_SC_PAGESIZE);

	pid = fork();
	ASSERT_NE(pid, -1);
	if (pid == 0) {
		addr = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
			    MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (addr == MAP_FAILED)
			_exit(1);
		*(volatile char *)addr = 1;
		if (madvise(addr, page_size, MADV_HWPOISON))
			_exit(2);
		FORCE_READ(*(volatile char *)addr);
		_exit(0); /* unreachable: SIGBUS expected */
	}

	ASSERT_EQ(waitpid(pid, &status, 0), pid);
	write_sysctl_int(PANIC_SYSCTL, saved);

	ASSERT_TRUE(WIFSIGNALED(status));
	ASSERT_EQ(WTERMSIG(status), SIGBUS);
}

TEST_HARNESS_MAIN
