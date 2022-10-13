#!/bin/bash

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

for branch in next hack
do
	echo "sj.korg"
	git push sj.korg "damon/$branch" --force

	echo "gh.public"
	git push gh.public "damon/$branch" --force

	echo "gh.damon"
	git push gh.damon "damon/$branch:$branch" --force
done
