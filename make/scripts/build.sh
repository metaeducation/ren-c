#!/bin/sh
#$0 [all|win-32|win-64|linux-32|linux-64] publish
version=3.0.0
BUILD_TIME=$(date +%Y-%m-%d/%H:%M:%S)

revision() {
	rev=$(git log|head -n 1|awk '{print $2}')
	echo ${rev:0:6}
}

setup() {
	#$1 platform
	timestamp=$(date +%Y-%m-%d)
	case $1 in 
		linux-32)
			MK="makefile-32"
			EXE="r3-view-linux"
			NAME="r3-32-view-linux-$timestamp-$(revision)"
			RELNAME="r3-32-view-linux"
			CFLAGS="-m32"
			;;
		linux-64)
			MK="makefile-64"
			EXE="r3-view-linux"
			NAME="r3-64-view-linux-$timestamp-$(revision)"
			RELNAME="r3-64-view-linux"
			;;
		win-32)
			MK="makefile-mingw-32"
			EXE="r3-view.exe"
			NAME="r3-32-view-$timestamp-$(revision).exe"
			RELNAME="r3-32-view.exe"
			HOST="i686-w64-mingw32"
			;;
		win-64)
			MK="makefile-mingw-64"
			EXE="r3-view.exe"
			NAME="r3-64-view-$timestamp-$(revision).exe"
			HOST="x86_64-w64-mingw32"
			RELNAME="r3-64-view.exe"
			;;
		armv7)
			EXE="r3-view-linux"
			MK="makefile-armv7"
			NAME="r3-armv7-view-$timestamp-$(revision)"
			RELNAME="r3-armv7hf-view-linux"
			;;
		*)
			echo "unsupported platform $1"
			exit 1
			;;
	esac
}

build() {
	#echo "make -f $MK $EXE"
	#echo "copy $EXE $NAME"
	DIR=`pwd`
	rm -fr $DIR/libffi
	cd ../src/libffi
	if [ -f configure ]; then
		make clean
	else
		./autogen.sh
	fi
	echo "CFLAGS: $CFLAGS"
	if [ -z $HOST ]; then
		if [ -z $CFLAGS ]; then
			./configure --prefix=$DIR/libffi
		else
			./configure --prefix=$DIR/libffi CFLAGS=$CFLAGS
		fi
	else
		if [ -z $CFLAGS ]; then
			./configure --prefix=$DIR/libffi --host=$HOST
		else
			./configure --prefix=$DIR/libffi --host=$HOST CFLAGS=$CFLAGS
		fi
	fi
	make
	make install
	cd $DIR
	make -f $MK clean
	make -f $MK $EXE
	make -f $MK strip-view
	cp $EXE $NAME
}

run() {
	#$1: platform
	echo "building for $1"
	CFLAGS=""
	HOST=""
	setup $1
	build
	if [ ! -z $2 ]; then
		echo "publishing $NAME"
		publish $NAME $RELNAME
	fi
}

if [ $1 == "all" ]; then
	for p in "win-32" "win-64" "linux-32" "linux-64"
	#for p in "win-32" "linux-64"
	do
		run $p $2
	done
else
	run $1 $2
fi
