#!/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <mainline base> <old mm-unstable>"
	exit 1
fi

mainline_base=$1
old_mm_unstable=$2

new_mm_unstable=akpm.korg.mm/mm-unstable

bindir=$(dirname "$0")

"$bindir/ensure_gpg_password.sh"
git remote update
git branch -M damon/next.old damon/next.old2
git branch -m damon/next damon/next.old
cp "$bindir/unmerged_commits.sh" ./
git checkout akpm.korg.mm/mm-unstable -b damon/next
commits_to_pick=$(./unmerged_commits.sh "$old_mm_unstable..damon/next.old" \
	"$mainline_base..$new_mm_unstable")
git cherry-pick --allow-empty $commits_to_pick
