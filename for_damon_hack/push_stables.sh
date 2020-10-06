#!/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <base series> <base version>"
	echo "    e.g., $0 \"5.4.y\" \"5.4.108\""
	exit 1
fi

series=$1
version=$2

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

echo "sj.korg"
git push sj.korg --force damon/for-v$series \
	damon/for-v$series:damon/for-v$version

echo "gh.public"
git push gh.public --force damon/for-v$series \
	damon/for-v$series:damon/for-v$version

echo "gh.damon"
git push gh.damon --force damon/for-v$series:for-v$series \
	damon/for-v$series:for-v$version
