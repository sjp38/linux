#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <output dir>"
	exit 1
fi

outdir=$1

bindir=$(dirname "$0")

"$bindir/config_damon_kernel.sh" "$outdir"
make O=$outdir -j$(nproc)
