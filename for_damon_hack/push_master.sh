#!/bin/bash

# sj.korg git@gitolite.kernel.org:pub/scm/linux/kernel/git/sj/linux.git
# gh.public       git@github.com:sjp38/linux.git
# gh.damon        git@github.com:damonitor/linux.git

echo "ensure gpg password"
gpg-connect-agent updatestartuptty /bye > /dev/null
ssh gitolite.kernel.org help > /dev/null

echo "sj.korg"
git push sj.korg damon/master --force

echo "gh.public"
git push gh.public damon/master --force

echo "gh.damon"
git push gh.damon damon/master:master --force
