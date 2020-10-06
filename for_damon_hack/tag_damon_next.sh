#!/bin/bash

if [ $# -lt 1 ] || [ $# -gt 2 ]
then
	echo "Usage: $0 <baseline> [--push]"
	exit 1
fi

baseline=$1

if [ $# -eq 2 ] && [ "$2" = "--push" ]
then
	push="true"
else
	push="false"
fi

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

datetime=$(date +"%Y-%m-%d-%H-%M")
tagname=damon/next-$datetime-on-$baseline
echo "tagging as $tagname"
git tag -as "$tagname" -m "A snapshot of damon/next"

if [ ! "$push" = "true" ]
then
	exit 0
fi

echo "push"
"$bindir/push_tag.sh" "$baseline"
"$bindir/push_tag.sh" "$tagname"
