Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Generate auto headers"
    file: %native-emitters.r
    type: module
    name: Native-Emitters
    rights: --[
        Copyright 2017 Atronix Engineering
        Copyright 2017-2026 Ren-C Open Source Contributors
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
                    "/" name: across to ":" one  ; /foo: or foo:
                ]
                space
                opt [["pure" | "impure"] space]
                opt ["vanishable" space]
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


; EMIT-INCLUDE-PARAMS-MACRO
;
; This routine has to deal with differences between specs the bootstrap EXE
; can load and what the modern Ren-C can load.  The easiest way to do this is
; to take in the textual spec, "massage" it in a way that doesn't destroy the
; information being captured, and then TRANSCODE it.
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
            "/" native-name: across to ":" one  ; everything else is foo:
        ]
        accept (okay)
    ] else [
        panic "Could not extract native name in emit-include-params-macro"
    ]

  === "PRE-PROCESS SPEC FOR BOOTSTRAP LOAD-ABILITY" ===

  ; 1. Sigils like [$ ^ @] and quoted/quasiforms [' ~] are mostly not legal in
  ;    the bootstrap executable.  We work around this, by transforming them
  ;    into tags that are spaced off from anything they are attached to.  This
  ;    makes them LOAD-able (albeit more awkward to parse)
  ;
  ; 2. We want {local1 local2} to become locals in the spec dialect, but this
  ;    will load as a string in the bootstrap executable. So we have to
  ;    transform those too if we want to see them.

    let spec: copy find proto "["  ; make copy (we'll corrupt it)

    replace spec "'" " <'> "  ; turn decorations to tags for bootstrap LOAD [1]
    replace spec "@" " <@> "
    replace spec "$" " <$> "
    replace spec "^^" " <caret> "  ; <^^> doesn't work atm
    replace spec "~" " <~> "
    replace spec ":" " <:> "

    replace spec "{" " <{> "  ; account for FENCE! transition as well [2]
    replace spec "}" " <}> "

    spec: transcode:one spec

  === "BUILD PARAMETER LIST FROM (ADJUSTED) SPEC BLOCK" ===

  ; 1. We are looking for the adjusted patterns:
  ;
  ;       return <:> [type1! type2!]     ; was [return: [type1! type2!]]
  ;       return <:> <'> [type1! type2]  ; was [return: '[type1! type2!]]
  ;
  ;    All natives *should* specify a `return:`, because it's important to
  ;    document what the return types are (and HELP should show it).  But only
  ;    CHECK_RAW_NATIVE_RETURNS builds actually *type check* the result; the C
  ;    code is trusted otherwise to do the correct thing.
  ;
  ;    Natives store their return type specification in their Details, not in
  ;    their ParamList (the way a FUNC does), because there is no need for
  ;    instances of natives to have a RETURN function slot.
  ;
  ;    (There are two exceptions: The NATIVE native itelf and TWEAK, in
  ;     versions used during system boot.)
  ;
  ; 2. NATIVE:COMBINATOR instances have implicit parameters just like usermode
  ;    COMBINATORs do.  We want those implicit parameters in ARG() macro list
  ;    so the native bodies see them (again, just as the usermode combinators
  ;    can use `state` and `input` in their body code)

    let paramlist: collect [  ; no PARSE COLLECT in bootstrap exe :-(
        if not text? spec.1 [
            panic "NATIVE IS MISSING DESCRIPTION"
        ]
        spec: my next

        let panic-bad-return: does [  ; see [1]
            panic [
                "has a bad RETURN: [<typespec>] specification" newline
                "Note no string text for RETURN is allowed!" newline
                "(Main description stored in RETURN PARAMETER!)" newline
                "You can put strings *inside* the typespec instead!"
            ]
        ]

        if not any [
            native-name = "native-unchecked"
            native-name = "tweak*-unchecked"
        ][
            if 'return <> spec.1 [panic-bad-return]
            spec: next spec

            if <:> <> spec.1 [panic-bad-return]
            spec: next spec

            if <'> = spec.1 [  ; optional: don't check return
                spec: next spec
            ]

            if not any [block? spec.1, spec.1 = <~>] [panic-bad-return]
            spec: next spec
        ]

        if find proto "native:combinator" [  ; implicit combinator params [2]
            keep spread [
                state [frame!]
                input [any-series?]
            ]
        ]

        keep spread spec
    ]

    spec: ~<native SPEC processed: use PARAM.SPEC in macro generation code>~

  === "GENERATE TEXT! OF INCLUDE_PARAMS_OF_XXX() MACRO" ===

    let is-intrinsic: did find proto "native:intrinsic"

    let symbols: copy []
    let n: 1
    let processing-locals: null
    let items: collect [
        let iter: paramlist
        while [iter and (not tail? iter)] [
            if iter.1 = <{> [
                assert [not processing-locals]
                processing-locals: okay
                iter: next iter
                continue
            ]
            if iter.1 = <}> [
                assert [processing-locals]
                processing-locals: null
                iter: next iter
                continue
            ]
            let param: make object! [
                refinement: null
                class: either processing-locals ['local] ['normal]
                unbind: null
                unchecked: null
                spec: null
            ]
            if iter.1 = <'> [  ; unbound parameter
                param.unbind: okay
                iter: next iter
            ]
            switch iter.1 [
                <caret> [
                    param.class: 'meta
                ]
                <$> [
                    param.class: 'aliasable
                    panic "no $PARAM aliasable parameters in specs... YET!"
                ]
                <@> [
                    if group? iter.2 [  ; escapable
                        param.class: 'soft
                        iter.2: first iter.2
                    ] else [
                        param.class: 'literal
                    ]
                ]
            ] then [
                assert [not processing-locals]
                iter: next iter
            ]
            if iter.1 = <:> [
                param.refinement: okay
                iter: next iter
            ]
            let name: iter.1
            if not word? name [
                panic ["Unknown item in spec for bootstrap:" mold name]
            ]
            iter: next iter

            while [text? cond try pick cond iter 1] [  ; allow TEXT! before spec
                iter: next iter
            ]

            if (try pick cond iter 1) = <'> [  ; quote on spec block
                param.unchecked: okay
                iter: next iter
            ]
            if param.spec: match block! cond try pick cond iter 1 [
                iter: next iter
            ] else [
                if not param.refinement [  ; !!! fix this once everything ready
                   ; panic "Non-refinement arg missing spec block:" mold item
                ]
            ]

            while [text? cond try pick cond iter 1] [  ; allow TEXT! after spec
                iter: next iter
            ]

            let ctype  ; underlying type (Element, Stable, Value, Param)
            let argmode  ; required, optional, or intrinsic
            case [
                find cond param.spec <hole> [ctype: 'Param]
                param.class = 'meta [ctype: 'Value]
                param.class = 'literal [ctype: 'Element]
                (find [  ; !!! bad way of doing this...think of better
                    [frame!]
                    [element?]
                    [<const> block! frame!]
                    [<opt> element?]
                ] cond param.spec) [ctype: 'Element]
            ] else [
                if find cond param.spec <end> [
                    ctype: 'Value
                ] else [
                    ctype: 'Stable
                ]
            ]

            case [
                param.refinement or (find cond param.spec <opt>) [
                    argmode: 'ARGMODE_OPTIONAL
                ]
                param.class = 'local [
                    assert [not param.refinement]
                    argmode: 'ARGMODE_REQUIRED
                ]
            ] then [
                if is-intrinsic and (n = 1) [
                    panic "Intrinsic argument cannot be optional/refinement"
                ]
            ] else [
                if is-intrinsic and (n = 1) [
                    argmode: 'ARGMODE_INTRINSIC
                ] else [
                    argmode: 'ARGMODE_REQUIRED
                ]
            ]

            let cwrap: switch argmode [
                'ARGMODE_OPTIONAL ['Option]
                'ARGMODE_REQUIRED ['Need]
                'ARGMODE_INTRINSIC ['Need]
                panic
            ]

            if is-intrinsic and (n = 1) [
                assert [param.unchecked]  ; intrinsic args never checked
            ]

            let checkmode: any [  ; if no relevant constraint, consider checked
                not param.unchecked
                all [param.class = 'normal, find cond param.spec 'any-stable?]
                all [param.class = 'meta, find cond param.spec 'any-value?]
            ] then [
                "CHECKED"
            ] else [
                "UNCHECKED"
            ]

            append symbols name

            if param.refinement and (not param.spec) [
                assert [argmode = 'ARGMODE_OPTIONAL]
                keep cscape [checkmode cwrap ctype name n argmode
                    "DECLARE_$<CHECKMODE>_PARAM(bool, ${NAME}, $<n>, $<ARGMODE>)"
                ]
            ]
            else [
                keep cscape [checkmode cwrap ctype name n argmode
                    "DECLARE_$<CHECKMODE>_PARAM($<CWrap>($<CType>*), ${NAME}, $<n>, $<ARGMODE>)"
                ]
            ]
            n: n + 1
        ]
    ]

  ; 1. We need `Set_Flex_Info(level_varlist, HOLD)` here because native code
  ;    trusts that type checking has ensured it won't get bits in its argument
  ;    slots that the C won't recognize.  Usermode code that gets its hands on
  ;    a native's FRAME! (e.g. for debug viewing) can't be allowed to change
  ;    the frame values to other bit patterns out from under the C or it could
  ;    result in a crash.  The native itself doesn't care because it's not
  ;    using ordinary variable assignment.
  ;
  ;    !!! This prevents API use inside natives which is binding-based.  That
  ;    is an inconvenience, and if a native wants to do it for expedience then
  ;    it needs to clear this bit itself (and set it back when done).

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

  ; For natives coming from %sys-core.h we do not have a per-native binding
  ; defined using variable shadowing the way API natives do, e.g. there are
  ; not lines in here like:
  ;
  ;       RebolContext* LIBREBOL_BINDING_NAME() = level_->varlist; \
  ;       USED(LIBREBOL_BINDING_NAME()); \
  ;
  ; This would give libRebol a way to find the frame for the native, and do
  ; resolution of the arguments by name vs. with ARG() macros.
  ;
  ; BUT the reason it is not done is because %sys-core.h natives do not want
  ; to pay for managing the VarList, and if it were used as the binding on
  ; API calls it would wind up glued onto elements of that code...making it
  ; tax the garbage collector more.  The tradeoff is that core natives just
  ; use the binding of LIB or the module they are in.
  ;
  ; (We could opportunistically manage the binding, but for the moment it is
  ; preferred to enforce the unmanaged state to emphasize how much more GC
  ; load would be created by using libRebol calls in core natives otherwise.)

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
                    replace type* "is-" none

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
