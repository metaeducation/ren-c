REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make Reb-Lib related files"
    File: %make-reb-lib.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
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
do %common-parsers.r
do %common-emitter.r

print "--- Make Reb-Lib Headers ---"

lib-ver: 2

preface: "RL_"

args: parse-args system/options/args
output-dir: fix-win32-path to file! any [:args/OUTDIR %prep/]
output-dir: output-dir/include
mkdir/deep output-dir

ver: load %../src/boot/version.r

;-----------------------------------------------------------------------------

; These are the blocks of strings that are gathered in the EMIT-PROTO scan of
; %a-lib.c.  They are later composed along with some boilerplate to produce
; the %reb-lib.h file.
;
lib-struct-fields: make block! 50
struct-call-macros: make block! 50
undecorated-prototypes: make block! 50
direct-call-macros: make block! 50
table-init-items: make block! 50

emit-proto: proc [proto] [
    header: proto-parser/data

    if not all [
        block? header
        2 <= length-of header
        set-word? header/1
    ][
        print mold header
        fail [
            proto
            newline
            "Prototype has bad Rebol function header block in comment"
        ]
    ]

    if header/2 != 'RL_API [
        leave
    ]

    ; Currently the only part of the comment header for the exports in
    ; the %a-lib.c file that is paid attention to is the SET-WORD! that
    ; mirrors the name of the function, and the RL_API word that comes
    ; after it.  Anything else should just be comments.  But some day,
    ; it could be a means of exposing documentation for the parameters.
    ;
    ; (This was an original intent of the comments in the %a-lib.c file,
    ; though they parsed a specialized documentation format that did not
    ; use Rebol syntax...the new idea is to always use Rebol syntax.)

    api-name: spelling-of header/1
    unless proto-parser/proto.id = unspaced ["RL_" api-name] [
        fail [
            "Name in comment header (" api-name ") isn't function name"
            "minus RL_ prefix for" proto-parser/proto.id
        ]
    ]

    pos.id: find proto "RL_"
    fn.declarations: copy/part proto pos.id
    pos.lparen: find pos.id "("
    fn.name: copy/part pos.id pos.lparen
    fn.name.upper: uppercase copy fn.name
    fn.name.lower: lowercase copy find/tail fn.name "RL_"

    append lib-struct-fields unspaced [
        fn.declarations "(*" fn.name.lower ")" pos.lparen ";"
    ]

    append struct-call-macros unspaced [
        "#define" space api-name args space "RL->" fn.name.lower args
    ]

    append undecorated-prototypes unspaced [
        "RL_API" space proto ";"
    ]

    append direct-call-macros unspaced [
        "#define" space api-name args space proto-parser/proto.id
    ]

    append table-init-items unspaced [
        fn.name ","
    ]
]

process: func [file] [
    data: read the-file: file
    data: to-string data

    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

;-----------------------------------------------------------------------------

e-lib: (make-emitter
    "Lightweight Rebol Interface Library" output-dir/reb-lib.h)

e-lib/emit-lines [
    {#ifdef __cplusplus}
    {extern "C" ^{}
    {#endif}
    []
]

; !!! It is probably the case that the interface itself should contain the
; versioning (in early fields), so it can be checked by anyone it's passed to.
;
e-lib/emit-lines [
    {// These constants are created by the release system and can be used}
    {// to check for compatiblity with the reb-lib DLL (using RL_Version.)}
    {//}
    [{#define RL_VER } ver/1]
    [{#define RL_REV } ver/2]
    [{#define RL_UPD } ver/3]
    {}
]

;-----------------------------------------------------------------------------
;
; Currently only two files are searched for RL_API entries.  This makes it
; easier to track the order of the API routines and change them sparingly
; (such as by adding new routines to the end of the list, so as not to break
; binary compatibility with code built to the old ordered interface).

src-dir: %../src/core/

e-lib/emit-line [
    {// Function entry points for reb-lib (used for MACROS below):}
]

e-lib/emit-line "typedef struct rebol_ext_api {"

process src-dir/a-lib.c
process src-dir/f-extension.c ; !!! is there a reason to process this file?

for-each field lib-struct-fields [
    e-lib/emit-line/indent field
]
e-lib/emit-line "} RL_LIB;"
e-lib/emit newline

;-----------------------------------------------------------------------------

e-lib/emit-line [
    {#ifdef REB_EXT // can't direct call into EXE, must go through interface}
]

e-lib/emit-lines/indent [
    {// The macros below will require this base pointer:}
    {extern RL_LIB *RL;  // is passed to the RX_Init() function}
    []
]

e-lib/emit-line/indent [
    {// Macros to access reb-lib functions (from non-linked extensions):}
]

e-lib/emit newline
for-each macro struct-call-macros [
    e-lib/emit-line/indent macro
]
e-lib/emit newline

e-lib/emit-line [
    {#else // ...calling Rebol built as DLL or code built into the EXE itself}
]

e-lib/emit-line/indent [
    {// Undecorated prototypes, don't call with this name directly}
]
for-each proto undecorated-prototypes [
    e-lib/emit-line/indent proto
]
e-lib/emit newline

e-lib/emit-line/indent [
    {// Use these macros for consistency with extension code naming}
]
for-each macro direct-call-macros [
    e-lib/emit-line/indent macro
]
e-lib/emit-lines [
    {#endif // REB_EXT}
    {}
]

e-lib/emit-lines [
    {#ifdef __cplusplus}
    "}"
    {#endif}
]

e-lib/write-emitted

;-----------------------------------------------------------------------------

e-table: (make-emitter
    "REBOL Interface Table Singleton" output-dir/tmp-reb-lib-table.inc)

e-table/emit-line "RL_LIB Ext_Lib = {"
e-table/emit-line/indent table-init-items
e-table/emit-line "};"

e-table/write-emitted
