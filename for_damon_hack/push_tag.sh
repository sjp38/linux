#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <tag>"
	exit 1
fi

tag=$1

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

echo "sj.korg"
git push sj.korg "$tag"

echo "gh.public"
git push gh.public "$tag"

echo "gh.damon"
git push gh.damon "$tag"
