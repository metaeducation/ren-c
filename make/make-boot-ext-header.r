REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate extention native header files"
    File: %make-boot-ext-header.r;-- used by EMIT-HEADER to indicate emitting script
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

do %r2r3-future.r
do %common.r
do %common-emitter.r

r3: system/version > 2.100.0

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %prep]
mkdir/deep output-dir/include

extensions: either any-string? :args/EXTENSIONS [split args/EXTENSIONS #","][[]]

e-ext: (make-emitter
    "Boot Modules" output-dir/include/tmp-boot-extensions.h)

remove-each ext extensions [empty? ext] ;SPLIT in r3-a111 gives an empty "" at the end
either empty? extensions [
    e-ext/emit-lines [
        "#define LOAD_BOOT_EXTENSIONS(ext)"
        "#define SHUTDOWN_BOOT_EXTENSIONS()"
    ]
] [
    for-each ext extensions [
        e-ext/emit-line ["DECLARE_EXT_INIT(" ext ");"]
        e-ext/emit-line ["DECLARE_EXT_QUIT(" ext ");"]
    ]

    e-ext/emit-line []
    e-ext/emit-line ["static CFUNC *Boot_Extensions [] = {"]
    for-each ext extensions [
        e-ext/emit-line/indent ["cast(CFUNC *, EXT_INIT(" ext ")),"]
        e-ext/emit-line/indent ["cast(CFUNC *, EXT_QUIT(" ext ")),"]
    ]
    e-ext/emit-end

    e-ext/emit-line []

    e-ext/emit-lines [
        "#define LOAD_BOOT_EXTENSIONS(ext) do {\"
        "Prepare_Boot_Extensions(ext, Boot_Extensions, sizeof(Boot_Extensions)/sizeof(CFUNC *));\"
        "} while (0)"
    ]

    e-ext/emit-lines [
        "#define SHUTDOWN_BOOT_EXTENSIONS() do {\"
        "Shutdown_Boot_Extensions(Boot_Extensions, sizeof(Boot_Extensions)/sizeof(CFUNC *));\"
        "} while (0)"
    ]
]

e-ext/write-emitted
