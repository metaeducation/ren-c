Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Generate native specifications"
    file: %make-natives.r
    rights: --[
        Copyright 2012 REBOL Technologies
        Copyright 2012-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    description: --[
        "Natives" are Rebol functions whose implementations are C code (as
        opposed to blocks of user code, such as that made with FUNC).

        Though their bodies are C, native specifications are Rebol blocks.
        For convenience that Rebol code is kept in the same file as the C
        definition--in a C comment block.

        Typically the declarations wind up looking like this:

        //
        //  native-name: native [
        //
        //  "Description of native would go here"
        //
        //      return: "Return description here"
        //          [integer!]
        //      argument "Argument description here"
        //          [text!]
        //      :refinement "Refinement description here"
        //  ]
        //
        DECLARE_NATIVE(NATIVE_NAME) {
            INCLUDE_PARAMS_OF_NATIVE_NAME;

            if (Bool_ARG(REFINEMENT)) {
                 int i = VAL_INT32(ARG(ARGUMENT));
                 /* etc, etc. */
            }
            return OUT;
        }

        (Note that the C name of the native may need to be different from the
        Rebol native; e.g. above the `-` cannot be part of a name in C, so
        it gets converted to `_`.  See TO-C-NAME for the logic of this.)

        In order for these specification blocks to be loaded along with the
        function when the interpreter is built, a step in the build has to
        scan through the files to scrape them out of the comments.  This
        file implements that step, which recursively searches %src/core for
        any `.c` files.  Similar code exists for processing "extensions",
        which also implement C functions for natives.

        Not only does the text for the spec have to be extracted, but the
        `INCLUDE_PARAMS_OF_XXX` macros are generated to give a more readable
        way of accessing the parameters than by numeric index.  See the
        Bool_ARG() and ARG() macro definitions for more on this.
    ]--
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>
import <native-emitters.r>

print "------ Generate tmp-natives.r"

src-dir: join repo-dir %src/
output-dir: join system.options.path %prep/
mkdir:deep join output-dir %boot/

natives: copy []
generics: copy []


;-------------------------------------------------------------------------

output-buffer: make text! 20000


gather-natives: func [
    return: [~]
    dir
][
    let files: read dir
    for-each 'file files [
        file: join dir file
        case [
            dir? file [gather-natives file]
            all [
                %.c = suffix-of file
            ][
                append natives spread (extract-native-protos file)
                append generics spread (extract-generic-implementations file)
            ]
        ]
    ]
]

gather-natives join src-dir %core/


=== "MOVE `/NATIVE: NATIVE` AND TYPE CONSTRAINTS TO START OF BOOT" ===

; The construction `native: native [...]` obviously has to be treated in a
; special way.  Startup constructs it manually, before skipping it and invoking
; the evaluator to do the other `xxx: native:yyy [...]` evaluations.

leaders: [
    native
    logic?
    moldify  ; want this early so PROBE() works as early as it can!
    antiform?  ; needs to accept unstable antiforms, overwrites auto-gen case
    infix
    any-value?
    any-atom?
    set-word?
    get-word?
    set-tuple?
    get-tuple?
    set-group?
    get-group?
    get-block?
    set-block?
]
leader-protos: to map! []

pos: natives
while [not tail? pos] [
    assert [text? pos.1.name]  ; allows native names illegal in bootstrap
    let name: to word! pos.1.name  ; extract to avoid left-before-right eval
    if find leaders name [
        leader-protos.(name): take pos  ; (pos.1.name) would eval after TAKE!
    ] else [
        pos: next pos
    ]
]

insert natives spread collect [
    for-each 'l leaders [
        if not leader-protos.(l) [
            fail ["Did not find" l "to put at head of natives list"]
        ]
        keep leader-protos.(l)
    ]
]


=== "MOLD AS TEXT TO BE EMBEDDED IN THE EXECUTABLE AND SCANNED AT BOOT" ===

append output-buffer ---[Rebol [
    System: "Rebol [R3] Language Interpreter and Run-time Environment"
    Title: "Native specs"
    Rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    License: --[
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    Note: "!!! THIS FILE IS GENERATED BY CODE - DO NOT EDIT HERE !!!"
]
]---

; As a hint in case you come across %tmp-natives.r in your editor, this puts
; comments to warn you not to edit there.  (The comments and newlines are
; removed by the process that does embedding in the EXE.)

for-each 'info natives [
    if yes? info.exported [
        fail "EXPORT is implied on %tmp-natives.r"
    ]
    append output-buffer spaced [
        newline newline
        -[; !!! DON'T EDIT HERE, generated from]- info.file -[line]- info.line
        newline
    ]
    append output-buffer info.proto
]

append output-buffer unspaced [
    newline
    "~okay~  ; C code checks for this eval product" newline
    newline
]

write-if-changed (join output-dir %boot/tmp-natives.r) output-buffer

print [(length of natives) "natives"]
print newline

clear output-buffer


=== "EMIT INCLUDE_PARAMS_OF_XXX AUTOMATIC MACROS" ===

; This used to be done in %make-headers.r, but we handle this here because we
; still have the individual specs for the natives on hand.

mkdir:deep (join output-dir %include/)

e-params: make-emitter "PARAM() and REFINE() Automatic Macros" (
    join output-dir %include/tmp-paramlists.h
)

for-each 'info natives [
    emit-include-params-macro e-params info.proto
]

e-params/write-emitted


=== "EMIT DECLARE_NATIVE() FORWARD DECLS" ===

e-forward: make-emitter "DECLARE_NATIVE() forward decls" (
    join output-dir %include/tmp-native-fwd-decls.h
)

e-forward/emit [--[
    /*
     * NATIVE PROTOTYPES
     *
     * DECLARE_NATIVE() is a macro expanding so DECLARE_NATIVE(PARSE) will
     * define a function named `N_parse`.  The prototypes are included in a
     * system-wide header so you can call a native dispatcher directly if
     * you need to.
     */
]--]

for-each 'info natives [
    e-forward/emit [info -[DECLARE_NATIVE(${INFO.NAME});]-]
]

e-forward/write-emitted


=== "LOAD TYPESET BYTE MAPPING" ===

; %make-types.r creates a table of TypesetByte, like:
;
;    blank 1
;    integer 2
;    decimal 3
;    ...
;    any-list 90
;    any-bindable 91
;    any-element 92
;
; The generic registry uses this, sorting the more specific values used in
; IMPLEMENT_GENERIC() first in the table for each generic.

name-to-typeset-byte: load3 (join output-dir %boot/tmp-typeset-bytes.r)


=== "SORT GATHERED GENERICS IN TYPESET-BYTE ORDER" ===

sort:compare generics func [a b] [
    let bad: null
    if a.name < b.name [return okay]
    let a-byte: (select name-to-typeset-byte a.type) else [
        bad: a
    ]
    let b-byte: (select name-to-typeset-byte b.type) else [
        bad: b
    ]
    if bad [
        fail [
            "Unknown builtin typeset (did you forget `any_`?)?" newline
            "IMPLEMENT_GENERIC(" bad.name bad.type ") in" bad.file
        ]
    ]
    if a-byte < b-byte [return okay]
    return null
]


=== "EMIT IMPLEMENT_GENERIC() FORWARD DECLS" ===

e-forward: make-emitter "IMPLEMENT_GENERIC() forward decls" (
    join output-dir %include/tmp-generic-fwd-decls.h
)

for-each 'info generics [
    e-forward/emit [info
        -[IMPLEMENT_GENERIC(${INFO.NAME}, ${Info.Proper-Type});]-
    ]
]

e-forward/emit newline

; Every generic has an array of type byte + function pointer entries, that is
; terminated with a zero byte.  We need to forward declare those as well.

for-each 'info natives [
    if info.native-type != 'generic [continue]

    e-forward/emit [info -[
        /* ${INFO.NAME} ExtraGenericInfo fragments contributed by extensions */
        extern GenericInfo const g_generic_${INFO.NAME}_info[];
        extern GenericTable g_generic_${INFO.NAME};  /* pairs info with extra */
    ]-]
]

e-forward/write-emitted


=== "EMIT GENERIC DISPATCH TABLES" ===

e-tables: make-emitter "GENERIC DISPATCH TABLES" (
    join output-dir %core/tmp-generic-tables.c
)

e-tables/emit -[
    #include "sys-core.h"
    /* #include "tmp-generic-fwd-decls.h" */  // separate this out?
]-

for-each 'n-info natives [
    if n-info.native-type != 'generic [continue]

    let entries: collect [
        let last-byte: null
        for-each 'g-info generics [
            assert [text? g-info.name, text? n-info.name]

            if g-info.name != n-info.name [continue]

            let byte: select name-to-typeset-byte g-info.type
            assert [byte]  ; sort phase should have complained if not found

            if byte = last-byte [  ; sorted, so only have to check last byte
                fail [
                    "Multiple IMPLEMENT_GENERIC(" g-info.name g-info.type ")"
                ]
            ]
            last-byte: byte

            g-info.found: 'yes

            keep trim:tail cscape [g-info proper-type -[
                {$<byte>, &GENERIC_CFUNC(${G-INFO.NAME}, ${G-Info.Proper-Type})}
            ]-]
        ]
        keep "{0, nullptr}"
    ]

    e-tables/emit [n-info -[
        const GenericInfo g_generic_${N-INFO.NAME}_info[] = {
            $(Entries),
        };

        GenericTable g_generic_${N-INFO.NAME} = {
            g_generic_${N-INFO.NAME}_info,  /* immutable field, fixed array */
            nullptr  /* mutable field (linked list of ExtraGenericInfo*) */
        };
    ]-]
]

for-each 'info generics [
    if info.found <> 'yes [
        fail [
            "Did not find generic to implement:" info.name newline
            "Definition is in file:" info.file
        ]
    ]
]

e-tables/write-emitted
