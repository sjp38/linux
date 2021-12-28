// SPDX-License-Identifier: GPL-2.0
/*
 * Author: SeongJae Park <sj@kernel.org>
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define DBGFS_TARGET_IDS "/sys/kernel/debug/damon/target_ids"

static void write_targetid_exit(void)
{
	int target_ids_fd = open(DBGFS_TARGET_IDS, O_RDWR);
	char pid_str[128];

	snprintf(pid_str, sizeof(pid_str), "%d", getpid());
	write(target_ids_fd, pid_str, sizeof(pid_str));
	close(target_ids_fd);
	exit(0);
}

int main(int argc, char *argv[])
{
	while (true) {
		int pid = fork();
		pid_t child_pid;

		if (pid < 0) {
			fprintf(stderr, "fork() failed\n");
			exit(1);
		}
		if (pid == 0)
			write_targetid_exit();
		child_pid = wait(NULL);
	}
	return 0;
}
