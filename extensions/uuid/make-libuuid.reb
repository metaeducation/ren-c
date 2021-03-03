REBOL [
    Title: {Extract}
    File: %make-libuuid.reb

    Description: {
        The Linux Kernel organization has something called `util-linux`, which
        is a standard package implementing various functionality:

        https://en.wikipedia.org/wiki/Util-linux

        This script is designed to extract just the files that relate to UUID
        generation and handling, to be built into the Rebol executable.  The
        files are read directly from GitHub, and tweaked to build without
        warnings uunder the more rigorous settings used in compilation, which
        includes compiling as C++.

        The extracted files are committed into the Ren-C repository, to reduce
        the number of external dependencies in the build.
    }
]

ROOT: https://raw.githubusercontent.com/karelzak/util-linux/master/

mkdir %libuuid

pass: func [x][x]

add-config.h: [
    to "/*" thru "*/"
    thru "^/"
    insert {^/#include "config.h"^/}
]
space: charset " ^-^/^M"

;comment out unneeded headers
comment-out-includes: [
    pos: {#include}
    [
        [
            some space [
                exclude-headers
            ] (insert pos {//} pos: skip pos 2)
            | skip
        ] (pos: skip pos 8)
    ] seek pos
]


fix-randutils.c: func [
    text [text!]
][
    exclude-headers: [
        {"c.h"}
    ]

    parse text [
        add-config.h
        insert {^/#include <errno.h>^/}

        any [
            comment-out-includes

            ; randutils.c:137:12: error:
            ; invalid conversion from ‘void*’ to ‘unsigned char*’
            ;
            | change {cp = buf} {cp = (unsigned char*)buf}

            ; Fix "error: invalid suffix on literal;
            ; C++11 requires a space between literal and identifier"
            ;
            | change {"PRIu64"} {" PRIu64 "}

            | skip
        ]

        end
    ]

    return text
]

fix-gen_uuid.c: function [
    text [text!]
    <with>
    exclude-headers
    comment-out-includes
    add-config.h
    space
][

    exclude-headers: [
        {"all-io.h"}
        | {"c.h"}
        | {"strutils.h"}
        | {"md5.h"}
        | {"sha1.h"}
    ]

    parse text [
        add-config.h

        any [
            ;comment out unneeded headers
            comment-out-includes

            ; avoid "unused node_id" warning
            | {get_node_id} thru #"^{" thru "^/" insert {^/^-(void)node_id;^/}

            ; comment out uuid_generate_md5, we don't need this
            | change [
                copy definition: [
                    {void uuid_generate_md5(} thru "^}"
                  ]
                  (target: unspaced [{#if 0^/} to text! definition {^/#endif^/}])
                ]
                target

            ; comment out uuid_generate_sha1, we don't need this
            | change [
                copy definition: [
                    {void uuid_generate_sha1(} thru "^}"
                  ]
                  (target: unspaced [{#if 0^/} to text! definition {^/#endif^/}])
                ]
                target

            ; comment out unused variable variant_bits
            | change [
                copy unused: [
                    {static unsigned char variant_bits[]}
                  ]
                  (target: unspaced [{//} _ to text! unused])
                ] target

            | skip
        ]

        end
    ]

    return text
]

files: compose [
    %include/nls.h              _
    %include/randutils.h        _
    %lib/randutils.c            (:fix-randutils.c)
    %libuuid/src/gen_uuid.c     (:fix-gen_uuid.c)
    %libuuid/src/pack.c         _
    %libuuid/src/unpack.c       _
    %libuuid/src/uuidd.h        _
    %libuuid/src/uuid.h         _
    %libuuid/src/uuidP.h        _
]

for-each [file fix] files [
    data: to-text read url: join ROOT file
    target: join %libuuid/ (last split-path file)

    print [
        url LF
        "->" target LF
    ]

    if :fix [data: fix data]  ; correct compiler warnings

    replace/all data tab {    }  ; spaces not tabs

    write target data
]
