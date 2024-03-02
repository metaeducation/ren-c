REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate native specifications"
    File: %make-natives.r
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2020 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
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
        //      /refinement "Refinement description here"
        //  ]
        //
        DECLARE_NATIVE(native_name) {
            INCLUDE_PARAMS_OF_NATIVE_NAME;

            if (REF(refinement)) {
                 int i = VAL_INT32(ARG(argument));
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
        REF() and ARG() macro definitions for more on this.
    }
]

if trap [:import/into] [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>
import <native-emitters.r>

print "------ Generate tmp-natives.r"

src-dir: join repo-dir %src/
output-dir: join system/options/path %prep/
mkdir/deep join output-dir %boot/

verbose: false

all-protos: copy []


;-------------------------------------------------------------------------

output-buffer: make text! 20000


gather-natives: func [
    return: [~]
    dir
][
    files: read dir
    for-each file files [
        file: join dir file
        case [
            dir? file [gather-natives file]
            all [
                %.c = suffix? file
            ][
                append all-protos spread (extract-native-protos file)
            ]
        ]
    ]
]

gather-natives join src-dir %core/


=== "MOVE `NATIVE: NATIVE` and `ENFIX: NATIVE` TO FRONT OF GATHERED LIST" ===

; The construction `native: native [...]` obviously has to be treated in a
; special way.  Startup constructs it manually, before skipping it and invoking
; the evaluator to do the other `xxx: native/yyy [...]` evaluations.

native-proto: null
antiform?-proto: null
logic?-proto: null
action?-proto: null
enfix-proto: null
any-value?-proto: null
any-atom?-proto: null
element?-proto: null
quasi?-proto: null
quoted?-proto: null

info: all-protos
while [not tail? info] [
    case [
        info/1/name = "native" [native-proto: take info]
        info/1/name = "antiform?" [antiform?-proto: take info]
        info/1/name = "logic?" [logic?-proto: take info]
        info/1/name = "action?" [action?-proto: take info]
        info/1/name = "enfix" [enfix-proto: take info]
        info/1/name = "any-value?" [any-value?-proto: take info]
        info/1/name = "any-atom?" [any-atom?-proto: take info]
        info/1/name = "element?" [element?-proto: take info]
        info/1/name = "quasi?" [quasi?-proto: take info]
        info/1/name = "quoted?" [quoted?-proto: take info]
    ]
    else [
        info: next info
    ]
]

assert [native-proto antiform?-proto logic?-proto action?-proto enfix-proto
    any-value?-proto any-atom?-proto element?-proto
    quasi?-proto quoted?-proto]

insert all-protos quoted?-proto  ; will be tenth
insert all-protos quasi?-proto  ; will be ninth
insert all-protos element?-proto  ; will be eighth
insert all-protos any-atom?-proto  ; will be seventh
insert all-protos any-value?-proto  ; will be sixth
insert all-protos enfix-proto  ; will be fifth
insert all-protos logic?-proto  ; will be fourth
insert all-protos action?-proto  ; will be third
insert all-protos antiform?-proto  ; will be second
insert all-protos native-proto  ; so now it's first


=== "MOLD AS TEXT TO BE EMBEDDED IN THE EXECUTABLE AND SCANNED AT BOOT" ===

append output-buffer {REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Native specs"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {This is a generated file.}
]
}

; As a hint in case you come across %tmp-natives.r in your editor, this puts
; comments to warn you not to edit there.  (The comments and newlines are
; removed by the process that does embedding in the EXE.)

for-each info all-protos [
    if info/exported [
        fail "EXPORT is implied on %tmp-natives.r"
    ]
    append output-buffer spaced [
        newline newline
        {; !!! DON'T EDIT HERE, generated from} mold info/file {line} info/line
        newline
    ]
    append output-buffer info/proto
]

append output-buffer unspaced [
    newline
    "~done~  ; C code expects evaluation to end in ~done~" newline
    newline
]

write-if-changed (join output-dir %boot/tmp-natives.r) output-buffer

print [(length of all-protos) "natives"]
print newline

clear output-buffer


=== "GENERATE PROCESSED FILES FOR GENERICS" ===

generic-names: copy []
stripped-generics: stripload/gather (join src-dir %boot/generics.r) inside [] 'generic-names

write-if-changed (join output-dir %boot/tmp-generics-stripped.r) unspaced [
    "[" newline
    stripped-generics
    "~done~  ; C code expects evaluation to end in ~done~" newline
    "]" newline
]

write-if-changed (join output-dir %boot/tmp-generic-names.r) unspaced [
    mold generic-names
    newline
]


=== "EMIT INCLUDE_PARAMS_OF_XXX AUTOMATIC MACROS" ===

; This used to be done in %make-headers.r, but we handle this here because we
; still have the individual specs for the natives on hand.  The generics need
; to be parsed.

mkdir/deep (join output-dir %include/)

e-params: make-emitter "PARAM() and REFINE() Automatic Macros" (
    join output-dir %include/tmp-paramlists.h
)

for-each info all-protos [
    emit-include-params-macro e-params info/proto
]

blockrule: ["[" any [blockrule | not "]" skip] "]"]

parse2 stripped-generics [
    some newline  ; skip newlines
    opt some [
        copy proto [
            thru ":" space "generic" space blockrule
        ]
        (emit-include-params-macro e-params proto)
            |
        skip
    ]
]

e-params/write-emitted


=== "EMIT DECLARE_NATIVE() or DECLARE_INTRINSIC() FORWARD DECLS" ===

e-forward: make-emitter "DECLARE_NATIVE/INTRINSIC() forward decls" (
    join output-dir %include/tmp-native-fwd-decls.h
)

e-forward/emit {
    /*
     * NATIVE PROTOTYPES
     *
     * DECLARE_NATIVE() is a macro expanding so DECLARE_NATIVE(parse) will
     * define a function named `N_parse`.  The prototypes are included in a
     * system-wide header in order to allow recognizing a given native by
     * identity in the C code, e.g.:
     *
     *     if (ACT_DISPATCHER(VAL_ACTION(native)) == &N_parse) { ... }
     *
     * There is also a special subclass of natives known as intrinsics, which
     * are defined via DECLARE_INTRINSIC().  These share a common simple
     * dispatcher based around a C function that can be called without a
     * frame.  See `Intrinsic` vs. `Dispatcher` types for more information.
     */
}
e-forward/emit newline

for-each info all-protos [
    if info/native-type = 'intrinsic [
        e-forward/emit [info {DECLARE_INTRINSIC(${info/name});}]
    ] else [
        e-forward/emit [info {DECLARE_NATIVE(${info/name});}]
    ]
    e-forward/emit newline
]

e-forward/write-emitted  ; wait to see if we actually need it
