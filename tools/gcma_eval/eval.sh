#!/bin/bash

file="2GiB_file"

if ! [ -f "$file" ]
then
	echo "$file not found. create it."
	dd if=/dev/zero of=$file bs=1M count=2048
fi

ITER=10;
TERM=1000;

cmd="./stress 600 &";
echo $cmd;
./stress 600 &
cmd = "sleep 5";
sleep 5;
for i in 10 50 100 500 1000 2000 3000 4000 5000 6000 7000 8000 9000 10000
do
	cmd="echo \"$i $ITER $TERM\" > /sys/kernel/debug/gcma_eval/eval";
	echo $cmd;
	echo "$i $ITER $TERM" > /sys/kernel/debug/gcma_eval/eval;
done
