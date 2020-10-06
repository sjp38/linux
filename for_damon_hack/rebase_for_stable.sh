#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <series>"
	echo "    e.g., $0 \"5.4.y\""
	exit 1
fi

series=$1

git checkout damon/for-v$series
git branch -D damon/for-v$series.old
git branch damon/for-v$series.old
git rebase stable/linux-$series
