#!/bin/bash

if [ $# -ne 2 ]
then
	echo "Usage: $0 <dev commits> <maintainer tree commits>"
	exit 1
fi

dev_commits_range=$1
maintainer_commits_range=$2

dev_commits=$(git log "$dev_commits_range" --pretty=%H --reverse)
maintainer_commits=$(git log "$maintainer_commits_range" --pretty=%H --reverse)
maintainer_subjects=$(git log "$maintainer_commits_range" --pretty=%s --reverse)

for dev_commit in $dev_commits
do
	dev_subject=$(git show "$dev_commit" --pretty=%s --quiet)
	dev_subject=$(echo "$dev_subject" | awk '{print tolower($0)}')

	if echo "$maintainer_subjects" | grep "$dev_subject\n" --quiet
	then
		continue
	fi

	echo "$dev_commit"
done
