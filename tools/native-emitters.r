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

if not find (words of :import) 'into [  ; See %import-shim.r
    do <import-shim.r>
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
; as a text string.
;
export extract-native-protos: func [
    return: "Returns block of NATIVE-INFO! objects"
        [block!]
    c-source-file [file!]
    <local> proto name exported
][
    return collect [
        parse3 read/string c-source-file [opt some [
            "//" newline
            "//" space space proto: across [
                (exported: false)
                opt ["export" space (exported: true)]
                not ahead space name: across to ":" one space
                opt ["enfix" space]
                ["native" (native-type: 'normal)
                    opt ["/combinator" (native-type: 'combinator)]
                    opt ["/intrinsic" (native-type: 'intrinsic)]
                ] space
                "[" thru "//  ]"
            ]
            (
                replace/all proto "//  " {}
                replace/all proto "//" {}

                keep make native-info! compose [
                    proto: (proto)
                    name: (name)
                    exported: (reify-logic exported)
                    file: (c-source-file)
                    native-type: the (native-type)
                ]
            )
                |
            thru newline
                |
            one  ; in case file doesn't end in newline
        ]]
    ]
]


; This routine has to deal with differences between specs the bootstrap EXE
; can load and what the modern Ren-C can load.  The easiest way to do this is
; to take in the textual spec, "massage" it in a way that doesn't destroy the
; information being captured, and then LOAD it.
;
export emit-include-params-macro: func [
    "Emit macros for a native's parameters"

    return: [~]
    e [object!] "where to emit (see %common-emitters.r)"
    proto [text!]
    /ext [text!] "extension name"  ; once used in extensions, not used now
][
    if find proto "native/intrinsic" [
        return ~  ; intrinsics don't have INCLUDE_PARAMS_OF macros
    ]

    let seen-refinement: false

    let native-name: ~
    parse3/match proto [
        opt some newline  ; stripload preserves newlines
        opt ["export" space] native-name: across to ":" to <end>
    ] else [
        fail "Could not extract native name in emit-include-params-macro"
    ]
    let spec: copy find proto "["  ; make copy (we'll corrupt it)

    replace/all spec "^^" {}

    ; We used stripload to get the function specs, so it has @output form
    ; parameters.  The bootstrap executable thinks that's an illegal email.
    ; So to process these, we replace the @ with # to get ISSUE!.
    ;
    let output-param?: :issue?
    let output-param!: issue!
    replace/all spec "@" {#}

    spec: load-value spec

    let paramlist
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
            keep spec/1
            spec: my next

            assert [spec/1 = the return:]
            keep spec/1
            spec: my next

            assert [text? spec/1]  ; description
            keep spec/1
            spec: my next

            assert [block? spec/1]  ; type spec
            keep spec/1
            spec: my next

            keep spread compose [
                (to output-param! 'remainder) [any-series?]

                state [frame!]
                input [any-series?]
            ]

            keep spread spec
        ]
    ]

    let n: 1
    let items: collect* [
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
        while [text? :paramlist/1] [paramlist: next paramlist]
        if (the return:) <> :paramlist/1 [
            fail [native-name "does not have a RETURN: specification"]
        ] else [
            keep {DECLARE_PARAM(1, return)}
            keep {USED(ARG(return))}  ; Suppress warning about not using return
            n: n + 1
            paramlist: next paramlist
        ]

        for-each item paramlist [
            if not match [&any-word? &refinement? &lit-word? output-param!] item [
                continue
            ]

            let param-name: as text! to word! noquote item
            keep cscape [n param-name {DECLARE_PARAM($<n>, ${param-name})}]
            n: n + 1

            if output-param? item [
                ;
                ; Used to be special handling here...but that's not needed
                ; currently.  What might be done?
            ]
        ]
    ]

    ; We need `Set_Flex_Info(level_varlist, HOLD)` here because native code
    ; trusts that type checking has ensured it won't get bits in its argument
    ; slots that the C won't recognize.  Usermode code that gets its hands on
    ; a native's FRAME! (e.g. for debug viewing) can't be allowed to change
    ; the frame values to other bit patterns out from under the C or it could
    ; result in a crash.  The native itself doesn't care because it's not
    ; using ordinary variable assignment.
    ;
    ; !!! This prevents API use inside natives which is specifier-based.  That
    ; is an inconvenience, and if a native wants to do it for expedience then
    ; it needs to clear this bit itself (and set it back when done).
    ;
    let prefix: all [ext unspaced [ext "_"]]
    e/emit [prefix native-name items {
        #define ${MAYBE PREFIX}INCLUDE_PARAMS_OF_${NATIVE-NAME} \
            Set_Flex_Info(level_->varlist, HOLD); \
            $[Items]; \
    }]
    e/emit newline
    e/emit newline
]
