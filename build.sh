#!/bin/bash

# Loaded into .travis.yml ("install" step)

set -x  # turn on debug mode (want to see the command lines being run)

## INSTALL PRE-REQUISITES FOR BUILD ##

# Fetch a Rebol binary to use as the "r3-make".
# Its job is to process C and Rebol sources to automatically generate some
# header files and embedded Rebol code bundles used in bootstrap, as well as
# to generate makefiles.
#
# !!! Originally, the Rebol that was fetched was the last r3-alpha that was
# published on rebol.com, to ensure that it could still be used for bootstrap.
# Conversion of the build in mid-2017 to using %rebmake.r meant that only a
# semi-recent Ren-C could be used to run the build prep steps.  There will not
# be an attempt to go back to building with R3-Alpha, though a more purposeful
# stable build should be picked at some point to replace the ad-hoc choice
# which wound up in the make dir.  The shim which is required to upgrade that
# older Ren-C is now rather "thick", and slows the build down significantly.
#

export PREBUILT="$(pwd)/prebuilt"  # see README.md in %prebuilt/ directory

# If we do two builds, we use the bootstrap executable for the first and the
# resulting Rebol from that build to do the second.  To keep from making
# mistakes and accidentally using the wrong r3-make, the executables are
# named `r3-one` and `r3-two`.  (If we only do one build, then r3-one will
# just get renamed to r3-two.)

if [[ $TRAVIS_OS_NAME = linux ]]; then
	wget -O "${PREBUILT}/r3-one" https://s3.amazonaws.com/r3bootstraps/r3-linux-x64-8994d23
elif [[ $TRAVIS_OS_NAME = osx ]]; then
	#
	# Brew was being installed even on minimal release builds, making it
	# unnoticed that there was a .dylib dependency from the bootstrap
	# release executable on ZeroMQ and ODBC.  This old debug build was
	# captured at the same version number as the bootstrap executables.
	# Use it for now until the next capture for the old OS X.
	#
	wget -O "${PREBUILT}/r3-one" https://s3.amazonaws.com/r3bootstraps/r3-osx-x64-8994d23-debug
else
	echo "!!! Unknown Travis Platform !!!"
	exit 1
fi

chmod +x "${PREBUILT}/r3-one"  # make bootstrap Rebol tool executable

PATH="${PREBUILT}":$PATH  # so we can just say `r3-one` or `r3-two`

# Install "Homebrew" package manager.  The Travis OS X images have brew
# preinstalled, but get old and out of sync with the package database.
# This means brew has to update.  It's supposed to do this automatically,
# but circa 2017 it broke in some images due to the lack of a sufficiently
# up-to-date Ruby:
#
# https://github.com/travis-ci/travis-ci/issues/8552

if [[ $TRAVIS_OS_NAME = osx && $BREW = yes ]]; then
	#
	# But even if brew can update automatically, it generates an unsightly
	# amount of output if it does so during a `brew install`.  So redirect
	# the hundreds of lines to /dev/null to shorten the Travis log.
	#
	# https://github.com/Homebrew/legacy-homebrew/issues/35662
	#
	brew update > /dev/null

	brew install unixodbc  # for ODBC extension
	brew install zmq  # for ZeroMQ extension
fi


if [[ $OS_ID = 0.16.1 || $OS_ID = 0.16.2 ]]; then
	git clone --depth 1 https://github.com/emscripten-core/emsdk
	cd emsdk
	./emsdk update-tags  # no `emsdk update` on git clone, use git pull

	# We use the latest incoming branch of emscripten (`tot-upstream`), not
	# the latest upstream release (`latest-upstream`) or the latest stable
	# release (`latest`).  While this is a more volatile choice, it helps
	# find out about potential problems faster...and make use of patches
	# that are applied to address our issues as soon as they are available.
	#
	./emsdk install tot-upstream
	./emsdk activate tot-upstream

	source emsdk_env.sh  # `source` => "inherit the environment variables"
	cd ..
fi

## BUILD ##

TOP_DIR="$(pwd)"  # https://stackoverflow.com/a/10795195/


"${CROSS_PREFIX}gcc" --version  # Output version of gcc for the log

# Ask for as many parallel builds as the virtual host supports

if [[ $TRAVIS_OS_NAME = linux ]]; then
  export MAKE_JOBS="$(nproc)"
fi
if [[ $TRAVIS_OS_NAME = osx ]]; then
  export MAKE_JOBS="$(sysctl -n hw.ncpu)"
fi

# Native Development Kit (NDK) is for C/C++ cross-compilation to Android.
# https://developer.android.com/ndk
# @giuliolunati is the maintainer and active user of the Android build.

if [[ $OS_ID = 0.13.2 ]]; then
    if [[ $(uname -m) = x86_64 ]]; then
        wget https://github.com/giuliolunati/android-travis/releases/download/v1.0.0/android-ndk-r13.tgz
        export ANDROID_NDK="${TOP_DIR}/android-ndk-r13"
    else
        exit 1
    fi
    tar zxf android-ndk-r13.tgz
    echo "$(pwd)"
    ls -dl "$(pwd)/android-ndk-r13"
    export CROSS_PREFIX="${ANDROID_NDK}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-"
    export SYSROOT="${ANDROID_NDK}/platforms/android-19/arch-arm"
fi

# @ShixinZeng added pre-built FFI libraries that are compatible with the
# MinGW cross-compiler and committed them to the git repository, because
# that was apparently easier/faster than trying to build it here
#
# !!! Review making this use the package manager's libffi if possible

if [[ $OS_ID = 0.3.40 || $OS_ID = 0.3.1 ]]; then  # Win32/Win64
    if [[ $OS_ID = 0.3.40 ]]; then
        export PKG_CONFIG_PATH="${TOP_DIR}/extensions/ffi/ffi-prebuilt/lib64/pkgconfig"
    else
        export PKG_CONFIG_PATH="${TOP_DIR}/extensions/ffi/ffi-prebuilt/lib32/pkgconfig"
    fi

    # Check cflags and libs
    # ("--define-prefix would be better, but it is not recognized")
    #
    pkg-config --define-variable="prefix=\"${TOP_DIR}/extensions/ffi/ffi-prebuilt\"" --cflags libffi
    pkg-config --define-variable="prefix=\"${TOP_DIR}/extensions/ffi/ffi-prebuilt\"" --libs libffi
fi

# Linux needs to build FFI if it is cross-compiling to 32-bit.  It's not
# clear why it wouldn't use the `sudo apt get` FFI packages of the distro
# on 64-bit (other than to use a consistent version committed in the repo)

if [[ -z $WITH_FFI || $WITH_FFI != no ]]; then
    mkdir ffi-build
    cd external/libffi
    ./autogen.sh
    cd "${TOP_DIR}/ffi-build"
    if [[ -z $HOST ]]; then
        "${TOP_DIR}/external/libffi/configure" --prefix="$(pwd)/fakeroot" CFLAGS="$ARCH_CFLAGS"
    else #cross-compiling
        "${TOP_DIR}/external/libffi/configure" --prefix="$(pwd)/fakeroot" --host="$HOST"
    fi
    make -j "$MAKE_JOBS"
    make install
    export PKG_CONFIG_PATH="$(pwd)/fakeroot/lib/pkgconfig"
    # check cflags and libs
    pkg-config --cflags libffi
    pkg-config --libs libffi

    ls "$(pkg-config --variable=toolexeclibdir libffi)"

    #remove dynamic libraries to force it to link with static libraries
    rm -f "$(pkg-config --variable=toolexeclibdir libffi)/*.so*"
    rm -f "$(pkg-config --variable=toolexeclibdir libffi)/*.dylib*"
    rm -f "$(pkg-config --variable=toolexeclibdir libffi)/*.dll*"
    
    ls "$(pkg-config --variable=toolexeclibdir libffi)"

    echo "FFI builds are erroring for now, above command fails"
    echo "Revisit when time permits - not currently being used"
fi

mkdir "${TOP_DIR}/build"
cd "${TOP_DIR}/build"

# For building the TCC extension, it requires both the TCC library (API
# you can link in to pass a string of code and compile it), as well as
# runtime support for that code once compiled.  See TCC extension README.
#
# While Fabrice Bellard is not involved in TCC development any longer,
# others are maintaining it.  Modifications have been proposed which would
# make the "Standalone Building" feature for Rebol better:
#
# http://lists.nongnu.org/archive/html/tinycc-devel/2018-12/msg00011.html
#
# But given priorities, there are not enough Rebol dev resources to
# develop and maintain a separate fork of TCC for that.
#

if [[ ! -z $TCC ]]; then
    if [[ 0 = 1 ]]; then
        #
        # !!! What's a better representation for literal false in bash?
        #
        # !!! was if [[ $OS_ID == 0.4.40 ]], but the old trusty doesn't
        # have working variadics in its stock libtcc)
        #
        # ---
        #
        # Just use the tcc and libtcc installed by the package manager
        # (in the matrix settings above).  That's because the platform
        # the TCC inside Rebol will be compiling for after installation
        # on the user's machine is identical to the Travis container
        # doing the compilation.
        #
        # This directory may vary, find with `dpkg -L tcc`
        #
        # (export is so that if we run r3 to test tcc, it can see it)
        #
        export CONFIG_TCCDIR=/usr/lib/tcc  # no slash!
    else
        # If cross-compiling, we can't merely `sudo apt-get` the libtcc
        # and resources we need...have to build it from scratch.

        # By using `--depth=1` this gets the publicly updated *latest
        # commit only* (don't bother cloning the entire history)
        #
        git clone --depth=1 git://repo.or.cz/tinycc.git tcc

        # TCC has an %include/ directory at the top level of the build
        # (for the unix-style files) and tends to generate libtcc1.a in
        # the top-level directory also.  But if you're using Win32, you
        # want to set CONFIG_TCCDIR to the %win32/ subdir, which
        # contains a different %include/ as well as a %lib/ dir.
        #
        # (export is so that if we run r3 to test tcc, it can see it)
        cd tcc
        export CONFIG_TCCDIR="$(pwd)"

        # If you want a "tcc.exe" cross compiler, you could use
        # `./configure --enable-cross` and then `make`.  However, a TCC
        # executable is no longer needed to build the TCC extension.

        echo "Generating libtcc.a (provides the libtcc compilation API)"

        # libtcc.a requires --config-mingw32, or it doesn't think it's a
        # native compiler and disables functions in tccrun.c (including
        # the important `tcc_relocate()`
        #
        # !!! If the 64-bit tries to use `--config-mingw64`, then it
        # also doesn't define `TCC_IS_NATIVE` on windows, because it
        # seems `TCC_TARGET_PE` (Program Executable, e.g. Windows) is
        # only set by `--config-mingw32`.  Review.

        if [[ $OS_ID = 0.4.40 ]]; then
            ./configure --cpu=x86_64 --enable-static --extra-cflags="$ARCH_CFLAGS" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.4.4 ]]; then
            ./configure --cpu=x86 --enable-static --extra-cflags="$ARCH_CFLAGS" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.3.1 ]]; then  #x86-win32
            ./configure --config-mingw32 --cpu=x86 --enable-static --extra-cflags="$ARCH_CFLAGS" --cross-prefix="$CROSS_PREFIX" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.13.2 ]]; then  #arm-android5
            ./configure --cpu=arm --enable-static --extra-cflags="$ARCH_CFLAGS --sysroot=\"$SYSROOT\"" --cross-prefix="$CROSS_PREFIX" --extra-ldflags="$ARCH_CFLAGS --sysroot=\"$SYSROOT\""
        else  #x86_64-win32, see note that --config-mingw32 vs. --config-mingw64 is intentional
            ./configure --config-mingw32 --cpu=x86_64 --enable-static --extra-cflags="$ARCH_CFLAGS" --cross-prefix="$CROSS_PREFIX" --extra-ldflags="$ARCH_CFLAGS"
        fi
        make libtcc.a && cp libtcc.a libtcc.a.bak

        make clean

        # For the generation of %libtcc1.a, `--config-mingw32` must be
        # turned off, or it will try to compile with tcc.exe

        echo "Generating libtcc1.a (runtime lib for TCC-built programs)"

        if [[ $OS_ID = 0.4.40 ]]; then
            ./configure --cpu=x86_64 --extra-cflags="$ARCH_CFLAGS" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.4.4 ]]; then
            ./configure --cpu=x86 --extra-cflags="$ARCH_CFLAGS" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.3.1 ]]; then  # x86-win32
            ./configure --cpu=x86 --extra-cflags="$ARCH_CFLAGS" --cross-prefix="$CROSS_PREFIX" --extra-ldflags="$ARCH_CFLAGS"
        elif [[ $OS_ID = 0.13.2 ]]; then  # arm-android5
            ./configure --cpu=arm --extra-cflags="$ARCH_CFLAGS --sysroot=\"$SYSROOT\"" --cross-prefix="$CROSS_PREFIX"  --extra-ldflags="$ARCH_CFLAGS --sysroot=\"$SYSROOT\""
        else  # x86_64-win32
            ./configure --cpu=x86_64 --extra-cflags="$ARCH_CFLAGS" --cross-prefix="$CROSS_PREFIX" --extra-ldflags="$ARCH_CFLAGS"
        fi

        echo "make libtcc1.a"
        # Note: Could fail to build tcc due to lack of '-ldl' on Windows
        make libtcc1.a XCC="${CROSS_PREFIX}gcc" XAR="${CROSS_PREFIX}ar" || echo "ignoring error in building libtcc1.a"
        cp bin/* .  # restore cross-compilers, libtcc1.a depends on tcc
        touch tcc  # update the timestamp so it won't be rebuilt
        echo "ls"
        ls  # take a look at files under current directory
        echo "make libtcc1.a"
        make libtcc1.a XCC="${CROSS_PREFIX}gcc" XAR="${CROSS_PREFIX}ar"
        "${CROSS_PREFIX}ar" d libtcc1.a armeabi.o  # avoid link conflict with libtcc.a

        #restore libtcc.a
        # make libtcc1.a could have generated a new libtcc.a
        cp libtcc.a.bak libtcc.a

        echo "Objdumping!"
        objdump -a libtcc.a
    fi
    make
    cd "${TOP_DIR}/build"
fi

# Grab the abbreviated and full git commit ID into environment variables.
# The full commit is passed to make to build into the binary, and the
# abbreviated commit is used to name the executable.
#
# http://stackoverflow.com/a/42549385/211160
#
GIT_COMMIT="$(git show --format="%H" --no-patch)"
echo "$GIT_COMMIT"
GIT_COMMIT_SHORT="$(git show --format="%h" --no-patch)"
echo "$GIT_COMMIT_SHORT"

# As an extra step to test bootstrap ability, if the build can be run on
# the Travis we are using...make the debug build go even further by doing
# another full build, but using the just built r3 as its own r3-make.
# This will work on 64-bit OS X or Linux, and 32-bit if 32-bit support has
# been installed
#

if [[ $DEBUG != none && ($OS_ID = 0.4.40 || $OS_ID = 0.4.4 || $OS_ID = 0.2.40) ]]; then
    #
    # If building twice, don't specify GIT_COMMIT for the first build.
    # This means there's a test of the build process when one is not
    # specified, in case something is broken about that.  (This is how
    # most people will build locally, so good to test it.)
    #
    # Also request address sanitizer to be used for the first build.  It
    # is very heavyweight and makes the executable *huge* and slow, so
    # we do not apply it to any of the binaries which are uploaded to s3
    # -- not even debug ones.
    #
    r3-one ../make.r CONFIG="../configs/${CONFIG}" TARGET=makefile STANDARD="$STANDARD" OS_ID="$OS_ID" RIGOROUS="$RIGOROUS" DEBUG=sanitize OPTIMIZE=2 STATIC=no CFLAGS="$CFLAGS" ODBC_REQUIRES_LTDL="$ODBC_REQUIRES_LTDL" EXTENSIONS="$EXTENSIONS" $EXTRA
    make clean prep folders
    make -j "$MAKE_JOBS"

    # We have to set R3_MAKE explicitly to circumvent the automatic
    # r3-make filename inference, as we always use Linux "r3-two"
    # (not "r3-two.exe") even when doing windows builds, since this is
    # a cross-compilation.
    #
    mv r3 "${PREBUILT}/r3-two"
    rm "${PREBUILT}/r3-one"
    export R3_MAKE="${PREBUILT}/r3-two"

    # Now that we've moved the `r3` out of the way, try cleaning up the
    # other build products...and make sure it doesn't accidentally pick
    # up the old makefile.
    #
    make clean
    rm makefile

    # This instructs `r3-two` to use a mode in which allocations are not
    # pooled, but each uses an individual `malloc()`.  That means we
    # get the benefit of the checked/hooked allocator for address
    # sanitizer, which the pools don't have a parallel to at the moment.
    #
    export R3_ALWAYS_MALLOC=1

    # We'll do a simple HTTPS read test on the second build.  But on
    # this first build, we also do it because it has the address
    # sanitizer, which may catch more problems.
    #
    r3-two --do "print {Testing...} quit either find to-text read https://example.com {<h1>Example Domain</h1>} [0] [1]";
    R3_EXIT_STATUS=$?
    echo $R3_EXIT_STATUS
else
  mv "${PREBUILT}/r3-one" "${PREBUILT}/r3-two"
fi


if [[ -z $TCC ]]; then
  export WITH_TCC=no
else
  export WITH_TCC="%${PWD}/tcc/${TCC}"
fi

# On the second build of building twice, or just building once, include
# the GIT_COMMIT
#
r3-two ../make.r CONFIG="../configs/${CONFIG}" TARGET=makefile STANDARD="$STANDARD" OS_ID="$OS_ID" DEBUG="$DEBUG" GIT_COMMIT="{$GIT_COMMIT}" RIGOROUS="$RIGOROUS" STATIC="$STATIC" WITH_FFI="$WITH_FFI" WITH_TCC="$WITH_TCC" ODBC_REQUIRES_LTDL="$ODBC_REQUIRES_LTDL" EXTENSIONS="$EXTENSIONS" $EXTRA

# Do the main build of the files we will be uploading
#
make clean prep folders
make -j "$MAKE_JOBS"

# Dump out a list of the library dependencies of the executable

if [[ $OS_ID = 0.4.40 || $OS_ID = 0.4.4 ]]; then
    ldd ./r3
elif [[ $OS_ID = 0.2.40 ]]; then
    otool -L ./r3
fi

# Run once but don't pipe output, in case it prints out useful crash msg
# that we want to see in the Travis log (especially helpful for failures
# only happening in the Travis builds that aren't reproducing locally)
# Save the exit code ($?) so we can return it to Travis as last step
#
# !!! This is a very minimal sanity check to ensure the built R3 does
# *something*, and it can't be used on cross-compilations (e.g. a Windows
# executable won't run on Linux).  Running the full test suite would be a
# bit much, and developers are expected to have already done that.  But
# doing an HTTPS read exercises a fair amount of code.
#

if [[ $OS_ID = 0.4.40 || $OS_ID = 0.4.4 || $OS_ID = 0.2.40 ]]; then
    $RUNNER ./r3 --do "print {Testing...} quit either find to-text read https://example.com {<h1>Example Domain</h1>} [0] [1]"
    R3_EXIT_STATUS=$?
else
    R3_EXIT_STATUS=0
fi
echo "$R3_EXIT_STATUS"

# Run basic testing on FFI if it is included and this container can run it

if [[ -z $WITH_FFI || $WITH_FFI != no ]]; then
  if [[ $OS_ID = 0.4.40 || $OS_ID = 0.4.4 ]]; then
    $RUNNER ./r3 ../tests/misc/qsort_r.r
    R3_EXIT_STATUS=$?
  else
    R3_EXIT_STATUS=0
  fi
else
    R3_EXIT_STATUS=0
fi
echo "$R3_EXIT_STATUS"

# If the build is TCC-based and the host can run the produced executable,
# do a simple test that does a TCC fibonnaci calculation.

# The TCC extension currently looks at the environment variable
# LIBREBOL_INCLUDE_DIR to find "rebol.h".  Ultimately, this should be
# shipped with the TCC extension.  (It may be desirable to embed it,
# but it also may be desirable to have a copy that a file can find
# via `#include "rebol.h"`)
#
# (export is so that the child process can see the environment variable)
#
echo "Note: CONFIG_TCCDIR is ${CONFIG_TCCDIR}"
export LIBREBOL_INCLUDE_DIR="${TOP_DIR}/build/prep/include"
if [[ (! -z $TCC) && ($TCC != no) ]]; then
  if [[ $OS_ID = 0.4.40 ]]; then
    $RUNNER ./r3 ../extensions/tcc/tests/fib.r
    R3_EXIT_STATUS=$?
  elif [[ $OS_ID = 0.4.4 ]]; then
    #
    # Most 32-bit builds are intending to run on 32-bit systems, hence
    # the running TCC should link to libs in `/usr/lib`.  But this
    # container is 64-bit (Travis does not support 32-bit containers,
    # nor do they seem to intend to).  The tcc extension heeds this
    # environment variable and uses hardcoded output of `gcc -v m32`
    # for library directories to override `/usr/lib` with.
    #
    export REBOL_TCC_EXTENSION_32BIT_ON_64BIT=1
    $RUNNER ./r3 ../extensions/tcc/tests/fib.r
    R3_EXIT_STATUS=$?
  fi
else
    R3_EXIT_STATUS=0
fi
echo $R3_EXIT_STATUS


if [[ $R3_EXIT_STATUS != 0 ]]; then
  exit $R3_EXIT_STATUS
fi

cd $TOP_DIR

set +x  # turn off debug mode (Travis outputs the commands in .travis.yml)

# vim: set et sw=2:
