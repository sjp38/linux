#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <revision-range>"
	exit 1
fi

revision_range=$1

git log "$revision_range" --oneline -- \
	Documentation/admin-guide/mm/damon/ Documentaiton/vm/damon/ \
       	include/linux/damon.h include/trace/events/damon.h \
	mm/damon \
	tools/testing/selftests/damon
