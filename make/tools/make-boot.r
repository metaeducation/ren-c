REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Make primary boot files"
    File: %make-boot.r ;-- used by EMIT-HEADER to indicate emitting script
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        A lot of the REBOL system is built by REBOL, and this program
        does most of the serious work. It generates most of the C include
        files required to compile REBOL.
    }
]

print "--- Make Boot : System Embedded Script ---"

do %r2r3-future.r
do %common.r
do %common-emitter.r

do %systems.r

change-dir %../../src/boot/

args: parse-args system/options/args
config: config-system get 'args/OS_ID

first-rebol-commit: "19d4f969b4f5c1536f24b023991ec11ee6d5adfb"

either args/GIT_COMMIT = "unknown" [
    ;
    ; !!! If we used blank here, then R3-Alpha would render it as the word
    ; "none" which is not defined during the execution of %sysobj.r, so by
    ; using '_ it will act as a WORD! in R3-Alpha, and render as _.
    ;
    ; 
    git-commit: either word? first [_] [
        '_ ;-- R3-Alpha is being used for bootstrap
    ][
        _ ;-- Ren-C is being used for bootstrap
    ]
][
    git-commit: args/GIT_COMMIT
    if (length-of git-commit) != (length-of first-rebol-commit) [
        print ["GIT_COMMIT should be a full hash, e.g." first-rebol-commit]
        print ["Invalid hash was:" git-commit]
        quit
    ]
]

;-- SETUP --------------------------------------------------------------

;dir: %../core/temp/  ; temporary definition
output-dir: system/options/path/prep
inc: output-dir/include
core: output-dir/core
boot: output-dir/boot
mkdir/deep probe inc
mkdir/deep probe boot
mkdir/deep probe core

version: load %version.r
version/4: config/id/2
version/5: config/id/3

;-- Title string put into boot.h file checksum:
Title:
{REBOL
Copyright 2012 REBOL Technologies
REBOL is a trademark of REBOL Technologies
Licensed under the Apache License, Version 2.0
}

sections: [
    boot-types
    boot-words
    boot-actions
    boot-natives
    boot-typespecs
    boot-errors
    boot-sysobj
    boot-base
    boot-sys
    boot-mezz
;   boot-script
]

; Args passed: platform, product
;
; !!! Heed /script/args so you could say e.g. `do/args %make-boot.r [0.3.01]`
; Note however that current leaning is that scripts called by the invoked
; process will not have access to the "outer" args, hence there will be only
; one "args" to be looked at in the long run.  This is an attempt to still
; be able to bootstrap under the conditions of the A111 rebol.com R3-Alpha
; as well as function either from the command line or the REPL.
;
unless args: any [
    either string? :system/script/args [
        either block? load system/script/args [
            load system/script/args
        ][
            reduce [load system/script/args]
        ]
    ][
        get 'system/script/args
    ]

    ; This is the only piece that should be necessary if not dealing w/legacy
    system/options/args
][
    fail "No platform specified."
]

product: to-word any [get 'args/PRODUCT | "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

;-- Fetch platform specifications:
;init-build-objects/platform platform
;platform-data: platforms/:platform
;build: platform-data/builds/:product


type-table: load %types.r

e-dispatch: make-emitter "Dispatchers" core/tmp-dispatchers.c
e-dispatch/emit newline


e-dispatch/emit [{#include "sys-core.h"}]
e-dispatch/emit newline


e-dispatch/emit {
    /*
    ** VALUE DISPATCHERS: e.g. for `append value x` or `select value y`
    */

    REBTAF Value_Dispatch[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/class [
        0 [e-dispatch/emit-item "NULL"] ;-- never dispatch on REB_0
        * [e-dispatch/emit-item "T_Unhooked"] ;-- extensions replace this
    ][
        e-dispatch/emit-item ["T_" propercase-of ensure word! t/class]
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/emit {
    /*
    ** PATH DISPATCHERS: for `a/b`, `:a/b`, `a/b:`, `pick a b`, `poke a b`
    */

    REBPEF Path_Dispatch[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/path [
        - [e-dispatch/emit-item "PD_Fail"]
        + [e-dispatch/emit-item ["PD_" propercase-of ensure word! t/class]]
        * [e-dispatch/emit-item "PD_Unhooked"]
    ][
        ; !!! Today's PORT! path dispatches through context even though
        ; that isn't its technical "class" for responding to actions.
        ;
        e-dispatch/emit-item ["PD_" propercase-of ensure word! t/path]
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/emit {
    /*
    ** MAKE DISPATCHERS: for `make datatype def`
    */

    MAKE_CFUNC Make_Dispatch[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/make [
        - [e-dispatch/emit-item "MAKE_Fail"]
        + [e-dispatch/emit-item ["MAKE_" propercase-of ensure word! t/class]]
        * [e-dispatch/emit-item "MAKE_Unhooked"]
    ][
        fail "MAKE in %types.r should be, -, +, or *"
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/emit {
    /*
    ** TO DISPATCHERS: for `to datatype value`
    */

    TO_CFUNC To_Dispatch[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/make [
        - [e-dispatch/emit-item "TO_Fail"]
        + [e-dispatch/emit-item ["TO_" propercase-of ensure word! t/class]]
        * [e-dispatch/emit-item "TO_Unhooked"]
    ][
        fail "TO in %types.r should be -, +, or *"
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/emit {
    /*
    ** MOLD DISPATCHERS: for `mold value`
    */

    MOLD_CFUNC Mold_Or_Form_Dispatch[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/mold [
        - [e-dispatch/emit-item "MF_Fail"]
        + [e-dispatch/emit-item ["MF_" propercase-of ensure word! t/class]]
        * [e-dispatch/emit-item "MF_Unhooked"]
    ][
        ; ERROR! may be a context, but it has its own special forming
        ; beyond the class (falls through to ANY-CONTEXT! for mold), and
        ; BINARY! has a different handler than strings
        ;
        e-dispatch/emit-item ["MF_" propercase-of ensure word! t/mold]
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/emit {
    /*
    ** COMPARISON DISPATCHERS, to support GREATER?, EQUAL?, LESSER?...
    */

    REBCTF Compare_Types[REB_MAX] = ^{
}
for-each-record t type-table [
    switch/default t/class [
        0 [e-dispatch/emit-item "NULL"]
        * [e-dispatch/emit-item "CT_Unhooked"]
    ][
        e-dispatch/emit-item ["CT_" propercase-of ensure word! t/class]
    ]
    e-dispatch/emit-annotation ensure [word! integer!] t/name
]
e-dispatch/emit-end


e-dispatch/write-emitted



;----------------------------------------------------------------------------
;
; %reb-types.h - Datatype Definitions
;
;----------------------------------------------------------------------------

e-types: make-emitter "Datatype Definitions" inc/reb-types.h


e-types/emit {
    /*
    ** INTERNAL DATATYPE CONSTANTS, e.g. REB_BLOCK or REB_TAG
    **
    ** Do not export these values via libRebol, as the numbers can change.
    ** Their ordering is for supporting certain optimizations, such as being
    ** able to quickly check if a type IS_BINDABLE().  When types are added,
    ** or removed, the numbers must shuffle around to preserve invariants.
    **
    ** Note: REB_0 is reserved for internal purposes...0 is not a "type".
    ** But to make it easier to build the dispatch tables (which must have
    ** an entry for index 0) it appears in %types.r
    */

    enum Reb_Kind ^{
}

n: 0
for-each-record t type-table [
    either t/name = 0 [
        e-types/emit-item/assign "REB_0" 0
    ][
        ensure word! t/name
        e-types/emit-item/upper ["REB_" t/name]
        e-types/emit-annotation n
    ]
    n: n + 1
]

e-types/emit-item/assign "REB_MAX" n
e-types/emit-end


e-types/emit {
    /*
    ** SINGLE TYPE CHECK MACROS, e.g. IS_BLOCK() or IS_TAG()
    **
    ** These routines are based on VAL_TYPE(), which does much more checking
    ** than VAL_TYPE_RAW() in the debug build.  In some commonly called
    ** routines, it may be worth it to use the less checked version.
    **
    ** Note: There's no IS_0() test for REB_0.  Usages alias it and test
    ** against the alias for clarity, e.g. `VAL_TYPE(v) == REB_0_PARTIAL`
    */
}
e-types/emit newline

boot-types: copy []
n: 0
for-each-record t type-table [
    if n != 0 [
        e-types/emit 't {
            #define IS_${T/NAME}(v) \
                (VAL_TYPE(v) == REB_${T/NAME}) /* $(n) */
        }
        e-types/emit newline

        append boot-types to-word adjoin form t/name "!"
    ]
    n: n + 1
]

types-header: first load/header %types.r
e-types/emit trim/auto copy ensure string! types-header/macros


e-types/emit {
    /*
    ** TYPESET DEFINITIONS (e.g. TS_ANY_ARRAY or TS_ANY_STRING)
    **
    ** Note: User-facing typesets, such as ANY-VALUE!, do not include void
    ** (absence of a value), nor do they include the internal "REB_0" type.
    */

    #define TS_VALUE ((FLAGIT_KIND(REB_MAX_VOID) - 1) - FLAGIT_KIND(REB_0))
}
typeset-sets: copy []

for-each-record t type-table [
    for-each ts compose [(t/typesets)] [
        spot: any [
            to-value select typeset-sets ts
            first back insert tail-of typeset-sets reduce [ts copy []]
        ]
        append spot t/name
    ]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
    e-types/emit 'ts "#define TS_${TS} ("
    for-each t types [
        e-types/emit 't "FLAGIT_KIND(REB_${T})|"
    ]
    e-types/unemit #"|" ;-- remove the last | added
    e-types/emit unspaced [")" newline]
]

e-types/write-emitted


;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

e-bootdefs: make-emitter "Boot Definitions" inc/tmp-bootdefs.h


e-bootdefs/emit {
    /*
    ** VERSION INFORMATION
    **
    ** !!! While using 5 byte-sized integers to denote a Rebol version might
    ** not be ideal, it's a standard that's been around a long time.
    */

    #define REBOL_VER $(version/1)
    #define REBOL_REV $(version/2)
    #define REBOL_UPD $(version/3)
    #define REBOL_SYS $(version/4)
    #define REBOL_VAR $(version/5)
}
e-bootdefs/emit-line []


e-bootdefs/emit {
    /*
    ** CONSTANTS FOR BUILT-IN SYMBOLS: e.g. SYM_THRU or SYM_INTEGER_X
    **
    ** ANY-WORD! uses internings of UTF-8 character strings.  An arbitrary
    ** number of these are created at runtime, and can be garbage collected
    ** when no longer in use.  But a pre-determined set of internings are
    ** assigned small integer "SYM" compile-time-constants, to be used in
    ** switch() for efficiency in the core.
    **
    ** Datatypes are given symbol numbers at the start of the list, so that
    ** their SYM_XXX values will be identical to their REB_XXX values.
    **
    ** Note: SYM_0 is not a symbol of the string "0".  It's the "SYM" constant
    ** that is returned for any interning that *does not have* a compile-time
    ** constant assigned to it.  Since VAL_WORD_SYM() will return SYM_0 for
    ** all user (and extension) defined words, don't try to check equality
    ** with `VAL_WORD_SYM(word1) == VAL_WORD_SYM(word2)`.
    */

    enum REBOL_Symbols ^{
}

e-bootdefs/emit-item/assign "SYM_0" 0

n: 1
boot-words: copy [] ;-- MAP! in R3-Alpha is unreliable
add-word: func [
    word
    /skip-if-duplicate
][
    if find boot-words word [
        if skip-if-duplicate [return blank]
        fail ["Duplicate word specified" word]
    ]

    e-bootdefs/emit-item/upper [
        comment [to-cname ("SYM_" word)] ;-- `...` would be SYM__DOT_DOT_DOT
        "SYM_" (to-c-name word) ;-- `...` is recognized to make SYM_ELLIPSIS
    ]
    e-bootdefs/emit-annotation spaced [n "-" word]
    n: n + 1

    append boot-words word
    return blank
]

for-each-record t type-table [
    if t/name != 0 [
        add-word to-word unspaced [ensure word! t/name "!"]
    ]
]

wordlist: load %words.r
replace wordlist '*port-modes* load %modes.r

for-each word wordlist [add-word word]

boot-actions: load boot/tmp-actions.r
for-each item boot-actions [
    if set-word? :item [
        add-word/skip-if-duplicate to-word item ;-- maybe in %words.r already
    ]
]

e-bootdefs/emit-end

print [n "words + actions"]

e-bootdefs/write-emitted

;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

e-sysobj: make-emitter "System Object" inc/tmp-sysobj.h

at-value: func ['field] [next find boot-sysobj to-set-word field]

boot-sysobj: load %sysobj.r
change at-value version version
change at-value commit git-commit
change at-value build now/utc
change at-value product to lit-word! product

change/only at-value platform reduce [
    any [config/platform-name | "Unknown"]
    any [config/build-label | ""]
]

ob: has boot-sysobj

make-obj-defs: procedure [
    {Given a Rebol OBJECT!, write C structs that can access its raw variables}

    e [object!]
       {The emitter to write definitions to}
    obj
    prefix
    depth
    /selfless
][
    prefix: uppercase-of prefix
    e/emit-line ["enum " prefix "object {"]

    either selfless [
        ;
        ; Make sure *next* value starts at 1.  Keys/vars in contexts start
        ; at 1, and if there's no "userspace" self in the 1 slot, the first
        ; key has to be...so we make `SYS_CTX_0 = 0` (for instance)
        ;
        e/emit-item/assign [prefix "0"] 0
    ][
        ; The internal generator currently puts SELF at the start of new
        ; objects in key slot 1, by default.  Eventually MAKE OBJECT! will
        ; have nothing to do with adding SELF, and it will be entirely a
        ; by-product of generators.
        ;
        e/emit-item/assign [prefix "SELF"] 1
    ]

    for-each field words-of obj [
        e/emit-item/upper [prefix field]
    ]
    e/emit-item [prefix "MAX"]
    e/emit-end

    if depth > 1 [
        for-each field words-of obj [
            if all [
                field != 'standard
                object? get in obj field
            ][
                extended-prefix: uppercase to-c-name [prefix field "_"]
                make-obj-defs e obj/:field extended-prefix (depth - 1)
            ]
        ]
    ]
]

make-obj-defs e-sysobj ob "SYS_" 1
make-obj-defs e-sysobj ob/catalog "CAT_" 4
make-obj-defs e-sysobj ob/contexts "CTX_" 4
make-obj-defs e-sysobj ob/standard "STD_" 4
make-obj-defs e-sysobj ob/state "STATE_" 4
;make-obj-defs e-sysobj ob/network "NET_" 4
make-obj-defs e-sysobj ob/ports "PORTS_" 4
make-obj-defs e-sysobj ob/options "OPTIONS_" 4
;make-obj-defs e-sysobj ob/intrinsic "INTRINSIC_" 4
make-obj-defs e-sysobj ob/locale "LOCALE_" 4
make-obj-defs e-sysobj ob/view "VIEW_" 4

e-sysobj/write-emitted


;----------------------------------------------------------------------------
;
; Event Types
;
;----------------------------------------------------------------------------

e-event: make-emitter "Event Types" inc/reb-evtypes.h

e-event/emit-line ["enum event_types {"]
for-each field ob/view/event-types [
    e-event/emit-item/upper ["EVT_" field]
]
e-event/emit-item "EVT_MAX"
e-event/emit-end

e-event/emit-line ["enum event_keys {"]
e-event/emit-item "EVK_NONE"
for-each field ob/view/event-keys [
    e-event/emit-item/upper ["EVK_" field]
]
e-event/emit-item "EVK_MAX"
e-event/emit-end

e-event/write-emitted


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

e-errnums: make-emitter "Error Structure and Constants" inc/tmp-errnums.h

e-errnums/emit {
/***********************************************************************
**
*/  typedef struct REBOL_Error_Vars
/*
***********************************************************************/
}
e-errnums/emit-line "{"

; Generate ERROR object and append it to bootdefs.h:
e-errnums/emit-line/indent "RELVAL self;" ;-- C++ build cannot be REBVAL
for-each word words-of ob/standard/error [
    either word = 'near [
        e-errnums/emit-line/indent [
            "RELVAL nearest;" ;-- C++ build cannot be REBVAL
        ]
        e-errnums/emit-annotation "near/far are non-standard C keywords"
    ][
        e-errnums/emit-line/indent [
            "RELVAL" space (to-c-name word) ";" ;-- C++ build cannot be REBVAL
         ]
    ]
    
]
e-errnums/emit-line "} ERROR_VARS;"

e-errnums/emit {
/***********************************************************************
**
*/  enum REBOL_Errors
/*
***********************************************************************/
}
e-errnums/emit-line "{"

boot-errors: load %errors.r

id-list: make block! 200

for-each [category info] boot-errors [
    unless all [
        (quote code:) == info/1
        integer? info/2
        (quote type:) == info/3
        string? info/4
    ][
        fail ["%errors.r" category "not [code: INTEGER! type: STRING! ...]"]
    ]

    code: info/2

    new-section: true
    for-each [key val] skip info 4 [
        unless set-word? key [
            fail ["Non SET-WORD! key in %errors.r:" key]
        ]

        id: to-word key
        if find (extract id-list 2) id [
            fail ["DUPLICATE id in %errors.r:" id]
        ]

        append id-list reduce [id val]

        either new-section [
            e-errnums/emit-item/assign/upper ["RE_" id] code
            new-section: false
        ][
            e-errnums/emit-item/upper ["RE_" id]
        ]
        e-errnums/emit-annotation spaced [code mold val]

        code: code + 1
    ]
    e-errnums/emit-item ["RE_" (uppercase-of to word! category) "_MAX"]
    e-errnums/emit newline
]

e-errnums/emit-end

e-errnums/emit-line {#define RE_USER INT32_MAX}
e-errnums/emit-annotation {Hardcoded, update in %make-boot.r}

e-errnums/emit-line {#define RE_CATEGORY_SIZE 1000}
e-errnums/emit-annotation {Hardcoded, update in %make-boot.r}

e-errnums/emit-line {#define RE_INTERNAL_FIRST RE_MISC}
e-errnums/emit-annotation {GENERATED! update in %make-boot.r}

e-errnums/emit-line {#define RE_MAX RE_COMMAND_MAX}
e-errnums/emit-annotation {GENERATED! update in %make-boot.r}
e-errnums/write-emitted

;-------------------------------------------------------------------------

e-errfuncs: make-emitter "Error functions" inc/tmp-error-funcs.h
for-each [id val] id-list [
    n-args: 0
    if block? val [
        parse val [
            any [
                get-word! (
                    n-args: n-args + 1 ; don't use ME, not R3-Alpha compatible
                )
                | skip
            ]
        ]
    ]

    e-errfuncs/emit-line []
    e-errfuncs/emit-line ["//  " mold val] 
    c-id: to-c-name id
    f-name: uppercase/part copy c-id 1
    parse f-name [
        any [
            #"_" w: (uppercase/part w 1)
            | skip
        ]
    ]
    either zero? n-args [
        e-errfuncs/emit-lines [
            [ {static inline REBCTX *Error_} f-name {_Raw(void)}]
            [ "^{" ]
            [ "    return Error(RE_" uppercase c-id ", END);" ]
            [ "^}" ]
        ]
    ][
        e-errfuncs/emit-line [
            {static inline REBCTX *Error_} f-name {_Raw(}
        ]
        i: 0
        while [i < n-args] [
            e-errfuncs/emit-line compose [ {const REBVAL *arg} (i + 1)
                either i < (n-args - 1) [","] [""]
            ]
            i: i + 1 ; don't use ME, not R3-Alpha compatible 
        ]
        e-errfuncs/emit-line [")"]
        e-errfuncs/emit-line [ "^{" ]

        args: copy ""
        i: 0
        while [i < n-args] [
            append args compose [ {, arg} (i + 1)]
            i: i + 1 ; don't use ME, not R3-Alpha comptible
        ]

        e-errfuncs/emit-line/indent [
            "return Error(RE_" uppercase c-id args ", END);"
        ]
        e-errfuncs/emit-line [ "^}" ]
    ]
]
e-errfuncs/write-emitted

;----------------------------------------------------------------------------
;
; Load Boot Mezzanine Functions - Base, Sys, and Plus
;
;----------------------------------------------------------------------------

;-- Add other MEZZ functions:
mezz-files: load %../mezz/boot-files.r ; base lib, sys, mezz

for-each section [boot-base boot-sys boot-mezz] [
    set section make block! 200
    for-each file first mezz-files [
        append get section load join-of %../mezz/ file
    ]

    ;-- Expectation is that section does not return result; GROUP! makes unset
    append get section [()]

    mezz-files: next mezz-files
]

e-sysctx: make-emitter "Sys Context" inc/tmp-sysctx.h

; We don't actually want to create the object in the R3-MAKE Rebol, because
; the constructs are intended to run in the Rebol being built.  But the list
; of top-level SET-WORD!s is needed.  R3-Alpha used a non-evaluating CONSTRUCT
; to do this, but Ren-C's non-evaluating construct expects direct alternation
; of SET-WORD! and unevaluated value (even another SET-WORD!).  So we just
; gather the top-level set-words manually.

sctx: has collect [
    for-each item boot-sys [
        if set-word? :item [
            keep item
            keep "stub proxy for %sys-base.r item"
        ]
    ]
]

; !!! The SYS_CTX has no SELF...it is not produced by the ordinary gathering
; constructor, but uses Alloc_Context() directly.  Rather than try and force
; it to have a SELF, having some objects that don't helps pave the way
; to the userspace choice of self-vs-no-self (as with func's `<with> return`)
;
make-obj-defs/selfless e-sysctx sctx "SYS_CTX_" 1

e-sysctx/write-emitted


;----------------------------------------------------------------------------
;
; TMP-BOOT-BLOCK.R and TMP-BOOT-BLOCK.C
;
; Create the aggregated Rebol file of all the Rebol-formatted data that is
; used in bootstrap.  This includes everything from a list of WORD!s that
; are built-in as symbols, to the sys and mezzanine functions.
;
; %tmp-boot-block.c is just a C file containing a literal constant of the
; compressed representation of %tmp-boot-block.r
;
;----------------------------------------------------------------------------

e-bootblock: make-emitter "Natives and Bootstrap" core/tmp-boot-block.c

e-bootblock/emit-line {#include "sys-core.h"}
e-bootblock/emit newline

externs: make string! 2000
boot-natives: load boot/tmp-natives.r
num-natives: 0

for-each val boot-natives [
    if set-word? val [
        num-natives: num-natives + 1
    ]
]

print [num-natives "natives"]

e-bootblock/emit newline

e-bootblock/emit-line [
    "#define NUM_NATIVES" space num-natives
]
e-bootblock/emit-line [
    "const REBCNT Num_Natives = NUM_NATIVES;"
]

e-bootblock/emit-line {REBVAL Natives[NUM_NATIVES];}

e-bootblock/emit-line "const REBNAT Native_C_Funcs[NUM_NATIVES] = {"

for-each val boot-natives [
    if set-word? val [
        e-bootblock/emit-item ["N_" to word! val]
    ]
]
e-bootblock/emit-end
e-bootblock/emit newline


;-- Build typespecs block (in same order as datatypes table):

boot-typespecs: make block! 100
specs: load %typespec.r
for-each-record t type-table [
    if t/name <> 0 [
        append/only boot-typespecs really select specs to-word t/name
    ]
]

;-- Create main code section (compressed):

write-if-changed boot/tmp-boot-block.r mold reduce sections
data: mold/flat reduce sections
insert tail-of data make char! 0 ; scanner requires zero termination

comp-data: gzip data: to-binary data

; Array sizes in C have to be constant expressions, which doesn't include
; constant values.  Have to use #defines.
;
e-bootblock/emit-line [
    "#define NAT_COMPRESSED_SIZE" space (length-of comp-data)
]

; Though #defines must be used for the array declarations, using values with
; external linkage at other sites makes it so that the file containing the
; natives can be recompiled and not require recompiling places where they
; are referred to as well.
;
e-bootblock/emit-line [
    "const REBCNT Nat_Compressed_Size = NAT_COMPRESSED_SIZE;"
]

e-bootblock/emit-line ["const REBYTE Native_Specs[NAT_COMPRESSED_SIZE] = {"]

;-- Convert UTF-8 binary to C-encoded string:
e-bootblock/emit binary-to-c comp-data
e-bootblock/emit-line "};" ;-- EMIT-END erases last comma but there's no extra

e-bootblock/write-emitted

;-- Output stats:
print [
    "Compressed" length-of data "to" length-of comp-data "bytes:"
    to-integer ((length-of comp-data) / (length-of data) * 100)
    "percent of original"
]


;----------------------------------------------------------------------------
;
; Boot.h - Boot header file
;
;----------------------------------------------------------------------------

e-boot: make-emitter "Bootstrap Structure and Root Module" inc/tmp-boot.h

e-boot/emit newline

e-boot/emit {

EXTERN_C const REBCNT Num_Natives;
EXTERN_C const REBCNT Nat_Compressed_Size;

// Compressed data of the native specifications.  This is uncompressed during
// boot and executed.
//
EXTERN_C const REBYTE Native_Specs[]; // size is Nat_Compressed_Size

// Raw C function pointers for natives.
//
EXTERN_C const REBNAT Native_C_Funcs[]; // size is Num_Natives

// A canon ACTION! REBVAL of the native, accessible by the native's index #.
//
EXTERN_C REBVAL Natives[]; // size is Num_Natives
}

e-boot/emit newline
e-boot/emit-line "enum Native_Indices {"

nat-index: 0
for-each val boot-natives [
    if set-word? val [
        e-boot/emit-item/assign ["N_" (to word! val) "_ID"] nat-index
        nat-index: nat-index + 1
    ]
]

e-boot/emit-end

e-boot/emit newline
e-boot/emit-line "typedef struct REBOL_Boot_Block {"

for-each word sections [
    word: form word
    remove/part word 5 ; boot_
    e-boot/emit-line/indent [
        "RELVAL" space (to-c-name word) ";" ;-- can't be REBVAL in C++ build
    ]
]
e-boot/emit-line "} BOOT_BLK;"

;-------------------

e-boot/write-emitted
