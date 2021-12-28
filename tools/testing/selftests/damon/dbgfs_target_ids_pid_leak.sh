#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

ksft_skip=4

if ! which slabtop > /dev/null
then
	echo "slabtop not found"
	exit $ksft_skip
fi

before=$(script -q -c "stty rows 999; sudo slabtop -o" /dev/null | grep pid | \
	awk '{print $2}')

timeout 1 ./dbgfs_target_ids_pid_leak

after=$(script -q -c "stty rows 999; sudo slabtop -o" /dev/null | grep pid | \
	awk '{print $2}')

echo "number of active pid slabs: $before -> $after"
if [ $after -gt $before ]
then
	echo "maybe pids are leaking"
	exit 1
else
	exit 0
fi
