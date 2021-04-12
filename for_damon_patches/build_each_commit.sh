#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <commits range>"
	exit 1
fi

range=$1
commits=()
while IFS= read -r line
do
	commits+=( "$line" )
done < <(git log --oneline --reverse "$range")

nr_pass=0
nr_fails=0
results=()
max_reply=5
ODIR=$HOME/damon_build_per_commit_outdir
for ((i = 0; i < ${#commits[@]}; i++))
do
	commit=${commits[$i]}
	hashid=$(echo $commit | awk '{print $1}')
	git checkout "$hashid"

	result="FAIL"
	for ((j = 0; j < $max_reply; j++))
	do
		make O=$ODIR olddefconfig
		echo 'CONFIG_DAMON=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_KUNIT_TEST=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_VADDR=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_PADDR=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_PGIDLE=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_VADDR_KUNIT_TEST=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_DBGFS=y' >> $ODIR/.config
		echo 'CONFIG_DAMON_DBGFS_KUNIT_TEST=y' >> $ODIR/.config
		if make O=$ODIR -j$(nproc)
		then
			result="PASS"
			break
		fi
	done

	if [ "$result" == "PASS" ]
	then
		nr_pass=$((nr_pass + 1))
	else
		nr_fails=$((nr_fails + 1))
	fi
	results+=("[$result] $commit")
done


echo
for ((i = 0; i < ${#results[@]}; i++))
do
	echo "${results[$i]}"
done

echo "PASS/FAIL: $nr_pass/$nr_fails"
