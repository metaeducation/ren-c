#!/usr/bin/env sh

# Root directory of this script
rdir=$(cd `dirname $0` && pwd)

# Instead of testing if build directory exist, make it
mkdir -p "$rdir/build"


# Go to build directory and search for a built binary
cd "$rdir/build"
r3bin=$(find . -name "r3*" | head -n 1)

if [[ -n $r3bin ]] ; then
    echo "Selected built binary: $r3bin"
else
    echo "Error: no binary available"
    exit 1
fi

echo "Run a test with yours parameters"
echo "$r3bin ../tests/run-recover.r"
$r3bin ../tests/run-recover.r
