#!/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <base series> <base version>"
	echo "    e.g., $0 \"5.4.y\" \"5.4.108\""
	exit 1
fi

series=$1
version=$2

# sj.korg git@gitolite.kernel.org:pub/scm/linux/kernel/git/sj/linux.git
# gh.public       https://github.com/sjp38/linux
# gh.damon        https://github.com/damonitor/linux

echo "ensure gpg password"
gpg-connect-agent updatestartuptty /bye > /dev/null
ssh gitolite.kernel.org help > /dev/null

echo "sj.korg"
git push sj.korg damon/for-v$series --force

echo "gh.public"
git push gh.public --force damon/for-v$series \
	damon/for-v$series:damon/for-v$version

echo "gh.damon"
git push gh.damon --force damon/for-v$series:for-v$series \
	damon/for-v$series:for-v$version
