#!/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <base series> <base version>"
	echo "    e.g., $0 \"5.4.y\" \"5.4.108\""
	exit 1
fi

series=$1
version=$2

# gh.public       https://github.com/sjp38/linux
# gh.damon        https://github.com/damonitor/linux

git push gh.public --force damon/for-v$series \
	damon/for-v$series:damon/for-v$version

git push gh.damon --force damon/for-v$series:for-v$series \
	damon/for-v$series:for-v$version
