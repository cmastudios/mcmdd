#!/bin/bash
set -e
# Ignore OS X resource forks (e.g. ._ files)
export COPYFILE_DISABLE=true
version="$1"
src=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
cd /tmp
rm -rf "mcmdd-$version"
cp -r "$src" "mcmdd-$version"
cd "mcmdd-$version"
rm -rf build *.tar.gz .git .gitignore
cd ..
tar czvf "$src/mcmdd-$version.tar.gz" "mcmdd-$version"
rm -rf "mcmdd-$version"

