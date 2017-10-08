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

do %form-header.r

do %systems.r
args: parse-args system/options/args
config: config-system to-value :args/OS_ID

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

change-dir %../boot/
;dir: %../core/temp/  ; temporary definition
output-dir: fix-win32-path to file! any [:args/OUTDIR %../]
mkdir/deep output-dir/include
mkdir/deep output-dir/boot
mkdir/deep output-dir/core
inc: output-dir/include
src: output-dir/core
boot: output-dir/boot

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
    boot-root
    boot-task
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
    if string? :system/script/args [
        either block? load system/script/args [
            load system/script/args
        ][
            reduce [load system/script/args]
        ]
    ]
    :system/script/args

    ; This is the only piece that should be necessary if not dealing w/legacy
    system/options/args
] [
    fail "No platform specified."
]

product: to-word any [:args/PRODUCT | "core"]

platform-data: context [type: 'windows]
build: context [features: [help-strings]]

;-- Fetch platform specifications:
;init-build-objects/platform platform
;platform-data: platforms/:platform
;build: platform-data/builds/:product


boot-types: load %types.r


emit-header "Dispatchers" %tmp-dispatchers.c
emit newline

emit-line {#include "sys-core.h"}
emit newline

emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "REBACT Value_Dispatch[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/class [
        0 [ ; REB_0 should not ever be dispatched, bad news
            emit-item "NULL"
        ]
        * [ ; Extension types just fail until registered
            emit-item "T_Unhooked"
        ]
    ][ ; All other types should have handlers
        emit-item ["T_" propercase-of really word! type/class]
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end


emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  PATH DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "REBPEF Path_Dispatch[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/path [
        - [emit-item "PD_Fail"]
        + [emit-item ["PD_" propercase-of really word! type/class]]
        * [emit-item "PD_Unhooked"]
    ][
        ; !!! Today's PORT! path dispatches through context even though that
        ; isn't its technical "class" for responding to actions.
        ;
        emit-item ["PD_" propercase-of really word! type/path]
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end


emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  MAKE DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "MAKE_FUNC Make_Dispatch[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/make [
        - [emit-item "MAKE_Fail"]
        + [emit-item ["MAKE_" propercase-of really word! type/class]]
        * [emit-item "MAKE_Unhooked"]
    ][
        fail "MAKE in %types.r should be, -, +, or *"
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end


emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  TO DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "TO_FUNC To_Dispatch[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/make [
        - [emit-item "TO_Fail"]
        + [emit-item ["TO_" propercase-of really word! type/class]]
        * [emit-item "TO_Unhooked"]
    ][
        fail "TO in %types.r should be -, +, or *"
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end


emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  MOLD DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "MOLD_FUNC Mold_Or_Form_Dispatch[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/mold [
        - [emit-item "MF_Fail"]
        + [emit-item ["MF_" propercase-of really word! type/class]]
        * [emit-item "MF_Unhooked"]
    ][
        ; ERROR! may be a context, but it has its own special forming
        ; beyond the class (falls through to ANY-CONTEXT! for mold), and
        ; BINARY! has a different handler than strings
        ;
        emit-item ["MF_" propercase-of really word! type/mold]
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end


emit {
//=////////////////////////////////////////////////////////////////////////=//
//
//  COMPARISON DISPATCHERS (auto-generated, edit in %make-boot.r)
//
///////////////////////////////////////////////////////////////////////////=//
}
emit newline
emit-line "REBCTF Compare_Types[REB_MAX] = {"
for-each-record type boot-types [
    switch/default type/class [
        0 [emit-item "NULL"]
        * [emit-item "CT_Unhooked"]
    ][
        emit-item ["CT_" propercase-of really word! type/class]
    ]
    emit-annotation really [word! integer!] type/name
]
emit-end

write-emitted src/tmp-dispatchers.c


;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-header "Datatype Definitions" %reb-types.h

emit {
/***********************************************************************
**
*/  enum Reb_Kind
/*
**      Internal datatype numbers. These change. Do not export.
**
***********************************************************************/
}
emit-line "{"

datatypes: copy []
n: 0

for-each-record type boot-types [
    append datatypes type/name

    either type/name = 0 [
        emit-item/assign "REB_0" 0
    ][
        emit-item/assign/upper ["REB_" really word! type/name] n
    ]
    emit-annotation n

    n: n + 1
]

emit-item/assign "REB_MAX" n
emit-annotation n
emit-end

emit {
/***********************************************************************
**
**  REBOL Type Check Macros
**
***********************************************************************/
}

new-types: copy []
n: 0
for-each-record type boot-types [
    ;
    ; Type #0 is reserved for special purposes
    ;
    if n != 0 [
        ;
        ; Emit the IS_INTEGER() / etc. test for the datatype.  Use LOGICAL()
        ; so that `REBOOL b = IS_INTEGER(value);` passes the tests in the
        ; build guaranteeing all REBOOL are 1 or 0, despite the fact that
        ; there are other values that C considers "truthy".
        ;
        emit-line [
            {#define IS_} (uppercase to-c-name type/name) "(v)" space
            "LOGICAL(VAL_TYPE(v)==REB_" (uppercase to-c-name type/name) ")"
        ]

        append new-types to-word adjoin form type/name "!"
    ]

    n: n + 1
]

; Emit the macros that embed specific knowledge of the type ordering.  These
; are best kept in the header of %types.r, so anyone changing the order is
; reminded of the importance of adjusting them.
;
types-header: first load/header %types.r
emit trim/auto copy really string! types-header/macros


emit {
/***********************************************************************
**
**  REBOL Typeset Defines
**
***********************************************************************/

// User-facing typesets, such as ANY-VALUE!, do not include void (absence of
// a value) nor the internal "REB_0" type
//
#define TS_VALUE ((FLAGIT_KIND(REB_MAX_VOID) - 1) - FLAGIT_KIND(REB_0))
}

typeset-sets: copy []

for-each-record type boot-types [
    for-each ts compose [(type/typesets)] [
        spot: any [
            select typeset-sets ts
            first back insert tail typeset-sets reduce [ts copy []]
        ]
        append spot type/name
    ]
]
remove/part typeset-sets 2 ; the - markers

for-each [ts types] typeset-sets [
    emit ["#define" space uppercase to-c-name ["TS_" ts] space "("]
    for-each t types [
        emit ["FLAGIT_KIND(" uppercase to-c-name ["REB_" t] ")|"]
    ]
    unemit #"|" ;-- remove the last | added
    emit [")" newline]
]

write-emitted inc/reb-types.h


;----------------------------------------------------------------------------
;
; Bootdefs.h - Boot include file
;
;----------------------------------------------------------------------------

emit-header "Boot Definitions" %bootdefs.h

emit-line [{#define REBOL_VER} space (version/1)]
emit-line [{#define REBOL_REV} space (version/2)]
emit-line [{#define REBOL_UPD} space (version/3)]
emit-line [{#define REBOL_SYS} space (version/4)]
emit-line [{#define REBOL_VAR} space (version/5)]

;-- Generate Canonical Words (must follow datatypes above!) ------------------

emit {
/***********************************************************************
**
*/  enum REBOL_Symbols
/*
**      REBOL static canonical words (symbols) used with the code.
**
***********************************************************************/
}
emit-line "{"
emit-item/assign "SYM_0" 0

n: 0
boot-words: copy []
add-word: func [
    ; LEAVE is not available in R3-Alpha compatibility PROC
    ; RETURN () is not legal in R3-Alpha compatibility FUNC (no RETURN: [...])
    ; Make it a FUNC and just RETURN blank to appease both

    word
    /skip-if-duplicate
    /type
][
    ;
    ; Horribly inefficient linear search, but MAP! in R3-Alpha is unreliable
    ; and implemented differently, and we want this code to work in it too.
    ;
    if find boot-words word [
        if skip-if-duplicate [return blank]
        fail ["Duplicate word specified" word]
    ]

    ; Although TO-C-NAME is used on the SYM_XXX string as a whole, in order
    ; to get names like SYM_ELLIPSIS the escaping has to be done on the
    ; individual word first in this case, as opposed to something more generic
    ; which would also turn SYM_.a. into "SYM__DOTA_DOT" (or similar) 
    ;
    emit-item/upper ["SYM_" (to-c-name word)]
    emit-annotation spaced [n "-" word]
    n: n + 1

    ; The types make a SYM_XXX entry, but they're kept in a separate block
    ; in the boot object (see `boot-types` in `sections`)
    ;
    ; !!! Update--temporarily to make word numbering easier, they are
    ; duplicated.  Consider a better way longer term.
    ;
    append boot-words word ;-- was `unless type [...]`
    return blank
]

for-each-record type boot-types [
    if n = 0 [n: n + 1 | continue]

    add-word/type to-word unspaced [to-string type/name "!"]
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

emit-end

print [n "words + actions"]

write-emitted inc/tmp-bootdefs.h

;----------------------------------------------------------------------------
;
; Sysobj.h - System Object Selectors
;
;----------------------------------------------------------------------------

emit-header "System Object" %sysobj.h
emit newline

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

make-obj-defs: procedure [obj prefix depth /selfless] [
    prefix: uppercase-of prefix
    emit-line ["enum " prefix "object {"]

    either selfless [
        ;
        ; Make sure *next* value starts at 1.  Keys/vars in contexts start
        ; at 1, and if there's no "userspace" self in the 1 slot, the first
        ; key has to be...so we make `SYS_CTX_0 = 0` (for instance)
        ;
        emit-item/assign [prefix "0"] 0
    ][
        ; The internal generator currently puts SELF at the start of new
        ; objects in key slot 1, by default.  Eventually MAKE OBJECT! will
        ; have nothing to do with adding SELF, and it will be entirely a
        ; by-product of generators.
        ;
        emit-item/assign [prefix "SELF"] 1
    ]

    for-each field words-of obj [
        emit-item/upper [prefix field]
    ]
    emit-item [prefix "MAX"]
    emit-end

    if depth > 1 [
        for-each field words-of obj [
            if all [
                field != 'standard
                object? get in obj field
            ][
                extended-prefix: uppercase to-c-name [prefix field "_"]
                make-obj-defs obj/:field extended-prefix (depth - 1)
            ]
        ]
    ]
]

make-obj-defs ob "SYS_" 1
make-obj-defs ob/catalog "CAT_" 4
make-obj-defs ob/contexts "CTX_" 4
make-obj-defs ob/standard "STD_" 4
make-obj-defs ob/state "STATE_" 4
;make-obj-defs ob/network "NET_" 4
make-obj-defs ob/ports "PORTS_" 4
make-obj-defs ob/options "OPTIONS_" 4
;make-obj-defs ob/intrinsic "INTRINSIC_" 4
make-obj-defs ob/locale "LOCALE_" 4
make-obj-defs ob/view "VIEW_" 4

write-emitted inc/tmp-sysobj.h


;----------------------------------------------------------------------------
;
; Event Types
;
;----------------------------------------------------------------------------

emit-header "Event Types" %reb-evtypes.h
emit newline

emit-line ["enum event_types {"]
for-each field ob/view/event-types [
    emit-item/upper ["EVT_" field]
]
emit-item "EVT_MAX"
emit-end

emit-line ["enum event_keys {"]
emit-item "EVK_NONE"
for-each field ob/view/event-keys [
    emit-item/upper ["EVK_" field]
]
emit-item "EVK_MAX"
emit-end

write-emitted inc/reb-evtypes.h


;----------------------------------------------------------------------------
;
; Error Constants
;
;----------------------------------------------------------------------------

;-- Error Structure ----------------------------------------------------------

emit-header "Error Structure and Constants" %errnums.h

emit {
/***********************************************************************
**
*/  typedef struct REBOL_Error_Vars
/*
***********************************************************************/
}
emit-line "{"

; Generate ERROR object and append it to bootdefs.h:
emit-line/indent "RELVAL self;" ;-- C++ build cannot be REBVAL
for-each word words-of ob/standard/error [
    either word = 'near [
        emit-line/indent [
            "RELVAL nearest;" ;-- C++ build cannot be REBVAL
        ]
        emit-annotation "near/far are non-standard C keywords"
    ][
        emit-line/indent [
            "RELVAL" space (to-c-name word) ";" ;-- C++ build cannot be REBVAL
         ]
    ]
    
]
emit-line "} ERROR_VARS;"

emit {
/***********************************************************************
**
*/  enum REBOL_Errors
/*
***********************************************************************/
}
emit-line "{"

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
            emit-item/assign/upper ["RE_" id] code
            new-section: false
        ][
            emit-item/upper ["RE_" id]
        ]
        emit-annotation spaced [code mold val]

        code: code + 1
    ]
    emit-item ["RE_" (uppercase-of to word! category) "_MAX"]
    emit newline
]

emit-end

emit-line {#define RE_USER MAX_I32}
emit-annotation {Hardcoded, update in %make-boot.r}

emit-line {#define RE_CATEGORY_SIZE 1000}
emit-annotation {Hardcoded, update in %make-boot.r}

emit-line {#define RE_INTERNAL_FIRST RE_MISC}
emit-annotation {GENERATED! update in %make-boot.r}

emit-line {#define RE_MAX RE_COMMAND_MAX}
emit-annotation {GENERATED! update in %make-boot.r}
write-emitted inc/tmp-errnums.h

;-------------------------------------------------------------------------
emit-header "Error functions" %error-funcs.h
for-each [id val] id-list [
    n-args: 0
    if block? val [
        parse val [
            any [
                get-word! (
                    n-args: n-args + 1 ; don't use ++, not R3-Alpha compatible
                )
                | skip
            ]
        ]
    ]

    emit-line []
    emit-line ["//  " mold val] 
    c-id: to-c-name id
    f-name: uppercase/part copy c-id 1
    parse f-name [
        any [
            #"_" w: (uppercase/part w 1)
            | skip
        ]
    ]
    either zero? n-args [
        emit-line [ {static inline REBCTX *Error_} f-name {_Raw(void)}]
        emit-line [ "^{" ]
        emit-line/indent [ "return Error(RE_" uppercase c-id ", END);" ]
        emit-line [ "^}" ]
    ][
        emit-line [ {static inline REBCTX *Error_} f-name {_Raw(} ]
        i: 0
        while [i < n-args] [
            emit-line compose [ {const RELVAL *arg} (i + 1)
                either i < (n-args - 1) [","] [""]
            ]
            i: i + 1 ; don't use ++, not R3-Alpha compatible 
        ]
        emit-line [")"]
        emit-line [ "^{" ]

        args: copy ""
        i: 0
        while [i < n-args] [
            append args compose [ {, arg} (i + 1)]
            i: i + 1 ; don't use ++, not R3-Alpha comptible
        ]

        emit-line/indent [ "return Error(RE_" uppercase c-id args ", END);"]
        emit-line [ "^}" ]
    ]
]
write-emitted inc/tmp-error-funcs.h

;-------------------------------------------------------------------------

emit-header "Port Modes" %port-modes.h

data: load %modes.r

emit newline
emit-line "enum port_modes {"

for-each word data [
    emit-item/upper word
]
emit-end

write-emitted inc/tmp-portmodes.h

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

emit-header "Sys Context" %sysctx.h

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
make-obj-defs/selfless sctx "SYS_CTX_" 1

write-emitted inc/tmp-sysctx.h


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

emit-header "Natives and Bootstrap" %tmp-boot-block.c
emit newline
emit-line {#include "sys-core.h"}
emit newline

externs: make string! 2000
boot-natives: load boot/tmp-natives.r
num-natives: 0

for-each val boot-natives [
    if set-word? val [
        num-natives: num-natives + 1
    ]
]

print [num-natives "natives"]

emit newline

emit-line {REBVAL Natives[NUM_NATIVES];}

emit-line "const REBNAT Native_C_Funcs[NUM_NATIVES] = {"

for-each val boot-natives [
    if set-word? val [
        emit-item ["N_" to word! val]
    ]
]
emit-end
emit newline


;-- Build typespecs block (in same order as datatypes table):

boot-typespecs: make block! 100
specs: load %typespec.r
for-each type datatypes [
    if type = 0 [continue]
    verify [spec: select specs type]
    append/only boot-typespecs spec
]

;-- Create main code section (compressed):

boot-types: new-types
boot-root: load %root.r
boot-task: load %task.r

write-if-changed boot/tmp-boot-block.r mold reduce sections
data: mold/flat reduce sections
insert data reduce ["; Copyright (C) REBOL Technologies " now newline]
insert tail data make char! 0 ; scanner requires zero termination

comp-data: compress data: to-binary data

emit {
// Native_Specs contains data which is the DEFLATE-algorithm-compressed
// representation of the textual function specs for Rebol's native
// routines.  Though DEFLATE includes the compressed size in the payload,
// NAT_UNCOMPRESSED_SIZE is also defined to be used as a sanity check
// on the decompression process.
}
emit newline

emit-line ["const REBYTE Native_Specs[NAT_COMPRESSED_SIZE] = {"]

;-- Convert UTF-8 binary to C-encoded string:
emit binary-to-c comp-data
emit-line "};" ;-- EMIT-END would erase the last comma, but there's no extra

write-emitted src/tmp-boot-block.c

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

emit-header "Bootstrap Structure and Root Module" %boot.h

emit newline

emit-line ["#define NUM_NATIVES" space num-natives]
emit-line ["#define NAT_UNCOMPRESSED_SIZE" space (length-of data)]
emit-line ["#define NAT_COMPRESSED_SIZE" space (length-of comp-data)]
emit-line ["#define CHECK_TITLE" space (checksum to binary! title)]

emit {
// Compressed data of the native specifications.  This is uncompressed during
// boot and executed.
//
extern const REBYTE Native_Specs[NAT_COMPRESSED_SIZE];

// Raw C function pointers for natives.
//
extern const REBNAT Native_C_Funcs[NUM_NATIVES];

// A canon FUNCTION! REBVAL of the native, accessible by the native's index #.
//
extern REBVAL Natives[NUM_NATIVES];
}

emit newline
emit-line "enum Native_Indices {"

nat-index: 0
for-each val boot-natives [
    if set-word? val [
        emit-item/assign ["N_" (to word! val) "_ID"] nat-index
        nat-index: nat-index + 1
    ]
]

emit-end

emit newline
emit-line "typedef struct REBOL_Boot_Block {"

for-each word sections [
    word: form word
    remove/part word 5 ; boot_
    emit-line/indent [
        "RELVAL" space (to-c-name word) ";" ;-- can't be REBVAL in C++ build
    ]
]
emit "} BOOT_BLK;"

;-------------------

emit [newline newline]

emit-line {//**** ROOT Vars (GC protected special values):}
emit newline

emit-line "typedef struct REBOL_Root_Vars {"

for-each word boot-root [
    emit-line/indent [
        "RELVAL" space (to-c-name word) ";" ;-- Can't be REBVAL in C++ build
    ]
]
emit-line ["} ROOT_VARS;"]
emit newline

n: 0
for-each word boot-root [
    emit-line [
        "#define" space (uppercase to-c-name ["ROOT_" word]) space
        "KNOWN(&Root_Vars->" (lowercase to-c-name word) ")"
    ]
    n: n + 1
]
emit-line ["#define ROOT_MAX" space n]

;-------------------

emit [newline newline]

emit-line {//**** TASK Vars (GC protected special values)}
emit newline

emit-line "typedef struct REBOL_Task_Vars {"

for-each word boot-task [
    emit-line/indent [
        "RELVAL" space (to-c-name word) ";" ;-- Can't be REBVAL in C++ build
    ]
]
emit-line ["} TASK_VARS;"]
emit newline

n: 0
for-each word boot-task [
    emit-line [
        "#define" space (uppercase to-c-name ["TASK_" word]) space
        "KNOWN(&Task_Vars->" (lowercase to-c-name word) ")"
    ]
    n: n + 1
]
emit-line ["#define TASK_MAX" space n]

write-emitted inc/tmp-boot.h
