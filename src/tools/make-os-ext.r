REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate OS host API headers"
    File: %make-os-ext.r
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

verbose: false

version: load %../boot/version.r

lib-version: version/3
print ["--- Make OS Ext Lib --- Version:" lib-version]

do %r2r3-future.r
do %common.r
do %common-emitter.r
do %common-parsers.r
do %systems.r

args: parse-args system/options/args
config: config-system to-value :args/OS_ID
output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/include

file-base: has load %file-base.r

change-dir %../os/

; Collect OS-specific host files:
unless (
    os-specific-objs: select file-base to word! unspaced ["os-" config/os-base]
)[
    fail [
        "make-os-ext.r requires os-specific obj list in file-base.r"
        "none was provided for" unspaced ["os-" config/os-base]
    ]
]

; We want a list of files to search for the host-lib.h export function
; prototypes (called out with fancy /******* headers).  Those files are
; any preceded by a + sign in either the os or "os-specific" lists in
; file-base.r, so get those and ignore the rest.

files: copy []

rule: ['+ set scannable [word! | path!] (append files to-file scannable) | skip]

parse file-base/os [some rule]
parse os-specific-objs [some rule]

proto-count: 0

host-lib-externs: make string! 20000

host-lib-struct: make string! 1000

host-lib-instance: make string! 1000

rebol-lib-macros: make string! 1000
host-lib-macros: make string! 1000

;
; A checksum value is made to see if anything about the hostkit API changed.
; This collects the function specs for the purposes of calculating that value.
;
checksum-source: make string! 1000

count: func [s c /local n] [
    if find ["()" "(void)"] s [return "()"]
    output-buffer: copy "(a"
    n: 1
    while [s: find/tail s c][
        adjoin output-buffer [#"," #"a" + n]
        n: n + 1
    ]
    append output-buffer ")"
]

emit-proto: proc [
    proto
] [

    if all [
        proto
        trim proto
        not find proto "static"

        pos.id: find proto "OS_"

        ;-- !!! All functions *should* start with OS_, not just
        ;-- have OS_ somewhere in it!  At time of writing, Atronix
        ;-- has added As_OS_Str and when that is addressed in a
        ;-- later commit to OS_STR_FROM_SERIES (or otherwise) this
        ;-- backwards search can be removed
        pos.id: next find/reverse pos.id space
        pos.id: either #"*" = first pos.id [next pos.id] [pos.id]

        find proto #"("
    ] [

        ; !!! We know 'the-file', but it's kind of noise to annotate
        append host-lib-externs reduce [
            "extern " proto ";" newline
        ]

        append checksum-source proto

        fn.declarations: copy/part proto pos.id
        pos.lparen: find pos.id #"("
        fn.name: copy/part pos.id pos.lparen
        fn.name.upper: uppercase copy fn.name
        fn.name.lower: lowercase copy fn.name

        append host-lib-instance reduce [spaced-tab fn.name "," newline]

        append host-lib-struct reduce [
            spaced-tab fn.declarations "(*" fn.name.lower ")" pos.lparen ";"
            newline
        ]

        args: count pos.lparen #","
        append rebol-lib-macros reduce [
            {#define} space fn.name.upper args space {Host_Lib->} fn.name.lower args newline
        ]

        append host-lib-macros reduce [
            "#define" space fn.name.upper args space fn.name args newline
        ]

        proto-count: proto-count + 1
    ]
]

process: func [file] [
    if verbose [probe [file]]
    data: read the-file: file
    data: to-string data
    proto-parser/emit-proto: :emit-proto
    proto-parser/process data
]

append host-lib-struct {
typedef struct REBOL_Host_Lib ^{
    int size;
    unsigned int ver_sum;
    REBDEV **devices;
}

for-each file files [
    print ["scanning" file]
    if all [
        %.c = suffix? file
    ][process file]
]

append host-lib-struct "} REBOL_HOST_LIB;"


;
; Do a reduce which produces the output string we will write to host-lib.h
;

e-lib: make-emitter "Host Access Library" output-dir/include/host-lib.h

e-lib/emit-lines [
    [{#define HOST_LIB_VER} space lib-version]
    [{#define HOST_LIB_SUM} space checksum/tcp to-binary checksum-source]
    [{#define HOST_LIB_SIZE} space proto-count]
]

e-lib/emit reduce [
{
// !!! SEE **WARNING** BEFORE EDITING

#ifdef __cplusplus
extern "C" ^{
#endif

extern REBDEV *Devices[];

/***********************************************************************
**
**  HOST LIB TABLE DEFINITION
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
**      The "Rebol Host" provides a "Host Lib" interface to operating
**      system services that can be used by "Rebol Core".  Each host
**      provides functions with names starting with OS_ and then a
**      mixed-case name separated by underscores (e.g. OS_Get_Time).
**
**      Rebol cannot call these functions directly.  Instead, they are
**      put into a table (which is actually a struct whose members are
**      function pointers of the appropriate type for each call).  It is
**      similar in spirit to how IOCTLs work in operating systems:
**
**          https://en.wikipedia.org/wiki/Ioctl
**
**      To give a sense of scale, there are 48 separate functions in the
**      Linux build at time of writing.  Some functions are very narrow
**      in what they do...such as OS_Browse which will open a web browser.
**      Other functions are doorways to dispatching a wide variety of
**      requests, such as OS_Do_Device.)
**
**      So instead of OS_Get_Time, Core uses 'Host_Lib->os_get_time(...)'.
**      Since that is verbose, an all-caps macro is provided, which in
**      this case would be OS_GET_TIME.  For parity, all-caps macros are
**      provided in the host like '#define OS_GET_TIME OS_Get_Time'.  As
**      a result, the all-caps forms should be preserved since they can
**      be read/copied/pasted consistently between host and core code.
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
***********************************************************************/
}

(host-lib-struct) newline

{
extern const REBOL_HOST_LIB *Host_Lib;


//** Included by HOST *********************************************

#ifndef REB_DEF
}

newline (host-lib-externs) newline

newline (host-lib-macros) newline

{
#else //REB_DEF

//** Included by REBOL ********************************************

}

newline newline (rebol-lib-macros)

{
#endif //REB_DEF


/***********************************************************************
**
**  "OS" MEMORY ALLOCATION AND FREEING MACROS
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
**      These parallel Rebol's ALLOC/ALLOC_N/FREE macros.
**      Main difference is that there is only one FREE, as the
**      hostkit API is not required to remember the size on free.
**
**      It is not strictly necessary to use these to allocate memory
**      from the hostkit allocator instead of malloc().  The only
**      time you are *required* to use the hostkit allocator is if
**      you are exchanging memory with Rebol Core and have to
**      agree about how to free it.  (So if Rebol allocates
**      something the Host may have to free, or vice-versa.)
**
**      However, in embedded programming it is thought that perhaps
**      malloc would not be available (or not the best choice) on
**      small systems.  So getting in the habit of using the
**      habit of using the host allocator isn't a bad thing, and
**      these macros make it convenient and type safe.
**
**      In the Ren/C codebase where the goal is to be able to
**      build with both ANSI C89 *and* C++ (all the way up to the
**      latest standard, C++14 or C++17 etc.) then these macros
**      are much better than doing the casting of malloc manually.
**
**      Note: OS_ALLOC_N/OS_FREE_N used to be called OS_ALLOC_ARRAY
**      and OS_FREE_ARRAY.  But with the change of Rebol's ANY-BLOCK!
**      to ANY-ARRAY! the ARRAY term has a more important use.  So
**      this uses N to mean "allocate N items contiguously".
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
***********************************************************************/

// !!! SEE **WARNING** BEFORE EDITING
#define OS_ALLOC(t) \
    cast(t *, OS_ALLOC_MEM(sizeof(t)))
#define OS_ALLOC_ZEROFILL(t) \
    cast(t *, memset(OS_ALLOC(t), '\0', sizeof(t)))
#define OS_ALLOC_N(t,n) \
    cast(t *, OS_ALLOC_MEM(sizeof(t) * (n)))
#define OS_ALLOC_N_ZEROFILL(t,n) \
    cast(t *, memset(OS_ALLOC_N(t, (n)), '\0', sizeof(t) * (n)))
#define OS_FREE(p) \
    OS_FREE_MEM(p)

#ifdef __cplusplus
^}
#endif
}
]

e-lib/write-emitted

e-table: (
    make-emitter "Host Table Definition" output-dir/include/host-table.inc
)

e-table/emit {
/***********************************************************************
**
**  HOST LIB TABLE DEFINITION
**
**      This is the actual definition of the host table.  In order for
**      the assignments to work, you must have included host-lib.h with
**      REB_DEF undefined, to get the prototypes for the host kit
**      functions.  (You'll get this automatically if you are doing
**      #include "reb-host.h).
**
**      There can be only one instance of this table linked into your
**      program, or you will get multiple defintitions of the Host_Lib
**      table.  You may wish to make a .c file that only includes
**      this, in order to easily call out which object file has the
**      singular definition of Host_Lib that you need.
**
***********************************************************************/

EXTERN_C REBOL_HOST_LIB Host_Lib_Init;

REBOL_HOST_LIB Host_Lib_Init = ^{

    HOST_LIB_SIZE,
    (HOST_LIB_VER << 16) + HOST_LIB_SUM,
    (REBDEV**)&Devices,
}

e-table/emit host-lib-instance

e-table/emit-line "};"

e-table/write-emitted
