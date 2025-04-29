Rebol [
    name: Filesystem
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'no

definitions: []  ; added to by code below

includes: [
    %libuv/include/  ; sub %uv/
    %libuv/src/  ; e.g. queue.h
]

; If you `#include "uv.h"` and try to build as C++ with warnings up you
; will get warning 5220.
;
sources: [
    [%mod-filesystem.c <msc:/wd5220>]
    [%p-file.c <msc:/wd5220>]
    [%p-dir.c <msc:/wd5220>]
    [%file-posix.c <msc:/wd5220>]
]

; !!! Note: There are nuances between os-base, os-name, and platform's name
; The most relevant choice here is platform's name, e.g. the `Windows: 3` you
; would see in %platforms.r
;
os: platform-config.name

uv-sources: []
uv-libraries: []
uv-nowarn: ~

; Start from a base of files that are common for either Windows or UNIXes
;
if os = 'Windows [
    append definitions spread [
        WIN32_LEAN_AND_MEAN
        _WIN32_WINNT=0x0602
    ]
    append uv-sources spread [
        ;
        ; files in %src/windows/
        ;
        ; !!! Can't be `foo.c` as TUPLE! because bootstrap loads as `foo/c`
        %async.c
        %core.c
        %detect-wakeup.c
        %dl.c
        %error.c
        %fs.c
        %fs-event.c
        %getaddrinfo.c
        %getnameinfo.c
        %handle.c
        %loop-watcher.c
        %pipe.c
        %thread.c
        %poll.c
        %process.c
        %process-stdio.c
        %signal.c
        %snprintf.c
        %stream.c
        %tcp.c
        %tty.c
        %udp.c
        %util.c
        %winapi.c
        %winsock.c
    ]
    append uv-libraries spread [
        ;
        ; These include bases like GetMessage(), and are already included by
        ; the Event extension, but needed if you don't build with that.
        ;
        %user32
        %advapi32

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

    ; 1. Without disabling this, you likely get:
    ;
    ;      '_WIN32_WINNT_WIN10_TH2' is not defined as a preprocessor
    ;      macro, replacing with '0' for '#if/#elif'
    ;
    ;    Seems to be some mistake on Microsoft's part, that some report can be
    ;    remedied by using WIN32_LEAN_AND_MEAN:
    ;
    ;      https://stackoverflow.com/q/11040133/
    ;
    ;    But if you include <winioctl.h>, you still have it.
    ;
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

        <msc:/analyze->  ; do not do static analysis

        <msc:/wd4668>  ; Microsoft's own header files, as usual, are bad [1]
    ]
]
else [
    append definitions spread [
        _FILE_OFFSET_BITS=64
        _LARGEFILE_SOURCE
    ]
    append uv-sources spread [
        ;
        ; files in %src/unix/
        ;
        %async.c
        %core.c
        %dl.c
        [%fs.c <gcc:-Wno-unused-variable>]  ; "r" unused in Haiku builds
        %getaddrinfo.c
        %getnameinfo.c
        %loop-watcher.c
        %loop.c
        %pipe.c
        %poll.c
        %process.c
        %random-devurandom.c
        %signal.c
        %stream.c
        %tcp.c
        %thread.c
        %tty.c
        %udp.c
    ]
    if not find [Android OS390 QNX] os [
        append uv-libraries spread [
            %pthread  ; Android has pthread in its C library
        ]
    ]
    uv-nowarn: [
        <no-unused-parameter>
        <gcc:-Wno-redundant-decls>  ; `environ`, `uv__static_assert`
        <gnu:-Wno-logical-op>  ; logical OR of equal expressions
        <gcc:-Wno-pedantic>  ; casts object pointers to functions
        <gnu:-Wno-dangling-pointer>  ; queue in %async.c
    ]
]

if os = 'AIX [
    append definitions spread [
       _ALL_SOURCE
       _LINUX_SOURCE_COMPAT
       _THREAD_SAFE
       _XOPEN_SOURCE=500
       HAVE_SYS_AHAFS_EVPRODS_H
    ]
    append uv-sources spread [
        aix.c
        aix-common.c
    ]
    append uv-libraries spread [
        %perfstat
    ]
]

if os = 'Android [
    append definitions spread [
        _GNU_SOURCE
    ]
    append uv-sources spread [
       ; Note: android-ifaddrs.c was removed
       %linux-core.c
       %linux-inotify.c
       %linux-syscalls.c
       %procfs-exepath.c
       %pthread-fixes.c
       %random-getentropy.c
       %random-getrandom.c
       %random-sysctl-linux.c
       %epoll.c
    ]
    append uv-libraries spread [
        %dl
    ]
]

if find [Macintosh Android Linux] os [
    append uv-sources spread [
        %proctitle.c
    ]
]

if find [DragonFly FreeBSD] os [
    append uv-sources spread [
        %freebsd.c
    ]
]

if find [DragonFly FreeBSD NetBSD OpenBSD] os [
    append uv-sources spread [
        %posix-hrtime.c
        %bsd-proctitle.c
    ]
]

if find [Macintosh iOS DragonFly FreeBSD NetBSD OpenBSD] os [
    append uv-sources spread [
        %bsd-ifaddrs.c
        %kqueue.c
    ]
]

if os = 'FreeBSD [
    append uv-sources spread [
        %random-getrandom.c
    ]
]

if find [Macintosh OpenBSD] os [
    append uv-sources spread [
        %random-getentropy.c
    ]
]

if os = 'Macintosh [
    append definitions spread [
        _DARWIN_UNLIMITED_SELECT=1
        _DARWIN_USE_64_BIT_INODE=1
    ]
    append uv-sources spread [
       %darwin-proctitle.c
       %darwin.c
       %fsevents.c
    ]
]

if os = 'Linux [
    append definitions spread [
        _GNU_SOURCE
        _POSIX_C_SOURCE=200112
    ]
    append uv-sources spread [
        %linux-core.c
        %linux-inotify.c
        %linux-syscalls.c
        %procfs-exepath.c
        %random-getrandom.c
        %random-sysctl-linux.c
        %epoll.c
    ]
    append uv-libraries spread [
        %dl
        %rt
    ]
]

if os = 'NetBSD [
    append uv-libraries spread [%kvm]
    append uv-sources spread [
        %netbsd.c
    ]
]

if os = 'OpenBSD [
    append uv-sources spread [
        %openbsd.c
    ]
]

if os = 'Sun [
    append definitions spread [__EXTENSIONS__ _XOPEN_SOURCE=500 _REENTRANT]
    append uv-sources spread [
        %no-proctitle.c
        %sunos.c
    ]
    append uv-libraries spread [
        %kstat
        %nsl
        %sendfile
        %socket
    ]
]

if os = 'Haiku [
    append definitions spread [_BSD_SOURCE]
    append uv-sources spread [
        %haiku.c
        %bsd-ifaddrs.c
        %no-fsevents.c
        %no-proctitle.c
        %posix-hrtime.c
        %posix-poll.c
    ]
    append uv-libraries spread [
        %bsd
        %network
    ]
]

if os = 'QNX [
    append uv-sources spread [
        %posix-hrtime.c
        %posix-poll.c
        %qnx.c
        %bsd-ifaddrs.c
        %no-proctitle.c
        %no-fsevents.c
    ]
    append uv-libraries spread [
        %socket
    ]
]


=== "TRANSFORM PATHS FOR SOURCES TO FULL PATHS AND DISABLE WARNINGS" ===

uv-depends: map-each 'item uv-sources [  ; WORD! in bootstrap
    let file: ensure file! either block? item [item.1] [item]
    let flags: either block? item [next item] [null]
    file: if os = 'Windows [
        join %libuv/src/win/ file
    ] else [
        join %libuv/src/unix/ file
    ]
    compose [(file) #no-c++ (spread uv-nowarn) (maybe spread flags)]
]

append uv-depends spread map-each 'tuple [  ; WORD! in bootstrap
    %fs-poll.c
    %idna.c
    [
        %inet.c
        <clang:-Wno-deprecated-declarations>  ; uses sprintf()
    ]
    %random.c
    %strscpy.c
    %strtok.c  ; Note: seems only used in unix build (?)
    %threadpool.c
    %timer.c
    %uv-common.c
    %uv-data-getter-setters.c
    %version.c
][
    let file: if block? tuple [first tuple] else [tuple]
    let flags: if block? tuple [next tuple] else [[]]
    compose [
        (join %libuv/src/ file) #no-c++ (spread uv-nowarn) (spread flags)
    ]
]

depends: compose [
    (spread uv-depends)

    (if "1" = get-env "USE_BACKDATED_GLIBC" [
        spread [
            [%fcntl-patch.c]
        ]
    ])
]

ldflags: compose [
    (if "1" = get-env "USE_BACKDATED_GLIBC" [
        "-Wl,--wrap=fcntl64 -Wl,--wrap=log -Wl,--wrap=pow"
    ])
]

libraries: uv-libraries
