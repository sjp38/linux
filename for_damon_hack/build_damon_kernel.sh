#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <output dir>"
	exit 1
fi

outdir=$1

make O="$outdir" olddefconfig
echo 'CONFIG_DAMON=y' >> $outdir/.config
echo 'CONFIG_DAMON_KUNIT_TEST=y' >> $outdir/.config
echo 'CONFIG_DAMON_VADDR=y' >> $outdir/.config
echo 'CONFIG_DAMON_PADDR=y' >> $outdir/.config
echo 'CONFIG_DAMON_PGIDLE=y' >> $outdir/.config
echo 'CONFIG_DAMON_VADDR_KUNIT_TEST=y' >> $outdir/.config
echo 'CONFIG_DAMON_DBGFS=y' >> $outdir/.config
echo 'CONFIG_DAMON_DBGFS_KUNIT_TEST=y' >> $outdir/.config
echo 'CONFIG_DAMON_RECLAIM=y' >> $outdir/.config
make O=$outdir -j$(nproc)
