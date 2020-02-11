#!/bin/bash
# SPDX-License-Identifier: GPL-2.0

source ./_chk_dependency.sh

# Test attrs file
file="$DBGFS/attrs"

ORIG_CONTENT=$(cat $file)

echo 1 2 3 4 5 > $file
if [ $? -ne 0 ]
then
	echo "$file write failed"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo 1 2 3 4 > $file
if [ $? -eq 0 ]
then
	echo "$file write success (should failed)"
	echo $ORIG_CONTENT > $file
	exit 1
fi

CONTENT=$(cat $file)
if [ "$CONTENT" != "1 2 3 4 5" ]
then
	echo "$file not written"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo $ORIG_CONTENT > $file

# Test record file
file="$DBGFS/record"

ORIG_CONTENT=$(cat $file)

echo "4242 foo.bar" > $file
if [ $? -ne 0 ]
then
	echo "$file writing sane input failed"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo abc 2 3 > $file
if [ $? -eq 0 ]
then
	echo "$file writing insane input 1 success (should failed)"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo 123 > $file
if [ $? -eq 0 ]
then
	echo "$file writing insane input 2 success (should failed)"
	echo $ORIG_CONTENT > $file
	exit 1
fi

CONTENT=$(cat $file)
if [ "$CONTENT" != "4242 foo.bar" ]
then
	echo "$file not written"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo "0 null" > $file
if [ $? -ne 0 ]
then
	echo "$file disabling write fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

CONTENT=$(cat $file)
if [ "$CONTENT" != "0 null" ]
then
	echo "$file not disabled"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo "4242 foo.bar" > $file
if [ $? -ne 0 ]
then
	echo "$file writing sane data again fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo $ORIG_CONTENT > $file

# Test target_ids file
file="$DBGFS/target_ids"

ORIG_CONTENT=$(cat $file)

echo "1 2 3 4" > $file
if [ $? -ne 0 ]
then
	echo "$file write fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo "1 2 abc 4" > $file
if [ $? -ne 0 ]
then
	echo "$file write fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

CONTENT=$(cat $file)
if [ "$CONTENT" != "1 2" ]
then
	echo "$file not written"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo abc 2 3 > $file
if [ $? -ne 0 ]
then
	echo "$file wrong value write fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

if [ ! -z "$(cat $file)" ]
then
	echo "$file not cleared"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo > $file
if [ $? -ne 0 ]
then
	echo "$file init fail"
	echo $ORIG_CONTENT > $file
	exit 1
fi

if [ ! -z "$(cat $file)" ]
then
	echo "$file not initialized"
	echo $ORIG_CONTENT > $file
	exit 1
fi

echo $ORIG_CONTENT > $file

echo "PASS"
