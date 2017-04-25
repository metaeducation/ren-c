REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make Reb-Lib related files"
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
do %form-header.r

print "--- Make Reb-Lib Headers ---"

verbose: true

lib-ver: 2

preface: "RL_"

src-dir: %../core/
reb-lib: src-dir/a-lib.c
ext-lib: src-dir/f-extension.c

args: parse-args system/options/args
output-dir: to file! any [:args/OUTDIR %../]
output-dir: fix-win32-path output-dir
out-dir: output-dir/include
mkdir/deep out-dir

reb-ext-lib:  out-dir/reb-lib.h   ; for Host usage
reb-ext-defs: out-dir/reb-lib-lib.h  ; for REBOL usage

ver: load %../boot/version.r


;-----------------------------------------------------------------------------
;-----------------------------------------------------------------------------

proto-count: 0

xlib-buffer: make string! 20000
rlib-buffer: make string! 1000
mlib-buffer: make string! 1000
dlib-buffer: make string! 1000
xsum-buffer: make string! 1000

emit: func [d] [append adjoin xlib-buffer d newline]
emit-rlib: func [d] [append adjoin rlib-buffer d newline]
emit-dlib: func [d] [append adjoin dlib-buffer d newline]
emit-mlib: proc [d /nol] [
    adjoin mlib-buffer d
    if not nol [append mlib-buffer newline]
]

count: func [s c /local n] [
    if find ["()" "(void)"] s [return "()"]
    out: copy "(a"
    n: 1
    while [s: find/tail s c][
        adjoin out [#"," #"a" + n]
        n: n + 1
    ]
    append out ")"
]

in-sub: func [text pattern /local position] [
    all [
        position: find text pattern ":"
        insert position "^/:"
        position: find next position newline
        remove position
        insert position " - "
    ]
]

pads: func [start col] [
    str: copy ""
    col: col - offset-of start tail start
    head insert/dup str #" " col
]

emit-proto: proc [
    proto
] [

    if all [
        proto
        trim proto
        pos.id: find proto preface
        find proto #"("
    ] [
        emit ["RL_API " proto ";"] ;    // " the-file]
        append xsum-buffer proto
        fn.declarations: copy/part proto pos.id
        pos.lparen: find pos.id #"("
        fn.name: copy/part pos.id pos.lparen
        fn.name.upper: uppercase copy fn.name
        fn.name.lower: lowercase copy find/tail fn.name preface

        emit-dlib [spaced-tab fn.name ","]

        emit-rlib [
            spaced-tab fn.declarations "(*" fn.name.lower ")" pos.lparen ";"
        ]

        args: count pos.lparen #","
        mlib.tail: tail mlib-buffer
        emit-mlib/nol ["#define " fn.name.upper args]
        emit-mlib [pads mlib.tail 35 " RL->" fn.name.lower args]

        comment-text: proto-parser/notes
        encode-lines comment-text {**} { }

        emit-mlib [
            "/*" newline
            "**" space space proto newline
            "**" newline
            comment-text
            "*/" newline
        ]

        proto-count: proto-count + 1
    ]
]

process: func [file] [
    if verbose [probe [file]]
    data: read the-file: file
    data: to-string data

    proto-parser/proto-prefix: "RL_API "
    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

;-----------------------------------------------------------------------------

emit-rlib {
typedef struct rebol_ext_api ^{}

;-----------------------------------------------------------------------------

process reb-lib
process ext-lib

;-----------------------------------------------------------------------------

emit-rlib "} RL_LIB;"

out: to-string reduce [
form-header/gen "REBOL Host and Extension API" %reb-lib.r %make-reb-lib.r
{
// These constants are created by the release system and can be used to check
// for compatiblity with the reb-lib DLL (using RL_Version.)
#define RL_VER } ver/1 {
#define RL_REV } ver/2 {
#define RL_UPD } ver/3 {


// Function entry points for reb-lib (used for MACROS below):}
rlib-buffer
{
// Extension entry point functions:
#ifdef TO_WINDOWS
    #define RXIEXT __declspec(dllexport)
#else
    #define RXIEXT extern
#endif

#ifdef __cplusplus
extern "C" ^{
#endif

RXIEXT const char *RX_Init(int opts, RL_LIB *lib);
RXIEXT int RX_Quit(int opts);
RXIEXT int RX_Call(int cmd, const REBVAL *frm, void *data);

// The macros below will require this base pointer:
extern RL_LIB *RL;  // is passed to the RX_Init() function

// Macros to access reb-lib functions (from non-linked extensions):

}
mlib-buffer
{

#define RL_MAKE_BINARY(s) RL_MAKE_STRING(s, FALSE)

#ifndef REB_EXT // not extension lib, use direct calls to r3lib

}
xlib-buffer
{
#endif // REB_EXT

#ifdef __cplusplus
^}
#endif

}
]

write reb-ext-lib out

;-----------------------------------------------------------------------------

out: to-string reduce [
form-header/gen "REBOL Host/Extension API" %reb-lib-lib.r %make-reb-lib.r
{RL_LIB Ext_Lib = ^{
}
dlib-buffer
{^};
}
]

write reb-ext-defs out

;ask "Done"
print "   "
