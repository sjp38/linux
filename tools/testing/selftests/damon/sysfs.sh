#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

# Kselftest frmework requirement - SKIP code is 4.
ksft_skip=4

ensure_dir()
{
	dir=$1
	to_ensure=$2
	if [ "$to_ensure" = "exist" ] && [ ! -d "$dir" ]
	then
		echo "$dir dir is expected but not found"
		exit 1
	elif [ "$to_ensure" = "not_exist" ] && [ -d "$dir" ]
	then
		echo "$dir dir is not expected but found"
		exit 1
	fi
}

ensure_file()
{
	file=$1
	to_ensure=$2
	permission=$3
	if [ "$to_ensure" = "exist" ]
	then
		if [ ! -f "$file" ]
		then
			echo "$file is expected but not found"
			exit 1
		fi
		perm=$(stat -c "%a" "$file")
		if [ ! "$perm" = "$permission" ]
		then
			echo "$file permission: expected $permission but $perm"
			exit 1
		fi
	elif [ "$to_ensure" = "not_exist" ] && [ -f "$dir" ]
	then
		echo "$file is not expected but found"
		exit 1
	fi
}

test_kdamond()
{
	kdamond_dir=$1

	ensure_dir "$kdamond_dir" "exist"
	if ! pushd "$dir"
	then
		echo "pushd $dir failed"
		exit 1
	fi

	ensure_dir "contexts" "exist"
	ensure_file "pid" "exist" 400

	popd
}

test_kdamonds()
{
	local damon_sysfs=$1

	if ! pushd "$damon_sysfs"
	then
		echo "pushd $damon_sysfs failed"
		exit 1
	fi

	ensure_dir "kdamonds" "exist"
	ensure_file "kdamonds/nr" "exist" "600"
	ensure_file "monitor_on" "exist" "600"

	if ! echo 2 > kdamonds/nr
	then
		echo "writing 2 to kdamonds/nr failed"
		exit 1
	fi

	test_kdamond "kdamonds/0"
	test_kdamond "kdamonds/1"

	if ! echo 3 > kdamonds/nr
	then
		echo "writing 3 to kdamonds/nr failed"
		exit 1
	fi

	test_kdamond "kdamonds/0"
	test_kdamond "kdamonds/1"
	test_kdamond "kdamonds/2"

	if ! echo 0 > kdamonds/nr
	then
		echo "writing 0 to kdamonds/nr failed"
		exit 1
	fi

	ensure_dir "kdamonds/0" "not_exist"
	ensure_dir "kdamonds/1" "not_exist"
	ensure_dir "kdamonds/2" "not_exist"

	popd
}

check_dependencies()
{
	if [ $EUID -ne 0 ]
	then
		echo "Run as root"
		exit $ksft_skip
	fi

	if [ ! -d "$damon_sysfs" ]
	then
		echo "$damon_sysfs not found"
		exit $ksft_skip
	fi
}

damon_sysfs=/sys/kernel/mm/damon

check_dependencies
test_kdamonds "$damon_sysfs"
