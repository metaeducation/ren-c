Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Generate auto headers"
    file: %native-emitters.r
    type: module
    name: Native-Emitters
    rights: --[
        Copyright 2017 Atronix Engineering
        Copyright 2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    needs: 2.100.100
]

if not find (words of import/) 'into [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

import <common.r>
import <common-emitter.r>

import <text-lines.r>


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
export extract-native-protos: func [
    "Returns block of NATIVE-INFO! objects"
    return: [block!]
    c-source-file [file!]
][
    let [proto name exported native-type]

    assert [not dir? c-source-file]
    return collect [
        parse3 read:string c-source-file [opt some [
            "//" newline
            "//" space space proto: across [
                (exported: 'no)
                opt ["export" space (exported: 'yes)]
                not ahead space [
                    name: across some "/" ":"  ; things like //:
                    |
                    name: across to ":" one  ; all else is like foo:
                ]
                space
                opt ["ghostable" space]
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
                replace proto "//  " -[]-

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


; The bootstrap executable loads {...} as a text string.  In function specs
; we use FENCE! for locals list.  But all we want here are the words.  If we
; find a genuine locals-looking FENCE! at the tail of the spec, remove
; the braces around it so we just have the words.
;
textually-splice-last-fence: proc [
    spec [text!]
][
    let pos: find-last spec "}"
    if pos [
        if parse3 next pos [

            opt some [space | newline] "]" accept (okay)
            | accept (null)
        ][
            take pos
            take find-reverse pos "{"
        ] else [
            ; This happens if you have "{}" or other braces in strings in
            ; the spec.  If some locals aren't showing up check here why.
        ]
    ]
]


; EMIT-INCLUDE-PARAMS-MACRO
;
; This routine has to deal with differences between specs the bootstrap EXE
; can load and what the modern Ren-C can load.  The easiest way to do this is
; to take in the textual spec, "massage" it in a way that doesn't destroy the
; information being captured, and then TRANSCODE it.
;
;  1. Currently we do not need the information from $XXX or @XXX or ^XXX when
;     processing parameters to make the include macros.  So we just strip
;     those characters out.
;
;  2. All natives *should* specify a `return:`, because it's important to
;     document what the return types are (and HELP should show it).  But only
;     CHECK_RAW_NATIVE_RETURNS builds actually *type check* the result; the C
;     code is trusted otherwise to do the correct thing.
;
;     Natives store their return type specification in their Details, not in
;     their ParamList (the way a FUNC does), because there is no need for
;     instances of natives to have a RETURN function slot.
;
;     (There are two exceptions: The NATIVE native itelf and TWEAK, in
;     versions used during system boot.)
;
;  3. NATIVE:COMBINATOR instances have implicit parameters just like usermode
;     COMBINATORs do.  We want those implicit parameters in ARG() macro list
;     so the native bodies see them (again, just as the usermode combinators
;     can use `state` and `input` in their body code)
;
;  4. We need `Set_Flex_Info(level_varlist, HOLD)` here because native code
;     trusts that type checking has ensured it won't get bits in its argument
;     slots that the C won't recognize.  Usermode code that gets its hands on
;     a native's FRAME! (e.g. for debug viewing) can't be allowed to change
;     the frame values to other bit patterns out from under the C or it could
;     result in a crash.  The native itself doesn't care because it's not
;     using ordinary variable assignment.
;
;     !!! This prevents API use inside natives which is binding-based.  That
;     is an inconvenience, and if a native wants to do it for expedience then
;     it needs to clear this bit itself (and set it back when done).
;
export emit-include-params-macro: func [
    "Return block of symbols for macros that access a native's parameters"

    return: [block!]
    e "where to emit (see %common-emitters.r)"
        [object!]
    proto [text!]
    :extension "extension name (not currently in use)"
        [text!]
][
    let native-name: "<unknown>"

    let panic: proc [reason] [
        print mold proto
        lib/panic:blame [native-name spaced reason] $e
    ]

    proto: stripload proto  ; remove comments

    parse3:match proto [
        opt some newline  ; stripload preserves newlines
        opt ["export" space] [
            native-name: across some "/" ":"  ; e.g. //:
            |
            native-name: across to ":" one  ; everything else is foo:
        ]
        accept (okay)
    ] else [
        panic "Could not extract native name in emit-include-params-macro"
    ]
    let spec: copy find proto "["  ; make copy (we'll corrupt it)

    replace spec "@" -[]-  ; @WORD! would be invalid EMAIL! [1]
    replace spec "$" -[]-  ; $WORD! would be invalid MONEY! [1]
    replace spec "^^" -[]-  ; ^WORD! would just be invalid [1]

    textually-splice-last-fence spec  ; bootstrap loads {...} locals as TEXT!

    spec: transcode:one spec

    let paramlist: collect [  ; no PARSE COLLECT in bootstrap exe :-(
        if not text? spec.1 [
            panic "NATIVE IS MISSING DESCRIPTION"
        ]
        spec: my next

        any [  ; (almost) all natives should have `RETURN: [<typespec>]`  ; [2]
            (the return:) <> spec.1
            (not block? spec.2) and (spec.2 <> '~)
            text? opt try spec.3
        ] then [
            any [
                native-name = "native-bootstrap"
                native-name = "tweak*-bootstrap"
            ] else [
                panic [
                    "has a bad RETURN: [<typespec>] specification" newline
                    "Note no string text for RETURN is allowed!" newline
                    "(Main description stored in RETURN PARAMETER!)" newline
                    "You can put strings *inside* the typespec instead!"
                ]
            ]
        ] else [
            spec: my skip 2
        ]

        if find proto "native:combinator" [  ; implicit combinator params [3]
            keep spread [
                state [frame!]
                input [any-series?]
            ]
        ]

        keep spread spec
    ]

    let is-intrinsic: did find proto "native:intrinsic"

    let symbols: copy []
    let n: 1
    let items: collect [
        for-each 'item paramlist [
            if group? item [
                item: first item
            ]
            if not match [word! get-word3! lit-word3!] item [
                continue  ; note get-word3! is refinement?
            ]

            let param-name: resolve noquote item
            append symbols param-name
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

    let varlist-hold: if is-intrinsic [
        [
            -[if (Not_Level_Flag(level_, DISPATCHING_INTRINSIC))]-
            -[    Set_Flex_Info(Varlist_Array(level_->varlist), HOLD);]-  ; [4]
        ]
    ] else [
        [-[Set_Flex_Info(Varlist_Array(level_->varlist), HOLD);]-]
    ]

    if empty? items [  ; vaporization currently not allowed
        insert items "NOOP"
    ]

    let prefix: all [extension unspaced [extension "_"]]
    e/emit [prefix native-name items varlist-hold --[
        #define ${OPT PREFIX}INCLUDE_PARAMS_OF_${NATIVE-NAME} \
            $[Varlist-Hold] \
            $(Items); \
    ]--]

    return symbols
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
export extract-generic-implementations: func [
    "Returns block of GENERIC-INFO! objects"
    return: [block!]
    c-source-file [file!]
][
    let [name proper-type name* type*]

    assert [not dir? c-source-file]
    return collect [
        parse3 read:string c-source-file [some [
            "IMPLEMENT_GENERIC" [
                "(" name*: across to "," "," space type*: across to ")" ")"
                thru newline
                (
                    name: uppercase copy name*
                    proper-type: propercase copy type*

                    if (name <> name*) or (proper-type <> type*) [
                        panic [
                            "Bad casing, should be"
                            "IMPLEMENT_GENERIC(" name "," proper-type ")"
                            "and not"
                            "IMPLEMENT_GENERIC(" name* "," type* ")"
                        ]
                    ]

                    lowercase name
                    lowercase type*

                    replace name "_q" "?"  ; use smarter parse rule...
                    if #"p" = last name [
                        replace name "_p" "*"
                    ]

                    replace name "_" "-"

                    replace type* "_" "-"
                    replace type* "is-" ^void

                    keep make generic-info! compose [
                        name: (name)
                        type: (quote as word! type*)
                        proper-type: (proper-type)
                        file: (c-source-file)
                    ]
                )
                | (panic ["Malformed generic in" c-source-file])
            ]
            | thru newline
        ]]
    ]
]
