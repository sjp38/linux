#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <commits range>"
	exit 1
fi

range=$1

max_reply=5

fails=()

ODIR=$HOME/damon_build_per_commit_outdir

while IFS= read -r line
do
	echo "test for $line"
	commit=$(echo $line | awk '{print $1}')
	git checkout "$commit"

	success="false"
	for ((i = 0; i < $max_reply; i++))
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
			success="true"
			break
		fi
	done

	if [ "$success" == "false" ]
	then
		fails+=( "$line" )
	fi
done < <(git log --oneline --reverse "$range")

echo
if [ ${#fails[@]} -eq 0 ]
then
	echo "PASS"
	exit 0
fi

echo "FAIL"
echo "failed commits:"
for ((i = 0; i < ${#fails[@]}; i++))
do
	echo "${fails[$i]}"
done
exit 1
