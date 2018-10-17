## NOTES ON CREATING THE PREBUILT LIBRARIES ##

Start with a fresh older image of the linux you want to use.  These were made
with Ubuntu 14.04.1, because that's what the Travis build uses that runs the
old MinGW cross-compiler.

First, install the cross-compiler.  It can live on the same system that has
ordinary gcc; the executables have funny names to distinguish them:

    sudo apt-get install binutils-mingw-w64-i686
    sudo apt-get install gcc-mingw-w64-i686
    sudo apt-get install mingw-w64

Add git if you don't already have it:

    sudo apt-get install git

Then clone ZeroMQ into a directory (this creates `libzmq/`).

    git clone https://github.com/zeromq/libzmq.git

You will need to edit the ZeroMQ %src/windows.hpp file to add a MinGW-specific
include to provide SOCKADDR_IN6 (which modern winsock.h in Visual Studio has
already defined by default).  So look for:

    #include <winsock.h>
    #include <winsock2.h>
    #include <windows.h>
    #include <mswsock.h>
    #include <iphlpapi.h>

And change it to:

    #include <winsock.h>
    #include <winsock2.h>
    #include <ws2ipdef.h> // https://github.com/pocoproject/poco/issues/1603
    #include <windows.h>
    #include <mswsock.h>
    #include <iphlpapi.h>

The ZeroMQ build process requires several things just to get the `configure`
shell script generated:

    sudo apt-get install build-essential
    sudo apt-get install autotools
    sudo apt-get install libtool
    sudo apt-get install pkg-config

First you have to "autogen", running the shell script to create what automake
will use:

    ./autogen.sh

Then you need to run automake itself:

    automake

Now configure.  The `--host` option to configure should be enough to get it to
find the cross-compiling MinGW that was installed.  If you want a 32-bit build
then use the "i686" variant:

    ./configure --host=i686-w64-mingw32 --enable-static --disable-shared

For 64-bit builds use "x86_64" (a.k.a. amd64):

    ./configure --host=x86_64-w64-mingw32 --enable-static --disable-shared

That should give you a `Makefile`.  So, now build it.

    make

By default ZeroMQ puts the obj build products into the %src/ directory, then
sneaks the library itself (%.a file) into the hidden %src/.lib directory.

The file is way large, but you cannot simply `strip` a static lib and have it
still work.  You can only strip the unneeded information, so:

    strip --strip-unneeded src/libzmq.a


## COMPILING WITH THE PREBUILT LIBRARIES ##

One thing to note is that ZeroMQ is built with C++, something the author
later regretted:

http://250bpm.com/blog:4

There's no obvious instructions for how to get a static library in a cross
compiled mingw `.a` file to embed the standard C++ library and libpthread
dependencies, such that a plain C program can easily link to it.  While it's
possible, for now the easiest thing to do is to just use the C++ compiler to
build and link Rebol and pass the static linking switches:

       -static-libgcc // static C runtime
       -Wl,-Bstatic -lstdc++ // static C++ runtime

The rebmake-based build process still needs a lot of work to make it possible
for the various extensions to weave their dependencies together to build a
coherent executable.  To be fair, it's not very elegant in *any* build system.

For now an ad-hoc build was created with something like the following:

    ./r3-linux-x64-368bd5a make.r \
        config: configs/mingw-x64-c++.r \
        extensions: "ZeroMQ + Clipboard + ODBC + Debugger - GIF - View -" \
        standard: c++11 \
        cflags: "[{-DZMQ_STATIC}]" \
        ldflags: "[{-L../external/zmq-prebuilt/mingw64/lib/} \
            {-Wl,-Bstatic -lstdc++ -static-libgcc}]" \
        debug: asserts \
        optimize: 2 \
        target: makefile

The makefile needed a few tweaks.  The `-lwsock32` had to be changed to
`-lws2_32` to get some new Winsock functionality, and that also had to be
reordered so that it came after the `-lzmq` inclusion.  Another file that has
to come after that is `-liphlpapi`.


## NEXT STEP: CONTINUOUS INTEGRATION ##

This was a nuisance, but the next step after this ad-hoc build is to use the
prebuilt libs to get a continuously-integrated ZeroMQ cross-compiled on Travis
linux servers to Windows.  That would be a good time to review lots of issues,
like what kind of overhaul of the build system is needed to support this kind
of complexity for an extension.

For now, the one-off build is a proof of concept that Visual Studio is not
necessary to make a Windows executable including ZeroMQ.
