#!/bin/bash

pr_usage()
{
	echo "Usage: $0 [OPTION]... <revision range>"
	echo
	echo "OPTION"
	echo "  --reverse	Print in reverse"
	echo "	--cherry-pick	Cherry-pick the commits"
	echo "  -h, --help	Show this usage"
}

pr_usage_exit()
{
	exit_code=$1
	pr_usage
	exit "$exit_code"
}

if [ $# -lt 1 ]
then
	pr_usage_exit 1
fi

reverse="false"
cherry_pick="false"

while [ $# -ne 0 ]
do
	case $1 in
	"--reverse")
		reverse="true"
		shift 1
		continue
		;;
	"--cherry-pick")
		cherry_pick="true"
		shift 1
		continue
		;;
	"--help" | "-h")
		pr_usage_exit 0
		;;
	*)
		if [ $# -ne 1 ]
		then
			pr_usage_exit 1
		fi
		revision_range=$1
		break;;
	esac
done

damon_files="Documentation/admin-guide/mm/damon/ Documentaiton/vm/damon/ \
       	include/linux/damon.h include/trace/events/damon.h \
	mm/damon \
	tools/testing/selftests/damon"

if [ "$cherry_pick" = "true" ]
then
	for commit in $(git log --pretty=%H --reverse $revision_range \
		-- $damon_files)
	do
		if ! git cherry-pick $commit
		then
			echo "cherry picking $commit failed"
			exit 1
		fi
	done
	exit 0
fi

log_command="git log $revision_range --oneline"

if [ "$reverse" = "true" ]
then
	log_command="$log_command --oneline --reverse"
fi

log_command+=" -- $damon_files"

eval $log_command
