Rebol [
    name: Network
    notes: "See %extensions/README.md for the format and fields of this file"
]

use-librebol: 'no

includes: [
    ; Actual includes are in a subdirectory, e.g.
    ;
    ;     #include "uv/xxx.h" => %libuv/include/uv/xxx.h
    ;
    %../filesystem/libuv/include/

    %../filesystem/libuv/src/  ; e.g. queue.h
]

sources: [
    %mod-network.c

    ; If you `#include "uv.h"` and try to build as C++ with warnings up you
    ; will get warning 5220.
    ;
    <msc:/wd5220>
]

depends: []
