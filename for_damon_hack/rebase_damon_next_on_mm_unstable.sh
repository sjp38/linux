#!/bin/bash

if [ $# -ne 1 ] && [ $# -ne 2 ]
then
	echo "Usage: $0 <mainline base> [old mm-unstable]"
	exit 1
fi

mainline_base=$1

if [ $# -eq 2 ]
then
	old_mm_unstable=$2
else
	guess=$(git rev-parse akpm.korg.mm/mm-unstable)
	if git log damon/next --pretty=%H | grep "$guess" --max-count 1
	then
		old_mm_unstable=$guess
	else
		echo "Can't find the old mm-unstable.  Pass it explicitly"
		exit 1
	fi
fi

new_mm_unstable=akpm.korg.mm/mm-unstable

bindir=$(dirname "$0")

"$bindir/ensure_gpg_password.sh"
git remote update

old_mm_unstable_commit=$(git rev-parse "$old_mm_unstable")
new_mm_unstable_commit=$(git rev-parse "$new_mm_unstable")
if [ "$old_mm_unstable" = "$new_mm_unstable_commit" ]
then
	echo "No update on mm-unstable"
	exit 0
fi

git branch -M damon/next.old damon/next.old2
git branch -m damon/next damon/next.old
cp "$bindir/unmerged_commits.sh" ./
git checkout akpm.korg.mm/mm-unstable -b damon/next
commits_to_pick=$(./unmerged_commits.sh "$old_mm_unstable..damon/next.old" \
	"$mainline_base..$new_mm_unstable")
git cherry-pick --allow-empty $commits_to_pick
