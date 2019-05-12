#!/usr/bin/env bash

# Root directory of this script
rdir=$(cd `dirname $0` && pwd)

echo "Prepare the build directory $rdir/build"
mkdir -p "$rdir/build"

cd "$rdir/prebuilt"


echo ""
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
        echo "Error : you need wget or curl to download binaries."
        exit
    fi
fi

echo ""
echo "Get the list of available prebuilt binaries from S3"
s3url=https://r3bootstraps.s3.amazonaws.com/

if [ $dltool = "wget" ] ; then
    xml=$(wget -q $s3url -O -)
else
    xml=$(curl -s $s3url)
fi

pblist=$(echo "$xml" | sed -e 's/<\/Key>/<\/Key>\n/g' | sed -n -e 's/.*<Key>\(.*\)<\/Key>.*/\1/p')


echo ""
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


echo ""
echo "HAPPY HACKING!"

