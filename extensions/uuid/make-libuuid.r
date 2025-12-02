Rebol [
    title: "Extract libUUID"
    file: %make-libuuid.r

    description: --[
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
    ]--
]

ROOT: https://raw.githubusercontent.com/karelzak/util-linux/master/

mkdir %libuuid

[.]: {
    exclude-headers: ~  ; set differently in each routine
}

add-config-h: [
    to "/*" thru "*/"
    thru "^/"
    insert -[^/#include "config.h"^/]-
]
space: charset " ^-^/^M"

;comment out unneeded headers
comment-out-includes: [
    pos: "#include"
    [
        [
            some space [
                .exclude-headers
            ] (insert pos -[//]- pos: skip pos 2)
            | one
        ] (pos: skip pos 8)
    ] seek pos
]


fix-randutils-c: func [
    text [text!]
][
    .exclude-headers: [
        -["c.h"]-
    ]

    parse3 text [
        add-config-h
        insert -[^/#include <errno.h>^/]-

        opt some [
            comment-out-includes

            ; randutils.c:137:12: error:
            ; invalid conversion from ‘void*’ to ‘unsigned char*’
            ;
            | change -[cp = buf]- -[cp = (unsigned char*)buf]-

            ; Fix "error: invalid suffix on literal;
            ; C++11 requires a space between literal and identifier"
            ;
            | change -["PRIu64"]- -[" PRIu64 "]-

            | one
        ]

        <end>
    ]

    return text
]

fix-gen_uuid-c: func [
    text [text!]
][
    .exclude-headers: [
        -["all-io.h"]-
        | -["c.h"]-
        | -["strutils.h"]-
        | -["md5.h"]-
        | -["sha1.h"]-
    ]

    let definition
    let target
    let unused

    parse3 text [
        add-config-h

        opt some [
            ;comment out unneeded headers
            comment-out-includes

            ; avoid "unused node_id" warning
            | "get_node_id" thru #"^{" thru "^/" insert "^/^-(void)node_id;^/"

            ; comment out uuid_generate_md5, we don't need this
            | change [
                definition: across [
                    -[void uuid_generate_md5(]- thru "^}"
                  ]
                  (target: unspaced [
                      -[#if 0^/]- to text! definition -[^/#endif^/]-
                  ])
                ]
                target

            ; comment out uuid_generate_sha1, we don't need this
            | change [
                definition: across [
                    -[void uuid_generate_sha1(]- thru "^}"
                  ]
                  (target: unspaced [
                      -[#if 0^/]- to text! definition -[^/#endif^/]-
                  ])
                ]
                target

            ; comment out unused variable variant_bits
            | change [
                unused: across [
                    "static unsigned char variant_bits[]"
                  ]
                  (target: unspaced [-[//]- _ to text! unused])
                ] target

            | one
        ]

        <end>
    ]

    return text
]

files: compose [
    %include/nls.h              _
    %include/randutils.h        _
    %lib/randutils.c            (unrun fix-randutils-c/)
    %libuuid/src/gen_uuid.c     (unrun fix-gen_uuid-c/)
    %libuuid/src/pack.c         _
    %libuuid/src/unpack.c       _
    %libuuid/src/uuidd.h        _
    %libuuid/src/uuid.h         _
    %libuuid/src/uuidP.h        _
]

for-each [file fix] files [
    data: read:string url: join ROOT file
    split-path3:file file $filename
    target: join %libuuid/ filename

    print [
        url LF
        "->" target LF
    ]

    if not space? fix [data: run fix data]  ; correct compiler warnings

    replace data tab --[    ]--  ; spaces not tabs

    write target data
]
