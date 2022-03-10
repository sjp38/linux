#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ksft_skip=4

before=$(grep "^pid " /proc/slabinfo | awk '{print $2}')

timeout 1 ./dbgfs_target_ids_pid_leak

after=$(grep "^pid " /proc/slabinfo | awk '{print $2}')

echo "number of active pid slabs: $before -> $after"
if [ $after -gt $before ]
then
	echo "maybe pids are leaking"
	exit 1
else
	exit 0
fi
