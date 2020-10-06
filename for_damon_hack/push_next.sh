#!/bin/bash

echo "ensure gpg password"
bindir=$(dirname "$0")
"$bindir/ensure_gpg_password.sh"

echo "sj.korg"
git push sj.korg damon/next --force

echo "gh.public"
git push gh.public damon/next --force

echo "gh.damon"
git push gh.damon damon/next:next --force
