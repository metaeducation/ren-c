#!/usr/bin/env sh

# Root directory of this script
rdir=$(cd `dirname $0` && pwd)

echo "Prepare the build directory $rdir/build"
mkdir -p "$rdir/build"

# Go to prebuilt directory to check available binaries
# for the current OS or download one
cd "$rdir/prebuilt"

# Identify OS
usys=$(uname -s | tr '[:upper:]' '[:lower:]')

case $usys in
    *cygwin*)  localsys="windows";;
    *mingw*)   localsys="windows";;
    *msys*)    localsys="windows";;
    *windows*) localsys="windows";;
    *linux*)   localsys="linux"
        case $(uname -o | tr '[:upper:]' '[:lower:]') in
            *android*) localsys="android";;
            *)         localsys="linux";;
        esac
    ;;
    *openbsd*) localsys="openbsd";;
    *darwin*)  localsys="osx";;
    *)         localsys="none";;
esac

if [ $localsys = "none" ] ; then
    echo "Error: Sorry there is still no precompiled binary for your operating system ($usys)."
    exit 1
else
    echo "Identified OS: $localsys ($usys)"
    echo "Search a prebuilt binary for your OS"

    if [[ -n "$(find . -name "*$localsys*" | head -n 1)" ]] ; then
        echo ""
    else
        echo "Clear last downloaded prebuilt binaries"
        rm -f r3-*
        
        # Search for wget or curl
        which wget > /dev/null
        
        if [ $? -eq 0 ] ; then
            dltool="wget"
        else
            which curl > /dev/null
            if [ $? -eq 0 ] ; then
                dltool="curl"
            else
                echo "Error: you need wget or curl to download binaries."
                exit 1
            fi
        fi
        
        
        echo "Get the list of available prebuilt binaries from S3"
        s3url=https://r3bootstraps.s3.amazonaws.com/
        
        if [ $dltool = "wget" ] ; then
            xml=$(wget -q $s3url -O -)
        else
            xml=$(curl -s $s3url)
        fi
        
        # Use tr instead of sed to replace newlines because sed is not really powerful on Mac/BSD
        pblist=$(echo "$xml" |  tr "<" "\n" | sed -n -e 's/^Key>\(+*\)/\1/p')
        
        echo "Download prebuilt binaries"
        echo ""
        
        for pb in $pblist
        do
            s3pb="$s3url$pb"
        
            if [ $dltool = "wget" ] ; then
                wget -nv -o - "$s3pb"
            else
                echo "$s3pb"
                curl "$s3pb" > "$pb"
                echo ""
            fi
        done
        
        
        echo ""
        echo "Make executable prebuilt binaries"
        chmod -f +x r3-*
    fi
fi


# Go to build directory and resolve the path
# of the prebuilt binary from that place
cd "$rdir/build"
r3bin=$(find ../prebuilt -name "*$localsys*" | head -n 1)

if [[ -n $r3bin ]] ; then
    echo "Selected prebuilt binary: $r3bin"
else
    echo "Error: no prebuilt binary available"
    exit 1
fi

echo "Run a build with yours parameters"
echo "$r3bin ../make.r $@"
#$r3bin ../make.r $@
