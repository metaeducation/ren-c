REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %native-emitters.r
    Type: module
    Name: Native-Emitters
    Rights: --{
        Copyright 2017 Atronix Engineering
        Copyright 2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }--
    License: --{
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }--
    Needs: 2.100.100
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

import <text-lines.reb>


native-info!: make object! [
    ;
    ; Note: The proto is everything, including the SET-WORD! text, native or
    ; native:combinator, and then the spec block.  (Terminology-wise we say
    ; "spec" just refers to the block component.)
    ;
    proto: ~

    name: ~
    exported: ~
    native-type: ~  ; normal, intrinsic, combinator, generic

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
export /extract-native-protos: func [
    return: "Returns block of NATIVE-INFO! objects"
        [block!]
    c-source-file [file!]
    <local> proto name exported native-type
][
    return collect [
        parse3 read:string c-source-file [opt some [
            "//" newline
            "//" space space proto: across [
                (exported: 'no)
                opt ["export" space (exported: 'yes)]
                not ahead space [
                    name: across some "/" ":"  ; things like //:
                    |
                    "/" name: across to ":" one  ; all else is like /foo:
                ]
                space
                opt ["infix" opt [":" to space] space]
                ["native" (native-type: 'normal)
                    opt [":combinator" (native-type: 'combinator)]
                    opt [":intrinsic" (native-type: 'intrinsic)]
                    opt [":generic" (native-type: 'generic)]
                ] space
                "[" thru "//  ]"
            ]
            (
                replace proto unspaced [newline "//"] newline
                replace proto "//  " -{}-

                keep make native-info! compose [
                    proto: (proto)
                    name: (name)
                    exported: (quote exported)
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
export /emit-include-params-macro: func [
    "Emit macros for a native's parameters"

    return: [~]
    e [object!] "where to emit (see %common-emitters.r)"
    proto [text!]
    :extension [text!] "extension name"  ; once used not used now
][
    let native-name: ~
    parse3:match proto [
        opt some newline  ; stripload preserves newlines
        opt ["export" space] [
            native-name: across some "/" ":"  ; e.g. //:
            |
            "/" native-name: across to ":" one  ; everything else is /foo:
        ]
        accept (okay)
    ] else [
        fail "Could not extract native name in emit-include-params-macro"
    ]
    let spec: copy find proto "["  ; make copy (we'll corrupt it)

    replace spec "^^" -{}-

    ; We used stripload to get the function specs, so it has @output form
    ; parameters.  The bootstrap executable thinks that's an illegal email.
    ; So to process these, we replace the @ with nothing
    ;
    ; !!! Review replacing with / when specs are changed, so /(...) and /xxx
    ; will both work.
    ;
    replace spec "@" -{}-

    spec: transcode:one spec

    let paramlist
    if not find proto "native:combinator" [
        paramlist: spec
    ]
    else [
        ; NATIVE:COMBINATOR instances have implicit parameters just
        ; like usermode COMBINATORs do.  We want those implicit
        ; parameters in the ARG() macro list so the native bodies
        ; see them (again, just as the usermode combinators can use
        ; `state` and `input` and `remainder` in their body code)
        ;
        paramlist: collect [  ; no PARSE COLLECT in bootstrap exe :-(
            assert [text? spec.1]
            keep spec.1
            spec: my next

            assert [spec.1 = the return:]
            keep spec.1
            spec: my next

            assert [text? spec.1]  ; description
            keep spec.1
            spec: my next

            assert [block? spec.1]  ; type spec
            keep spec.1
            spec: my next

            keep spread [
                state [frame!]
                input [any-series?]
            ]

            keep spread spec
        ]
    ]

    let is-intrinsic: did find proto "native:intrinsic"

    let n: 1
    let items: collect [
        ;
        ; All natives *should* specify a `return:`, because it's important
        ; to document what the return types are (and HELP should show it).
        ; However, only CHECK_RAW_NATIVE_RETURNS builds actually *type check*
        ; the result; the C code is trusted otherwise to do the correct thing.
        ;
        ; Natives store their return type specification in their Details,
        ; not in their ParamList (the way a FUNC does), because there is no
        ; need for instances of natives to have a RETURN function slot.
        ;
        while [all [
            not tail? paramlist
            text? paramlist.1
        ]][
            paramlist: next paramlist
        ]
        if (the return:) <> paramlist.1 [
            fail [native-name "does not have a RETURN: specification"]
        ]
        paramlist: next paramlist

        for-each 'item paramlist [
            if group? item [
                item: first item
            ]
            if not match [word! get-word3! lit-word3!] item [
                continue  ; note get-word3! is refinement?
            ]

            let param-name: resolve noquote item
            all [
                is-intrinsic
                n = 1  ; first parameter
            ] then [
                keep cscape [
                    param-name "DECLARE_INTRINSIC_PARAM(${PARAM-NAME})"
                ]
            ] else [
                keep cscape [n param-name "DECLARE_PARAM($<n>, ${PARAM-NAME})"]
            ]
            n: n + 1
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
    ; !!! This prevents API use inside natives which is binding-based.  That
    ; is an inconvenience, and if a native wants to do it for expedience then
    ; it needs to clear this bit itself (and set it back when done).
    ;
    let varlist-hold: if is-intrinsic [
        [
            -{if (Not_Level_Flag(level_, DISPATCHING_INTRINSIC))}-
            -{    Set_Flex_Info(level_->varlist, HOLD);}-
        ]
    ] else [
        [-{Set_Flex_Info(level_->varlist, HOLD);}-]
    ]

    if empty? items [  ; vaporization currently not allowed
        insert items "NOOP"
    ]

    let prefix: all [extension unspaced [extension "_"]]
    e/emit [prefix native-name items varlist-hold --{
        #define ${MAYBE PREFIX}INCLUDE_PARAMS_OF_${NATIVE-NAME} \
            $[Varlist-Hold] \
            $(Items); \
    }--]
    e/emit newline
    e/emit newline
]


generic-info!: make object! [
    name: ~
    type: ~
    proper-type: ~  ; propercased type, with Is_XXX

    found: 'no  ; will change to "yes" if it matches a native:generic decl

    file: ~
    line: "???"
    ;
    ; The original parser code was much more complex and was able to track the
    ; LINE number.  UPARSE intends to be able to provide this as a <line>
    ; combinator, but until it's a built-in feature having it in this code
    ; is more complexity than it winds up being worth.
]


; R3-Alpha implemented generics with a large switch() statement inside a
; C function where there was one function per datatype (functions could be
; reused, e.g. GROUP! and BLOCK! both used the same function).  Ren-C takes
; a step toward finer granularity by building tables of many functions.
;
export /extract-generic-implementations: func [
    return: "Returns block of GENERIC-INFO! objects"
        [block!]
    c-source-file [file!]
    <local> name proper-type name* type*
][
    return collect [
        parse3 read:string c-source-file [some [
            "IMPLEMENT_GENERIC" [
                "(" name*: across to "," "," space type*: across to ")" ")"
                thru newline
                (
                    name: uppercase copy name*
                    proper-type: propercase copy type*

                    if (name <> name*) or (proper-type <> type*) [
                        fail [
                            "Bad casing, should be"
                            "IMPLEMENT_GENERIC(" name "," proper-type ")"
                            "and not"
                            "IMPLEMENT_GENERIC(" name* "," type* ")"
                        ]
                    ]

                    lowercase name
                    lowercase type*

                    replace name "_q" "?"  ; use smarter parse rule...
                    replace name "_" "-"

                    replace type* "_" "-"
                    replace type* "is-" void

                    keep make generic-info! compose [
                        name: (name)
                        type: (quote as word! type*)
                        proper-type: (proper-type)
                        file: (c-source-file)
                    ]
                )
                | (fail ["Malformed generic in" c-source-file])
            ]
            | thru newline
        ]]
    ]
]
