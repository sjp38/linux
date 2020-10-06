#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <on|off>"
	exit 1
fi

onoff=$1

cmd="file mm/damon/*"
if [ "$onoff" = "on" ]
then
	cmd+=" +p"
else
	cmd=" -p"
fi

echo -n "$cmd" | sudo tee /sys/kernel/debug/dynamic_debug/control
echo ""
