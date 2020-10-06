#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <build dir>"
	exit 1
fi

build_dir=$1
bindir=$(dirname "$0")
srcdir="$bindir/../"

cat "$bindir/damon_config" >> "$build_dir/.config"
make -C "$srcdir" O="$build_dir" olddefconfig
