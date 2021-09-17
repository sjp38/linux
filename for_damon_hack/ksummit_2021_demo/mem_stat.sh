#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <pid>"
	exit 1
fi
pid=$1

while [ -f /proc/$pid/status ]
do
	grep VmRSS /proc/$pid/status
	dmesg -c | grep ksdemo | tail -n 1
	sleep 1
done
