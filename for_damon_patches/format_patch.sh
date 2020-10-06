#!/bin/bash

if [ $# -lt 3 ]
then
	echo "Usage: $0 <commit range> <subject-prefix> <output dir> [recipients file>]"
	exit 1
fi

commit_range=$1
subject_prefix=$2
outdir=$3
recipients_file=$4

if [ -f "$recipients_file" ]
then
	recipients=$(cat "$recipients_file")
fi

coverletter="$outdir/0000-cover-letter.patch"
if [ -f "$coverletter" ]
then
	cp "$coverletter" "$coverletter.old"
fi

git format-patch $recipients --cover-letter \
	--subject-prefix "$subject_prefix" -o "$outdir" $commit_range

echo
echo "# Patch files are ready at $outdir"
echo
echo "# Do checkpatch"

failed=()
for patch in "$outdir"/*.patch
do
	echo "$(basename "$patch")"
	result=$(./scripts/checkpatch.pl "$patch" | grep total)
	echo "    $result"
	errors=$(echo $result | awk '{print $2}')
	warnings=$(echo $result | awk '{print $4}')
	if [ "$errors" -gt 0 ] || [ "$warnings" -gt 0 ]
	then
		failed+=($patch)
	fi
done

echo
echo "Below patches contain warnings and/or errors"
for ((i = 0; i < ${#failed[@]}; i++))
do
	echo "$(basename ${failed[$i]})"
done
