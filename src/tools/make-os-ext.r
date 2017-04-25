REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate OS host API headers"
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
do %common-parsers.r
do %systems.r

args: parse-args system/options/args
config: config-system to-value :args/OS_ID
output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/include

do %form-header.r

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

output-buffer: reduce [

form-header/gen "Host Access Library" %host-lib.h %make-os-ext.r

newline

{#define HOST_LIB_VER} space lib-version newline
{#define HOST_LIB_SUM} space checksum/tcp to-binary checksum-source newline
{#define HOST_LIB_SIZE} space proto-count newline

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
extern REBOL_HOST_LIB *Host_Lib;


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


/***********************************************************************
**
**  "OS" STRING FUNCTION ABSTRACTIONS
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
**      Rebol's string values are currently represented internally as
**      a series of either 8-bit REBYTEs (if codepoints are all <= 255) or
**      a series of 16-bit REBUNIs otherwise.  This is unrelated to
**      the issue of what the native character width is on the
**      platform which Rebol runs.  Windows has standardized on 16-bit
**      wide characters, and the wchar_t type is required to be 2 bytes
**      on windows platforms.
**
**      (There is no guarantee of the size of wchar_t on Linux, and
**      the C standard itself does not require a guarantee on other
**      platforms either.)
**
**      Yet at *some* point, Rebol must communicate with the OS in its
**      native format.  The API interfaces for asking to read from a file
**      or even to print a message out on the screen have different
**      encodings on each platform.  In order to speak of these strings,
**      Rebol introduced a variable-sized character type called a REBCHR.
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
**      REBCHR creates some complexity, because while code running on
**      the host knows what size it is...Rebol's codebase has to treat
**      it as a black box.  However, it did not quite treat it so--and
**      has a number of places where the strings were inspected and
**      handled.  These inspections generally relied upon wrappers of
**      strncpy, strncat, strchr and strlen.  But most of the code
**      that used REBCHR at all was sketchy-at-best.
**
**      @HostileFork feels that Rebol's model for extension probably
**      needs another answer (or a more coherent version of the current
**      answer) vs. having the core itself getting too hands-on with
**      brokering native format strings.  And the reach of REBCHR should
**      be reigned in as much as possible, with host code using its
**      own type (char, wchar_t).
**
**      So in order to limit the scope of REBCHR, and ensure that type
**      checking in the core is as rigorous as possible when dealing
**      with it (effectively letting the wide char developers test
**      their impacts on the non-wide char builds, and vice versa), the
**      REBCHR type is "opaque" inside the core (see sys-core.h).  It
**      is so opaque as to be a struct containing the native char type
**      in Debug builds.
**
**      By contrast, REBCHR is "transparent" to the host (see reb-host.h)
**      The expectation is that the host not use REBCHR or the wrappers
**      like OS_STRLEN...instead using char/strlen or wchar_t/wcslen.
**      However--the wrappers are still exported to the host, because
**      there are some pieces of code that are written outside the core
**      but are designed to be reused across hosts, so that code has to
**      be as agnostic about the character size as the core does.
**
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
***********************************************************************/

#ifdef OS_WIDE_CHAR
// !!! SEE **WARNING** BEFORE EDITING
    #define OS_WIDE TRUE
    #define OS_STR_LIT(s) cast(const REBCHR*, L##s)
#else
// !!! SEE **WARNING** BEFORE EDITING
    #define OS_WIDE FALSE
    #define OS_STR_LIT(s) cast(const REBCHR*, s)
#endif

#if defined(NDEBUG) || !defined(REB_DEF)
// !!! SEE **WARNING** BEFORE EDITING
    #define OS_MAKE_CH(c) (c)
    #define OS_CH_VALUE(c) (c)
    #define OS_CH_EQUAL(os_ch, ch)      ((os_ch) == (ch))

    #ifdef OS_WIDE_CHAR
    // !!! SEE **WARNING** BEFORE EDITING
        #define OS_STRNCPY(d,s,m) \
            wcsncpy(cast(wchar_t*, (d)), cast(const wchar_t*, (s)), (m))
        #define OS_STRNCAT(d,s,m) \
            wcsncat(cast(wchar_t*, (d)), cast(const wchar_t*, (s)), (m))
        #define OS_STRNCMP(l,r,m) \
            wcsncmp(cast(wchar_t*, (l)), cast(const wchar_t*, (r)), (m))
        // We have to m_cast because C++ actually has a separate overload of
        // wcschr which will return a const pointer if the in pointer was
        // const.
        #define OS_STRCHR(d,s) \
            cast(REBCHR*, \
                m_cast(wchar_t*, wcschr(cast(const wchar_t*, (d)), (s))) \
            )
        #define OS_STRLEN(s)            wcslen(cast(const wchar_t*, (s)))
    #else
        #ifdef TO_OPENBSD
    // !!! SEE **WARNING** BEFORE EDITING
            #define OS_STRNCPY(d,s,m) \
                strlcpy(cast(char*, (d)), cast(const char*, (s)), (m))
            #define OS_STRNCAT(d,s,m) \
                strlcat(cast(char*, (d)), cast(const char*, (s)), (m))
        #else
    // !!! SEE **WARNING** BEFORE EDITING
            #define OS_STRNCPY(d,s,m) \
                strncpy(cast(char*, (d)), cast(const char*, (s)), (m))
            #define OS_STRNCAT(d,s,m) \
                strncat(cast(char*, (d)), cast(const char*, (s)), (m))
        #endif
        #define OS_STRNCMP(l,r,m) \
            strncmp(cast(const char*, (l)), cast(const char*, (r)), (m))
        // We have to m_cast because C++ actually has a separate overload of
        // strchr which will return a const pointer if the in pointer was
        // const.
        #define OS_STRCHR(d,s) \
            cast(REBCHR*, m_cast(char*, strchr(cast(const char*, (d)), (s))))
        #define OS_STRLEN(s)            strlen(cast(const char*, (s)))
    #endif
#else
// !!! SEE **WARNING** BEFORE EDITING
    // Debug build only; fully opaque type and functions for certainty
    #define OS_CH_VALUE(c)              ((c).num)
    #define OS_CH_EQUAL(os_ch, ch)      ((os_ch).num == ch)
    #define OS_MAKE_CH(c)               OS_MAKE_CH_(c)
    #define OS_STRNCPY(d,s,m)           OS_STRNCPY_((d), (s), (m))
    #define OS_STRNCAT(d,s,m)           OS_STRNCAT_((d), (s), (m))
    #define OS_STRNCMP(l,r,m)           OS_STRNCMP_((l), (r), (m))
    #define OS_STRCHR(d,s)              OS_STRCHR_((d), (s))
    #define OS_STRLEN(s)                OS_STRLEN_(s)
#endif

#ifdef __cplusplus
^}
#endif
}
]

;print output-buffer ;halt
;print ['checksum checksum/tcp checksum-source]
write output-dir/include/host-lib.h output-buffer


output-buffer: unspaced [
form-header/gen "Host Table Definition" %host-table.inc %make-os-ext.r

{
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
**      !!!
**      !!! **WARNING!**  DO NOT EDIT THIS! (until you've checked...)
**      !!! BE SURE YOU ARE EDITING MAKE-OS-EXT.R AND NOT HOST-LIB.H
**      !!!
**
***********************************************************************/

EXTERN_C REBOL_HOST_LIB Host_Lib_Init;

REBOL_HOST_LIB Host_Lib_Init = ^{

    HOST_LIB_SIZE,
    (HOST_LIB_VER << 16) + HOST_LIB_SUM,
    (REBDEV**)&Devices,
}

(host-lib-instance)

"^};" newline
]

write output-dir/include/host-table.inc output-buffer

;ask "Done"
print "   "
