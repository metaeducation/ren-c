#!/usr/bin/env bash

#
# make.sh
#
# This is a simple script that will kick off a default build process that
# should work on many systems.
#

# The utility scripts in %bash/ all expect $repo_dir to be set
# Note Ren-C conventions always include slashes in directory names
#
repo_dir=$(cd `dirname $0` && pwd)/

source ${repo_dir}tools/bash/log.sh
source ${repo_dir}tools/bash/fetch-prebuilt.sh

# Check available build binaries for the current OS or download one.
#
r3make=$(fetch_prebuilt)
if [[ -n $r3make ]]; then
    echo "Selected prebuilt binary: $r3make"
else
    echo "Error: no prebuilt binary available"
    exit 1
fi


echo "Prepare the build directory ${repo_dir}build"
mkdir -p "${repo_dir}build"
cd "${repo_dir}build"

echo "Run a build with your parameters"
echo "$r3make ../make.r $@"
"$r3make" ../make.r $@
