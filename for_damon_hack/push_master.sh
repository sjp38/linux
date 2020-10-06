#!/bin/bash

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

echo "sj.korg"
git push sj.korg damon/master --force

echo "gh.public"
git push gh.public damon/master --force

echo "gh.damon"
git push gh.damon damon/master:master --force
