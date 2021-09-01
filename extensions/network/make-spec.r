REBOL []

name: 'Network
source: [
    ;
    ; If you `#include "uv.h"` and try to build as C++ with warnings up you
    ; will get warning 5220.
    ;
    %network/mod-network.c <msc:/wd5220>
]

includes: reduce [
    %prep/extensions/network

    join repo-dir %extensions/filesystem/libuv/src/  ; e.g. queue.h
    join repo-dir %extensions/filesystem/libuv/include/  ; sub %uv/
]

depends: []
