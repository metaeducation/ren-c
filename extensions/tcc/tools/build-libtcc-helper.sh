#
# %build-libtcc-helper.sh
#
#====# IMPORTANT #============================================================#
#
#   BE SURE TO RUN THIS SCRIPT WITH SHELL SET TO -e !  It should exit if
#   there are any failed steps.  This is the default in GitHub CI for bash,
#   but if running interactively do `bash -e build-libtcc-helper.sh`
#
# You must have the following environment variables defined:
#
#    OS_ID - Operating System ID of the Target System
#    ANDROID_NDK_ROOT - Where Android Was Installed
#    NDK_REVISION - NDK identifier, currently r13 or r21
#
#====# DESCRIPTION #==========================================================#
#
# If you want to write a C program that is itself capable of compiling strings
# of C code using the TinyC compiler, you can get a library for doing so
# on Linux with apt:
#
#    sudo apt install libtcc-dev
#
# But this gets more tricky if you are cross-compiling.  For instance if you
# are on x86 Linux and trying to build C code into an executable that runs on
# ARM, you want the ARM version of libtcc-dev.  That's not what you'll get from
# the apt package on your x86 machine.
#
# This file contains script code that was in an automated build system that
# was able to build libtcc.a and libtcc1.a on Linux platforms targeting
# Windows (with MinGW) and ARM.
#
#====# NOTES #================================================================#
#
# * For explanations of the difference between libtcc.a and libtcc1.a, see
#   the README.md file for the TCC extension
#
# * This assumes your host platform (the one doing the cross-compilation) is
#   a 64-bit Linux machine.  So if you are producing a Windows version of
#   libtcc, then it needs MinGW to be installed.
#
# * The process here used to be more convoluted, because it included a first
#   phase build of an artifact that was an x86_64 Linux TCC for building
#   ARM output.  This was used to do preprocessing of %sys-core.h into a
#   %sys-core.i file, that could work on Windows or ARM:
#
#   https://forum.rebol.info/t/952
#
#   That method was dropped as being overcomplicated...but if something like it
#   were needed consider `./configure --enable-cross` and then `make`.
#


#====# CLONE TCC REPOSITORY IF NECESSARY #====================================#

echo "CLONE TCC REPOSITORY IF NECESSARY"

# Make it faster to debug script locally by being willing to not clone if
# a tcc directory already exists.

if [[ ! -d "tcc" ]]; then
    #
    # By using `--depth=1` this gets the publicly updated *latest commit only*
    # (don't bother cloning the entire history)
    #
    git clone --depth=1 git://repo.or.cz/tinycc.git tcc
fi

# TCC has an %include/ directory at the top level of the build (for the
# unix-style files) and tends to generate libtcc1.a in the top-level directory
# also.  But if you're using Win32, you want to set CONFIG_TCCDIR to the
# %win32/ subdir, which contains a different %include/ as well as a %lib/ dir.

cd tcc

make clean


#====# DERIVE ENVIRONMENT VARIABLES #=========================================#

# See top of file for the input environment variables that must be set.

echo "DERIVE ENVIRONMENT VARIABLES"

# At some point, a compilation option called `--config-predefs` defaulted to
# being `yes`, even if you are cross-compiling.  That tries to build and run
# a tool called c2str.exe (yes, .exe even on Linux) to process `tccdefs.h`
# into `tccdefs.h_`.  But it tries to do this with the cross compiler, which
# won't work.  We turn the option off--though presumably the makefile could be
# edited in TCC to use a host compiler for this step.
#
# We default to using no configuration predefs, though if we are not cross
# compiling this could theoretically be yes.
#
CONFIG_PREDEFS=no

# Default the compiler to gcc (MinGW or default GNU Linux).
# Note that Android NDK >= 18 only offers clang.
#
CC=gcc

if [[ $OS_ID = 0.4.40 ]]; then  # 64-bit Linux

    CROSS_ARCH=x86_64  # not actually cross compiling
    ARCH_CFLAGS=  # assume we only compile for 64-bit linux on 64-bit linux
    CROSS_PREFIX=  # not cross compiling, use plain `gcc`
    CONFIG_PREDEFS=yes  # override since not cross-compiling

elif [[ $OS_ID = 0.4.4 ]]; then  # 32-bit Linux

    CROSS_ARCH=x86
    ARCH_CFLAGS=-m32  # tell host's 64-bit compiler to generate 32-bit code
    CROSS_PREFIX=  # plain 64-bit Linux `gcc` can build for 32-bit Linux
    CONFIG_PREDEFS=yes  # override since not cross-compiling

elif [[ $OS_ID = 0.3.1 ]]; then  # 32-bit Windows

    # You need to `sudo apt install` these packages:
    #
    #     binutils-mingw-w64-i686
    #     gcc-mingw-w64-i686
    #     mingw-w64

    CROSS_ARCH=x86
    ARCH_CFLAGS=  # intentionally left blank (?)
    CROSS_PREFIX=i686-w64-mingw32-  # trailing hyphen is intentional

    # You need to `sudo apt install` these packages:
    #
    #     binutils-mingw-w64-i686
    #     gcc-mingw-w64-i686
    #     mingw-w64

    # Note: libtcc.a requires --config-mingw32, or it doesn't think it's a
    # native compiler and disables functions in tccrun.c (including the
    # important `tcc_relocate()`
    #
    EXTRA_CONFIGURE_FLAGS=--config-mingw32

elif [[ $OS_ID = 0.3.40 ]]; then  # 64-bit Windows

    # You need to `sudo apt install` these packages:
    #
    #     binutils-mingw-w64-x86-64
    #     g++-mingw-w64-x86-64
    #     mingw-w64

    CROSS_ARCH=x86_64
    ARCH_CFLAGS=  # intentionally left blank (?)
    CROSS_PREFIX=x86_64-w64-mingw32- # trailing hyphen is intentional

    # Note on why this is `--config-mingw32`: If the 64-bit tries to use
    # `--config-mingw64`, then it also doesn't define `TCC_IS_NATIVE` on
    # windows, because it seems `TCC_TARGET_PE` (Program Executable, e.g.
    # Windows) is only set by `--config-mingw32`.  Review.
    #
    EXTRA_CONFIGURE_FLAGS=--config-mingw32

elif [[ $OS_ID = 0.13.2 ]]; then  #arm-android5

    # You need to `sudo apt install gcc-multilib` for ARM cross-compiling:

    CROSS_ARCH=arm  # !!! can we say armv7-a here instead of -march?
    ARCH_XFLAGS="-DANDROID -DTCC_ARM_EABI -DTCC_ARM_VFP -DTCC_ARM_HARDFLOAT"

    # In June 2020, a dependency was added to fetch_and_add_arm.S that used
    # the `ldrex` and `strex` instructions, which are only available on ARMv7.
    # (see commit b2d351e0ec2cb8191d0eaa6fc586eaeddab0b817).  So you cannot
    # use the default `-march=arm`
    #
    # It actually seems that `-march=armv7` won't work either...you'll get
    # the message:
    #
    # So instead we use armv7-a.  The full list and more options are here:
    #
    # https://gcc.gnu.org/onlinedocs/gcc/ARM-Options.html
    #
    # Can also pass as clfag `-march=armv7-a`
    #
    # -mtune=cortex-a8 -mfpu=neon -ftree-vectorize -ffast-math
    #
    ARCH_XFLAGS="$ARCH_XFLAGS -march=armv7-a"

    # !!! This was written before the clang upgrade.  Right now, it's the
    # android config file for rebmake that does the breakdown of this detection
    # and not a shell script.  This only works with older hardcoded gcc.
    #
    # We don't want to repeat the work, so we either need to have the compiler
    # and architecture work assumed done outside of the config or push this
    # build process itself into rebmake.
    #
    # !!! This had support for OS X at one point, but darwin-x86_64 is changed
    # to linux now, since all CI builds are Linux.  Review.
    #
    if [[ $NDK_REVISION = r13 ]]; then
        #
        # Note: substitute `linux-x86_64` -> `darwin-x86_64` here.  But main
        # issue is that TCC build files detect you are on OS X host and try
        # to use clang.  If this way old gcc branch is not switched to build
        # on MacOS then it can't be tested by the container...that may not
        # matter, as building an linking it on Linux is likely good enough if
        # the more modern version tests.  (This older build is only done to
        # measure how much dependency creep we are getting.)
        #
        GNU_TOOLS_PREFIX="${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-"

        # NDK versions prior to 18 use gcc, same directory as `ar`, `objdump`
        #
        CC=gcc
        CROSS_PREFIX="$GNU_TOOLS_PREFIX"

        SYSROOT_LINK="${ANDROID_NDK_ROOT}/platforms/android-19/arch-arm/"

        # NDK versions prior to 18 had separate copies of the headers for
        # each target, instead of controlling conditionals with #define.
        #
        SYSROOT_COMPILE="${SYSROOT_LINK}"

    else
        GNU_TOOLS_PREFIX="${ANDROID_NDK_ROOT}/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64/bin/arm-linux-androideabi-"

        # NDK 18 and later use clang, separate directory for llvm stuff
        #
        CC=clang
        CROSS_PREFIX="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/bin/armv7a-linux-androideabi29-"

        SYSROOT_LINK="${ANDROID_NDK_ROOT}/toolchains/llvm/prebuilt/linux-x86_64/sysroot/"

        # NDK 18 and later have unified headers for all Android.
        #
        SYSROOT_COMPILE="${ANDROID_NDK_ROOT}/sysroot/"
    fi

    ARCH_CFLAGS=" ${ARCH_XFLAGS} --sysroot=\"$SYSROOT_COMPILE\" "
    ARCH_LDFLAGS=" ${ARCH_XFLAGS} --sysroot=\"$SYSROOT_LINK\" "

    # In January 2020, the bounds checker was changed to use spinlocks, commit
    # 387761878500df5a5f883139ec1013bb3696dad2.  Rationale was:
    #
    #     "/* about 25% faster then semaphore. */"
    #
    # But in NDK cases this causes:
    #
    #     bcheck.c:141:8: error: unknown type name 'pthread_spinlock_t'
    #
    # Spinlocks are not part of the C standard, and for what people think is a
    # good reason...so this is probably a poor choice:
    #
    # https://stackoverflow.com/q/61526837/
    #
    # Internet suggests trying `-gnu99` or `-D_POSIX_C_SOURCE=200112L`, but it
    # doesn't seem to work for Android.  So we just disable bounds checking
    # entirely for the Android build.
    #
    EXTRA_CONFIGURE_FLAGS="--config-bcheck=no"

else

    echo "helper can't build libtcc.a for OS_ID: $OS_ID"
    exit 1

fi


#====# DEFAULT TO ASSUMING LDFLAGS AND CFLAGS ARE THE SAME #==================#

# Android has them different, as it needs to set the sysroot differently if
# version >= 18.
#
if [[ -z $ARCH_LDFLAGS ]]; then
    ARCH_LDFLAGS="${ARCH_CFLAGS}"
fi


#====# MAKE FAKE PTHREAD LIBRARY FOR ARM #====================================#

echo "MAKE FAKE PTHREAD LIBRARY FOR ARM"

# Building against the Android NDK was giving:
#
#     ld: error: cannot find -lpthread
#
# That's because Android's threads are in libc, there is no -libpthread.
#
# https://stackoverflow.com/q/57289494/
#
# It seems more Android's fault for not giving a dummy library than the
# Makefile's fault.  We make a dummy library.

if [[ $OS_ID = 0.13.2 ]]; then  #arm-android5
    rm -rf hack  # remove if already exists
    mkdir hack

    "${GNU_TOOLS_PREFIX}ar" cr hack/libpthread.a # hack/strtold.o  # cr -> create

    # Add the directory to the linker's library search path via -L.
    #
    ARCH_LDFLAGS=" $ARCH_LDFLAGS -L\"$(pwd)/hack\" "
fi


#====# HACK strtold() SHIM INTO DUMMY libpthread.a IF OLD GCC #===============#

# Old GCC in the NDK has this problem with TCC's usage of strtold():
#
#    libtcc.a(tccpp.o):tccpp.c:function parse_number:
#        error: undefined reference to 'strtold'
#
# This is because strtold() has been omitted from its libc, due to the reason
# of "long double == double on ARM nowadays":
#
# https://tinycc-devel.nongnu.narkive.com/MZBcPTBN/problem-compiling-tinycc-for-android-strtold
#
# We can't use the proposed workaround of `-Dstrtold-strtod` becuase tcc
# defines strtold an extern in %tcc.h with a different return type than Android
# NDK's strtod, so that makes a conflicting definition error.
#
# Solve both problems by making a libpthread.a with a single definition of
# strtold(), that just calls strtod()!
#
# Use Heredoc syntax to make the file:
#
# https://www.gnu.org/savannah-checkouts/gnu/bash/manual/bash.html#Here-Documents
#

if [[ ($OS_ID = 0.13.2) && ($NDK_REVISION = r13) ]]; then  #arm-android5

cat <<EOF >hack/strtold.c
    /* hack created by build-libtcc-helper.sh */
    #include <stdlib.h>
    #include <stdio.h>

    /* Note: If you try to use this hack in a modern clang, you'll get a
     * segmentation fault due to stack overflow...as strtod() is a macro
     * that calls strtold() or somesuch.  Only use this hack in old GCC-based
     * toolchains, e.g. NDK r13!
     */

    extern long double strtold (const char *nptr, char **endptr);

    long double strtold (const char *nptr, char **endptr) {
        return strtod(nptr, endptr);
    }
EOF
# ^-- has to be in first column, and only thing on line (no comments)

    "${CROSS_PREFIX}${CC}" `# gcc or clang` \
        -c hack/strtold.c \
        -o hack/strtold.o \
        --sysroot="${SYSROOT_COMPILE}"

    # Add the strtold() definition to the pthread library hack
    #
    "${GNU_TOOLS_PREFIX}ar" q hack/libpthread.a hack/strtold.o  # q -> append
fi


#====# USE CONFIGURE TO SET UP BUILD #========================================#

# The `configure` script is a handwritten TCC configure tool; e.g. it does not
# seem to be produced from a `configure.ac` script as per autotools.  It
# produces settings in `config.mak` that are used by the Makefile.

echo "USE CONFIGURE TO SET UP BUILD"

./configure \
    --cpu=$CROSS_ARCH `# plain arm lacks ldrex/strex instructions` \
    --cc=${CC} `# NDK < 16 only has gcc, NDK >= 18 only has clang` \
    --cross-prefix="$CROSS_PREFIX" `# applied to cc, ar overridden in make` \
    --enable-static \
    --extra-cflags="$ARCH_CFLAGS" \
    --extra-ldflags="$ARCH_LDFLAGS" \
    --config-predefs=$CONFIG_PREDEFS `# can't use with cross-compilation` \
    $EXTRA_CONFIGURE_FLAGS


#====# REMOVE CONFIG_OSX FROM config.mak IF NECESSARY #=======================#

# If you are compiling on OS X, then cross-compilation is ruled out, because the
# configure script uses `uname` to detect Darwin and then forces the CONFIG_OSX
# flag to be set.  There are no options for undefining things as a parameter
# to the `make` commaned.
#
# https://stackoverflow.com/q/12215975/
#
# Our hands are tied, so remove that flag from the product of ./configure by
# using a sed command:
#
# https://stackoverflow.com/a/5410784/
#
# There's also a TARGETOS=Darwin set, but that variable isn't heeded by the
# makefile (otherwise TARGETOS=Linux would be a problem).

echo "REMOVE CONFIG_OSX FROM config.mak IF NECESSARY"

sed '/CONFIG_OSX/d' ./config.mak > ./config.filtered
mv config.filtered config.mak


#====# GENERATE libtcc.a (provides compilation API) #=========================#

echo "GENERATE libtcc.a (provides compilation API)"

# Passing the AR into ./configure doesn't seem to work, first suggestion here:
#
# https://stackoverflow.com/questions/47074736/autoconf-uses-wrong-ar-on-os-x
#
# Trying to use second suggested alternative of passing to make itself.
#
make AR="${GNU_TOOLS_PREFIX}ar" libtcc.a


#====# ENSURE strtold() EXPORTED FROM libtcc.a IF OLD GCC #===================#

echo "ENSURE strtold() EXPORTED FROM libtcc.a IF OLD GCC"

# Although we put the strtold.o into the libpthread.a to get the libtcc.a file
# to be produced without error, it seems to be missing when we try to link
# the library into the r3 executable.  Force strtold into the archive to try
# and make it available to clients of libtcc.a since Android doesn't have it.
#
# New clang builds do not require this!
#
if [[ ($OS_ID = 0.13.2) && ($NDK_REVISION = r13) ]]; then  #arm-android5
    "${GNU_TOOLS_PREFIX}ar" r libtcc.a hack/strtold.o  # r -> replace
fi


#====# GENERATE libtcc1.a (runtime helpers for TCC-built code) #==============#

echo "GENERATE libtcc1.a (runtime helpers for TCC-built code)"

# !!! A note here said "could fail to build tcc due to lack of '-ldl' on
# Windows" and did `make ... || echo "ignoring error"`  Why?
#
# !!! We only compile and archive here, we don't link...so we don't need to
# know the linker's sysroot, and can use ARCH_CFLAGS without passing in
# ARCH_LDFLAGS.  (There's nowhere to pass them.)
#
make libtcc1.a \
    XCC="${CROSS_PREFIX}${CC}" \
    XAR="${GNU_TOOLS_PREFIX}ar" \
    XFLAGS="$ARCH_CFLAGS"

# This deletes armeabi.o from libtcc1.a (if it's there, otherwise no-op).
# The idea is to prevent conflicts when linking both libtcc1.a and libtcc.a.
#
# !!! Exactly what this was added for isn't clear (there were no comments).
# But it may be helpful if you want to link libtcc1.a and libtcc.a into the
# same executable, e.g. when an r3-with-tcc for ARM tries to compile and link
# another r3 with tcc extension in a second phase of bootstrap.
#
"${GNU_TOOLS_PREFIX}ar" d libtcc1.a armeabi.o   # d


#====# EXAMINE BUILD PRODUCT #================================================#

echo "EXAMINE BUILD PRODUCT"
objdump -a libtcc.a


#====# RESTORE INITIAL DIRECTORY #============================================#

# Rethink convention on how caller finds build products.
#
# Currently it assumes they're in the `tcc` directory of wherever they called
# the script from.

cd ..
