REBOL []

name: 'Filesystem
source: %filesystem/mod-filesystem.c

os: system-config/os-base

definitions: []
libraries: []

uv-sources: []

uv-nowarn: ~

; Start from a base of files that are common for either Windows or UNIXes
;
if os = 'Windows [
    append definitions [
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0602
    ]
    append uv-sources[
        ;
        ; files in %src/windows/
        ;
        async.c
        core.c
        detect-wakeup.c
        dl.c
        error.c
        fs.c
        fs-event.c
        getaddrinfo.c
        getnameinfo.c
        handle.c
        loop-watcher.c
        pipe.c
        thread.c
        poll.c
        process.c
        process-stdio.c
        signal.c
        snprintf.c
        stream.c
        tcp.c
        tty.c
        udp.c
        util.c
        winapi.c
        winsock.c
    ]
    append libraries [
        ;
        ; Already included
        ;
        ;   %user32
        ;   %advapi32

        ; GetProcessMemoryInfo()
        ;
        %psapi

        ; ConvertInterfaceLuidToNameW()
        ; ConvertInterfaceIndexToLuid()
        ;
        %iphlpapi

        ; GetUserProfileDirectoryW()
        ;
        %userenv

        ; These APIs are part of WinSock2, and one of the big differences
        ; is that they are *asynchronous* instead of merely *non-blocking*.
        ; e.g. you can ask to send() and it will report a partial result
        ; or tell you the socket is busy, but WSASend() can actually take
        ; the data and notify when the transfer is over.
        ;
        ; WSASocketW()
        ; WSAIoctl()
        ; WSADuplicateSocketW()
        ; WSAGetOverlappedResult()
        ; WSARecv()
        ; WSASend()
        ; WSARecvFrom()
        ; WSASendTo()
        ; GetHostNameW()
        ; GetAdaptersAddresses()
        ;
        %ws2_32
    ]
    uv-nowarn: [
        ;
        ; These were in the CMakeLists.txt for libuv
        ;
        <msc:/wd4100>  ; NO_UNUSED_PARAMETER
        <msc:/wd4127>  ; NO_CONDITIONAL_CONSTANT
        <msc:/wd4201>  ; NO_NONSTANDARD_MSVC
        <msc:/wd4206>  ; NO_NONSTANDARD_EMPTY_TU
        <msc:/wd4210>  ; NO_NONSTANDARD_FILE_SCOPE
        <msc:/wd4232>  ; NO_NONSTANDARD_NONSTATIC_DLIMPORT
        <msc:/wd4456>  ; NO_HIDES_LOCAL
        <msc:/wd4457>  ; NO_HIDES_PARAM
        <msc:/wd4459>  ; NO_HIDES_GLOBAL
        <msc:/wd4706>  ; NO_CONDITIONAL_ASSIGNMENT
        <msc:/wd4996>  ; NO_UNSAFE_MSVC

        ; These were not in the CMakeLists.txt for libuv
        ;
        <msc:/wd4464>  ; relative include `#include "../uv-common.h"`
        <msc:/wd4146>
        <msc:/wd4701>  ; false positive on `user_timeout`
        <msc:/wd4777>  ; _snwprintf() int/DWORD complaint
        <msc:/wd4702>  ; unreachable code
        <msc:/wd4777>  ; _snwprintf() int/DWORD complaint
        <msc:/wd4189>  ; `r` local variable initialized but not referenced
    ]
]
else [
    append definitions [
        _FILE_OFFSET_BITS=64
        _LARGEFILE_SOURCE
    ]
    append uv-sources [
        ;
        ; files in %src/unix/
        ;
        async.c
        core.c
        dl.c
        fs.c
        getaddrinfo.c
        getnameinfo.c
        loop-watcher.c
        loop.c
        pipe.c
        poll.c
        process.c
        random-devurandom.c
        signal.c
        stream.c
        tcp.c
        thread.c
        tty.c
        udp.c
    ]
    if not find [Android OS390 QNX] os [
        append libraries [
            %pthread  ; Android has pthread in its C library
        ]
    ]
    uv-nowarn: [
        <no-unused-parameter>
        <gnu:-Wno-redundant-decls>  ; `environ`, `uv__static_assert`
        <gnu:-Wno-logical-op>  ; logical OR of equal expressions
    ]
]

if os = 'AIX [
    append definitions [
       _ALL_SOURCE
       _LINUX_SOURCE_COMPAT
       _THREAD_SAFE
       _XOPEN_SOURCE=500
       HAVE_SYS_AHAFS_EVPRODS_H
    ]
    append uv-sources [
        aix.c
        aix-common.c
    ]
    append libraries [
        %perfstat
    ]
]

if os = 'Android [
    append definitions [
        _GNU_SOURCE
    ]
    append uv-sources [
       android-ifaddrs.c
       linux-core.c
       linux-inotify.c
       linux-syscalls.c
       procfs-exepath.c
       pthread-fixes.c
       random-getentropy.c
       random-getrandom.c
       random-sysctl-linux.c
       epoll.c
    ]
    append libraries [
        %dl
    ]
]

if find [OSX Android Linux] os [
    append uv-sources [
        proctitle.c
    ]
]

if find [DragonFly FreeBSD] os [
    append uv-sources [
        freebsd.c
    ]
]

if find [DragonFly FreeBSD NetBSD OpenBSD] os [
    append uv-sources [
        posix-hrtime.c
        bsd-proctitle.c
    ]
]

if find [OSX iOS DragonFly FreeBSD NetBSD OpenBSD] os [
    append uv-sources [
        bsd-ifaddrs.c
        kqueue.c
    ]
]

if os = 'FreeBSD [
    append uv-sources [
        random-getrandom.c
    ]
]

if find [OSX OpenBSD] os [
    append uv-sources [
        random-getentropy.c
    ]
]

if os = 'OSX [
    append definitions [
        _DARWIN_UNLIMITED_SELECT=1
        _DARWIN_USE_64_BIT_INODE=1
    ]
    append uv-sources [
       darwin-proctitle.c
       darwin.c
       fsevents.c
    ]
]

if os = 'Linux [
    append definitions [
        _GNU_SOURCE
        _POSIX_C_SOURCE=200112
    ]
    append uv-sources [
        linux-core.c
        linux-inotify.c
        linux-syscalls.c
        procfs-exepath.c
        random-getrandom.c
        random-sysctl-linux.c
        epoll.c
    ]
    append libraries [
        %dl
        %rt
    ]
]

if os = 'NetBSD [
    append libraries [%kvm]
    append uv-sources [
        netbsd.c
    ]
]

if os = 'OpenBSD [
    append uv-sources [
        openbsd.c
    ]
]

if os = 'Sun [
    append definitions [__EXTENSIONS__ _XOPEN_SOURCE=500 _REENTRANT]
    append uv-sources [
       no-proctitle.c
       sunos.c
    ]
    append libraries [
        %kstat
        %nsl
        %sendfile
        %socket
    ]
]

if os = 'Haiku [
    append definitions [_BSD_SOURCE]
    append uv-sources [
        haiku.c
        bsd-ifaddrs.c
        no-fsevents.c
        no-proctitle.c
        posix-hrtime.c
        posix-poll.c
    ]
    append libraries [
        %bsd
        %network
    ]
]

if os = 'QNX [
    append uv-sources [
        posix-hrtime.c
        posix-poll.c
        qnx.c
        bsd-ifaddrs.c
        no-proctitle.c
        no-fsevents.c
    ]
    append libraries [
        %socket
    ]
]


=== TRANSFORM PATHS FOR SOURCES TO FULL PATHS AND DISABLE WARNINGS ===

uv-depends: map-each tuple uv-sources [  ; WORD! in bootstrap
    file: if os = 'Windows [
        join %filesystem/libuv/src/win/ to file! tuple
    ] else [
        join %filesystem/libuv/src/unix/ to file! tuple
    ]
    compose [(file) #no-c++ ((uv-nowarn))]
]

append uv-depends map-each tuple [  ; WORD! in bootstrap
    fs-poll.c
    idna.c
    inet.c
    random.c
    strscpy.c
    threadpool.c
    timer.c
    uv-common.c
    uv-data-getter-setters.c
    version.c
][
    compose [(join %filesystem/libuv/src/ to file! tuple) #no-c++ ((uv-nowarn))]
]



includes: reduce [
    %prep/extensions/filesystem

    make-file [(repo-dir) extensions/filesystem/libuv/src /]  ; e.g. queue.h
    make-file [(repo-dir) extensions/filesystem/libuv/include /]  ; sub %uv/
]


depends: compose [
    ;
    ; If you `#include "uv.h"` and try to build as C++ with warnings up you
    ; will get warning 5220.
    ;
    [%filesystem/p-file.c <msc:/wd5220>]
    [%filesystem/p-dir.c <msc:/wd5220>]
    [%filesystem/file-posix.c <msc:/wd5220>]

    ((uv-depends))

    ((if "1" = get-env "USE_BACKDATED_GLIBC" [
        [%filesystem/fcntl-patch.c]
    ]))
]

ldflags: compose [
    ((if "1" = get-env "USE_BACKDATED_GLIBC" [
        {-Wl,--wrap=fcntl64 -Wl,--wrap=log -Wl,--wrap=pow}
    ]))
]
