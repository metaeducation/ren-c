REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %native-emitters.r
    Type: module
    Name: Native-Emitters
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

if not find words of :import [product] [  ; See %import-shim.r
    do load append copy system/script/path %import-shim.r
]

import <bootstrap-shim.r>
import <common.r>
import <common-emitter.r>

import <text-lines.reb>


native-info!: make object! [
    ;
    ; Note: The proto is everything, including the SET-WORD! text, native or
    ; native/combinator, and then the spec block.  (Terminology-wise we say
    ; "spec" just refers to the block component.)
    ;
    proto: ~

    name: ~
    exported: ~
    combinator: ~

    file: ~
    line: "???"
    ;
    ; The original parser code was much more complex and was able to track the
    ; LINE number.  UPARSE intends to be able to provide this as a <line>
    ; combinator, but until it's a built-in feature having it in this code
    ; is more complexity than it winds up being worth.
]


; This routine originally gathered native prototypes in their loaded forms.
; While that's nice, it created an unfortunate "lock-in" that the bootstrap
; executable would have to have a compatible source format.  The niceness of
; loading turned out to be less practically useful than the malleability.
;
; Hence this instead extracts a tiny bit of information, and returns the spec
; as a text blob.
;
export extract-native-protos: func [
    return: "Returns block of NATIVE-INFO! objects"
        [block!]
    c-source-file [file!]
    <local> proto name exported
][
    return collect [
        parse read/string c-source-file [while [
            "//" newline
            "//" space space copy proto [
                (exported: false)
                opt ["export" space (exported: true)]
                ahead not space copy name to ":" skip space
                opt ["enfix" space]
                (combinator: false)
                ["native" opt "/combinator" (combinator: true)] space
                "[" thru "//  ]"
            ]
            (
                replace/all proto "//  " {}
                replace/all proto "//" {}

                keep make native-info! compose [
                    proto: (proto)
                    name: (name)
                    exported: (exported)
                    file: (c-source-file)
                ]
            )
                |
            thru newline
                |
            skip  ; in case file doesn't end in newline
        ]]
    ]
]


; This routine has to deal with differences between specs the bootstrap EXE
; can load and what the modern Ren-C can load.  The easiest way to do this is
; to take in the textual spec, "massage" it in a way that doesn't destroy the
; information being captured, and then LOAD it.
;
export emit-include-params-macro: function [
    "Emit macros for a native's parameters"

    return: <none>
    e [object!] "where to emit (see %common-emitters.r)"
    proto [text!]
    /ext [text!] "extension name"
][
    seen-refinement: false

    native-name: ~
    parse proto [opt ["export" space] copy native-name to ":" to end] else [
        fail "Could not extract native name in emit-include-params-macro"
    ]
    spec: copy find proto "["  ; make copy (we'll corupt it)

    replace/all spec "^^" {}
    replace/all spec "@" {}

    spec: load-value spec

    if not find proto "native/combinator" [
        paramlist: spec
    ]
    else [
        ; NATIVE-COMBINATOR instances have implicit parameters just
        ; like usermode COMBINATORs do.  We want those implicit
        ; parameters in the ARG() macro list so the native bodies
        ; see them (again, just as the usermode combinators can use
        ; `state` and `input` and `remainder` in their body code)
        ;
        paramlist: collect [  ; no PARSE COLLECT in bootstrap exe :-(
            assert [text? spec/1]
            keep ^ spec/1
            spec: my next

            assert [spec/1 = the return:]
            keep ^ spec/1
            spec: my next

            assert [text? spec/1]  ; description
            keep ^ spec/1
            spec: my next

            assert [block? spec/1]  ; type spec
            keep ^ spec/1
            spec: my next

            keep [
                remainder: [<opt> any-series!]

                state [frame!]
                input [any-series!]
            ]

            keep spec
        ]
    ]

    n: 1
    items: try collect* [
        ;
        ; All natives *should* specify a `return:`, because it's important
        ; to document what the return types are (and HELP should show it).
        ; However, only the debug build actually *type checks* the result;
        ; the C code is trusted otherwise to do the correct thing.
        ;
        ; By convention, definitional returns are the first argument.  (It is
        ; not currently enforced they actually be first in the parameter list
        ; the user provides...so making actions may have to shuffle the
        ; position.  But it may come to be enforced for efficiency).
        ;
        loop [text? :paramlist/1] [paramlist: next paramlist]
        if (the return:) <> :paramlist/1 [
            fail [native-name "does not have a RETURN: specification"]
        ] else [
            keep {PARAM(1, return)}
            keep {USED(ARG(return))}  ; Suppress warning about not using return
            n: n + 1
            paramlist: next paramlist
        ]

        for-each item paramlist [
            if not match [any-word! refinement! lit-word!] item [
                continue
            ]

            param-name: as text! to word! noquote item
            keep cscape/with {PARAM($<n>, ${param-name})} [n param-name]
            n: n + 1
        ]
    ]

    prefix: try if ext [unspaced [ext "_"]]
    e/emit [prefix native-name items] {
        #define ${PREFIX}INCLUDE_PARAMS_OF_${NATIVE-NAME} \
            $[Items]; \
            assert(GET_SERIES_INFO(frame_->varlist, HOLD))
    }
    e/emit newline
    e/emit newline
]
