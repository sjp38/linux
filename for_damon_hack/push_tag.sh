#!/bin/bash

if [ $# -ne 1 ]
then
	echo "Usage: $0 <tag>"
	exit 1
fi

tag=$1

# sj.korg git@gitolite.kernel.org:pub/scm/linux/kernel/git/sj/linux.git
# gh.public       git@github.com:sjp38/linux.git
# gh.damon        git@github.com:damonitor/linux.git

echo "ensure gpg password"
gpg-connect-agent updatestartuptty /bye > /dev/null
ssh gitolite.kernel.org help > /dev/null

echo "sj.korg"
git push sj.korg "$tag"

echo "gh.public"
git push gh.public "$tag"

echo "gh.damon"
git push gh.damon "$tag"
