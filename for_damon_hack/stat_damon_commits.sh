#!/bin/bash

pr_stat()
{
	if [ $# -ne 1 ]
	then
		echo "Usage: $0 <revision range>"
		exit 1
	fi

	range=$1
	from_sj=$(git log "$range" --oneline --author=SeongJae -- \
		mm/damon/ \
		include/linux/damon.h \
		Documentation/admin-guide/mm/damon/ \
		Documentation/mm/damon/ \
		Documentation/vm/damon/ \
		Documentation/ABI/testing/sysfs-kernel-mm-damon \
		tools/testing/selftests/damon \
		| wc -l)
	from_comm=$(git log "$range" --oneline \
		--perl-regexp --author='^((?!SeongJae).*)$' -- \
		mm/damon/ \
		include/linux/damon.h \
		Documentation/admin-guide/mm/damon/ \
		Documentation/mm/damon/ \
		Documentation/vm/damon/ \
		Documentation/ABI/testing/sysfs-kernel-mm-damon \
		tools/testing/selftests/damon \
		| wc -l)

	echo "$range	$from_sj	$from_comm	$((from_comm * 100 / (from_sj + from_comm))) %"
}

echo "range	from_sj		from_comm	from_comm rate (%)"
prev_version=v5.14
for version in v5.15 v5.16 v5.17 v5.18 v5.19 linus/master
do
	pr_stat "$prev_version..$version"
	prev_version=$version
done

pr_stat "v5.14..linus/master"
