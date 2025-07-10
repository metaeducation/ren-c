Rebol [
    title: "Shim to bring old executables up to date to use for bootstrapping"
    type: module
    name: Bootstrap-Shim
    rights: --[
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
        Ren-C "officially" supports two executables for doing the pre-build
        process, which generates needed header files and other artifacts.

        One EXE that can be used is a frozen "stable" version (`8994d23`) which
        was committed circa Dec-2018:

        https://github.com/metaeducation/ren-c/commit/dcc4cd03796ba2a422310b535cf01d2d11e545af

        The only other executable that is guaranteed to work is the *current*
        build.  This is ensured by doing a two-step build in the continuous
        integration, where 8994d23 is used to make the first one, and then
        the build is started over using that product.

        This shim is for 8994d23, in order to bring it up to compatibility
        for any new features used in the bootstrap code that were introduced
        since it was created.  This is facilitated by Ren-C's compositional
        operations, like ADAPT, CHAIN, SPECIALIZE, and ENCLOSE.
    ]--
    notes: --[
      * When running under the bootstrap EXE, this shim does not use EXPORT
        via the %import-shim.r method--it modifies the user context directly.
        Under the new EXE (which has a real IMPORT) it has to do EXPORT for
        a few minor backwards compatibility tweaks--for things that could not
        be done forward compatibly by the shim (at least not easily).

      * There was an issue with the Linux bootstrap executable not working
        on GitHub Actions, due to some strange issue with the binary and the
        container.  Rebuilding with a newer version of GCC seemed to resolve
        that, but Linux executables compiled circa 2018 in the wild may not
        work on some modern systems.
    ]--
]


if not find (words of import/) 'into [
    panic "%import-shim.r must be loaded before %bootstrap-shim.r"
]


; The bootstrap executable does not have SPREAD (or isotopes, generally)
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
sys.util/recover [
    spread []
] then [
    ; Fall through to the body of this file, we are shimming version ~8994d23
] else [
    if find (words of transcode/) 'next3 [  ; old shim'd interpreter
        ;
        ; Old bootstrap executables that are already shimmed should not do
        ; tweaks for the modern import.  It's just generally inefficient to
        ; shim multiple times.
        ;
        quit 0
    ]


    === "BACKWARDS-LEANING BOOTSTRAP FUNCTIONS" ===

    ; Most of the time, we want to shim the old executable so that it acts like
    ; a modern one...so the source in files implementing the build process does
    ; not look any more antiquated than it has to.  But some features are just
    ; not available.  e.g. SET-BLOCK! and multi-return do not exist in the
    ; bootstrap executable.  In these cases, the modern EXE is shimmed to act
    ; like the old one, offering the function under the name with a number.
    ;
    ; Note that bootstrap uses PARSE3, which is not as powerful as PARSE, but
    ; has been brought up to date from R3-Alpha to the conventions of UPARSE.
    ; It is more brittle and less composable, but it is available in the
    ; bootstrap executable..and much faster (at time of writing) than UPARSE.

    export parse: ~#[Use PARSE3 in Bootstrap Code, not PARSE]#~

    export /split-path3: enclose (
        augment split-path/ [:file [any-word? tuple!]]
    ) f -> [
        let results: lift:lite eval f  ; no [...]: in bootstrap load of file
        set opt f.file unlift results.2
        unlift results.1
    ]
    export split-path: ~#[Use SPLIT-PATH3 in Bootstrap (no multi-return)]#~

    export /transcode: enclose (
        augment lib.transcode/ [:next3 [word!] "set to transcoded value"]
    ) func [f] [
        if not f.next3 [
            return eval-free f
        ]
        f.next: okay
        let block: (  ; no SET-BLOCK in boot, no # in boot (space -> #)
            compose [(space) (join chain! [_ f.next3])]  ; synth optional
        )
        return eval compose [(setify block) eval-free f]
    ]

    export cscape-inside: inside/  ; modern string interpolation tool

    export for: ~#[FOR is being repurposed, use CFOR]#~

    export unless: ~#[Don't use UNLESS in Bootstrap, definition in flux]#~

    export /bind: augment bind/ [
        :copy3  ; modern BIND is non-mutating, but bootstrap EXE needs :COPY
    ]

    export set-word3!: set-word?/
    export set-path3!: set-tuple?/
    export get-word3!: get-word?/
    export refinement3!: run-word?/
    export char3!: char?/

    export load3: enclose (
        augment load/ [:header]  ; no multi-return values
    ) func [f] [
        let result': unanti eval f
        if f.header [
            ensure block! unquote result'.1
            ensure object! unquote result'.2
            return head of insert unquote result'.1 unquote result'.2
        ]
        return ensure block! unquote first result'
    ]
    export load: ~#[Use LOAD3 in Bootstrap (no multi-returns for header)]#~

    quit 0
]


=== "ENCLOSE REST OF MODULE IN GROUP TO AVOID OVERWRITING TOP-LEVEL DECLS" ===

; This module does things like overwrite IF.  But were we to make a top-level
; statement like `if: ...` then in a modern executable, before any code in
; the module ran, IF would be WRAP*'d as a module-level declaration and set
; to nothing.  To avoid this, we simply put all the emulation code in a group.

(  ; closed at end of file

warning!: error!


=== "SHORT NAMES FOR LIB3/XXX, CATCH USES OF SHIMMED FUNCTIONS BEFORE SHIM" ===

; The shims use the functions in the bootstrap EXE's lib to make forwards
; compatible variations.  But it's easy while editing this file to get
; confused and use the wrong version--possibly before it has been defined.
; This defaults all the functions that need shims to errors, and offer a
; shorter name for lib3 variants while doing so (e.g. FUNC3 for LIB3/FUNC)
;
; 1. Words not mentioned don't get bound and mentioned in system.contexts.user
;    Mention them so that (system.contexts.user.(word): <...>) will work.
;
; 2. Using SET-WORD!s here also helps searchability if you're looking for
;    where `func3: ...` is defined.

comment [  ; need unmentioned words in user context [1]
    tail
    char! any-word! any-path! lit-word! lit-path! issue! issue?
]

for-each [alias] [
    parse3:                     ; PARSE is a completely new model ("UPARSE")
    func3:                      ; FUNC refinements are their own args, more...
    lambda3:                    ; LAMBDA same deal...
    function3:                  ; no FUNCTION at present (TBD: FUNC synonym)
    method3:                    ; same issues as FUNC
    append3:                    ; APPEND handles splices
    change3:                    ; CHANGE handles splices
    insert3:                    ; INSERT handles splices
    replace3:                   ; REPLACE handles splices
    join3:                      ; JOIN handles splices
    compose3:                   ; COMPOSE now arity-2, processes splices
    split-path3:                ; bootstrap uses SPLIT-PATH3 not SPLIT-PATH
    load3:                      ; bootstrap uses LOAD3 not LOAD
    collect3:                   ; COLLECT's KEEP processes splices
    mold3:                      ; MOLD takes splices instead of MOLD/ONLY
    bind3:                      ; BIND's arguments reversed
    head3:                      ; use HEAD OF instead
    tail3:                      ; use TAIL OF instead

    meta3:                      ; became LIFT
    unmeta3:                    ; became UNLIFT

    issue3!:                    ; now it's rune!
    issue3?:                    ; ...
    refinement3?:               ; Former refinements of /FOO now :FOO
    refinement3!:               ; ...
    set-word3!:                 ; use set-word?
    set-path3!:                 ; use set-path?
    get-word3!:                 ; use get-word?
    char3!:                     ; use get-path?
    any-word3!:                 ; use any-word?
    lit-word3!:                 ; use lit-word?
    lit-path3!:                 ; use lit-word?
    any-path3!:                 ; use path?
][
    assert [set-word? alias]  ; SET-WORD!s for readability + findability [2]

    ; Assign the alias what the existing version (minus the terminal "3") is
    ; (e.g. func3: func/)
    ;
    name: copy as text! alias
    lib3/parse name [to "3" remove "3" to <end>]
    name: to word! name
    lib3/append system.contexts.user reduce [alias :lib3.(name)]

    ; Make calling the undecorated version an error, until it becomes shimmed
    ;
    system.contexts.user.(name): to tripwire! spaced [
        (lib/mold name) "not shimmed yet, see" as word! (lib/mold alias)
    ]
]

function3: ~#[FUNCTION slated for synonym of FUNC, so no FUNCTION3]#~


=== "RENAME META AND UNMETA" ===

; Task: switch Boostrap META and UNMETA to LIFT and UNLIFT

lift: meta3/
unlift: unmeta3/


=== "BINARY! => BLOB!" ===

blob!: binary!
blob?: binary?/


=== "ISSUE! => RUNE!" ===

rune!: issue3!
rune?: issue3?/


=== "TRY TO UNIFY SPACE AND BLANK TESTING" ===

; There's only one literal for (_) and (#" ") in modern Ren_C, so that might
; confuse matters in bootstrap.  Try saying either is a space.

space?: func3 [x] [
    return (x = space) or (x = _)
]

blank?: ~#[No BLANK? in Bootstrap, Use SPACE?]#~


=== "MAKE THE KEEP IN COLLECT3 OBVIOUS AS KEEP3" ===

; Even if you see you are using the COLLECT3 version of COLLECT, it's easy
; to not remember that the KEEP is the kind that takes /ONLY.  Renaming the
; keeper to KEEP3 makes that clearer.

collect3: adapt lib3.collect/ [
    body: compose3 [
        keep3: keep/  ; help point out keep3 will splice blocks, has /ONLY
        keep: ~
        (body)  ; splices block body because it's COMPOSE3
    ]
]


=== "BACKWARDS-LEANING BOOTSTRAP FUNCTIONS (SHIM ERRORS)" ===

; At the top of the file, modern executables are dialed back on features which
; can't be emulated by older executables.  Here we panic in the old executable
; on any undecorated functions that have no emulation equivalent.

parse: ~#[Use PARSE3 in bootstrap code, not PARSE (UPARSE too slow, for now)]#~

split-path: ~#[Use SPLIT-PATH3 in Bootstrap (:FILE takes WORD! vs multirtn)]#~

load: ~#[Use LOAD3 in Bootstrap (:HEADER returns BLOCK! with OBJECT!)]#~


=== "FAKE UP QUASIFORM! AND @WORD! TYPES" ===

; In the case of the iteration functions, they take RUNE! (a WORD! type in
; the bootstrap executable) to mean that the variable has a binding already
; to use vs. create a new one.  It's essential to use with ITERATE in modern
; Ren-C, but we can't say `iterate @block [...]` in bootstrap (no @).  Hence
; instead, PIN (which adds @ in new executables) is defined to add a #"."

pin: lambda3 [word [word!]] [to rune! word]

quasiform!: word!  ; conflated, but can work in a very limited sense
quasi?: func3 [v <local> spelling] [
    if not word? v [return null]
    spelling: as text! v
    if #"~" <> first spelling [return null]
    if #"~" <> last spelling [return null]
    return okay
]
unquasi: func3 [v <local> spelling] [
    assert [quasi? v]
    spelling: to text! v
    assert [#"~" = take spelling]
    assert [#"~" = take:last spelling]
    return as word! spelling
]


unrun: ~#[No UNRUN in bootstrap, but could be done w/make FRAME!]#~

has: in/  ; old IN behavior of word lookup achieved by HAS now
overbind: in/  ; works in a limited sense
bindable: lambda3 [what] [:what]
inside: lambda3 [where value] [:value]  ; no-op in bootstrap
wrap: identity/  ; no op in bootstrap

in: ~#[Use HAS or OVERBIND instead of IN in bootstrap]#~

quote: lambda3 [x [any-value!]] [  ; see the more general UNEVAL
    switch type of x [
        word! [to lit-word3! x]  ; to lit-word! not legal in new EXE
        path! [to lit-path3! x]  ; to lit-path! not legal in new EXE

        panic:blame [
            "Bootstrap QUOTE only works on WORD!, PATH!:" mold x
        ] $x
    ]
]

unquote: lambda3 [x [any-value!]] [  ; see the more general EVAL
    switch type of x [
        lit-word3! [to word! x]  ; to word! of quoted not legal in new EXE
        lit-path3! [to path! x]  ; to path! of quoted not legal in new EXE

        panic:blame [
            "Bootstrap UNQUOTE only works on LIT-WORD?, LIT-PATH?:" mold x
        ] $x
    ]
]


;=== BELOW THIS LINE, TRY NOT TO USE FUNCTIONS IN THE SHIM IMPLEMENTATION ===

; Not sure if LOGIC! will be able to act as its own type in the future, so
; using "weird" definition for now.

; LIT-WORD and REFINEMENT will never be datatypes, so shim the type constraint
; so it works in old parse.

run-word?: refinement3?/
run-word!: refinement3!
refinement!: get-word3!
refinement?: get-word?/

chain!: path!  ; works in some places (a:b scans as a PATH! in bootstrap EXE)

set-word!: func3 [] [
    panic:blame "SET-WORD! is now a CHAIN!, try SET-WORD?!" $return
]
get-word!: func3 [] [
    panic:blame "GET-WORD! is now a CHAIN!, try GET-WORD?!" $return
]
set-path!: func3 [] [
    panic:blame "SET-PATH! can no longer exist, try SET-TUPLE?!" $return
]
get-path!: func3 [] [
    panic:blame "GET-PATH! can no longer exist, try GET-TUPLE?!" $return
]

setify: lambda3 [plain [word! path!]] [
    either word? plain [to-set-word plain] [to-set-path plain]
]

unchain: lambda3 [chain [set-word3! set-path3!]] [
    either set-word? chain [to-word chain] [to-path chain]
]

unpath: lambda3 [path [refinement3!]] [
    to-word path
]

any-value?: lambda3 [x] [okay]  ; now inclusive of null
element?: any-value?/  ; used to exclude null

typechecker: lambda3 [x [datatype! typeset! block!]] [
    if x [if block? x [make typeset! x] else [x]]
]


=== "SPLICE-AWARE FUNCTIONS IN BOOTSTRAP" ===

; The bootstrap executable is still based on the /ONLY refinement for asking
; to keep blocks as-is, while splicing by default.  This has been replaced
; in modern Ren-C, where all append-like operations do as-is by default and
; carry splicing intent on the value by means of antiform groups:
;
; https://forum.rebol.info/t/the-long-awaited-death-of-only/1607
;
; While many tweaks have been made to the bootstrap executable where it was
; possible to easily sync it to modern decisions, it would be disruptive to
; graft splicing into it... especially because it's most stable if the
; bootstrap executable can build itself with the prior bootstrap executable.
; These few shims have worked well enough.

spread: func3 [
    return: [~void~ ~null~ block!]  ; !!! bootstrap has stable ~void~
    x [~null~ ~void~ block!]
][
    return case [
        null? :x [return null]
        void? :x [return void]
        <else> [reduce [#splice! x]]
    ]
]

append: func3 [series value [<undo-opt> any-element!] /line <local> only] [
    any [
        object? series
        map? series
    ] then [
        all [
            block? value
            #splice! = (first value)
        ] else [
            panic:blame "Bootstrap shim for OBJECT! only APPENDs SPLICEs" $return
        ]
        return append3 series second value
    ]

    only: 'only
    if (block? value) and (#splice! = first value) [
        value: second value
        if not any-list? series [  ; itemwise appends for strings/etc.
            for-each 'item value [
                append series item
            ]
            return series
        ]
        only: null
    ]
    return append3:(opt only):(opt line) series opt value
]

insert: func3 [series value [<undo-opt> any-element!] /line <local> only] [
    only: 'only
    if (block? value) and (#splice! = first value) [
        value: second value
        if not any-list? series [  ; itemwise appends for strings/etc.
            for-each 'item value [
                series: insert series item
            ]
            return series
        ]
        only: null
    ]
    return insert3:(opt only):(opt line) series opt value
]

change: func3 [series value [<undo-opt> any-element!] /line <local> only] [
    only: 'only
    if (block? value) and (#splice! = first value) [
        value: second value
        if not any-list? series [
            panic ["CHANGE to SPLICE not currently in shim"]
        ]
        only: null
    ]
    return change3:(opt only):(opt line) series opt value
]

replace: func3 [
    target [<opt-out> any-series!]
    pattern [<undo-opt> any-element!]
    replacement [<undo-opt> any-element!]
][
    if (block? pattern) and (#splice! = first pattern) [
        pattern: second pattern
    ] else [
        if pattern [
            pattern: reduce [pattern]
        ]
    ]
    if (block? replacement) and (#splice! = first replacement) [
        pattern: second replacement
    ] else [
        if pattern [
            pattern: reduce [pattern]
        ]
    ]
    return replace3 target (opt pattern) (opt replacement)
]

join: func3 [
    base [blob! any-string! path! datatype!]
    value [void! any-value!]
][
    if block? value [
        if #splice! = (first value) [
            panic:blame "SPLICE no longer supported in JOIN" $return
        ]
    ]

    if datatype? base [
        if base = blob! [
            assert [block? value]
            return to blob! value
        ]
        any [base = path!, base = tuple!] then [
            assert [block? value]
            return to base reduce value
        ]
        panic ["JOIN of DATATYPE! only [BLOB! TUPLE! PATH!] in bootstrap"]
    ]
    if void? :value [
        return copy base
    ]
    if block? value [
        return append3 copy base reduce value
    ]
    return append3 copy base value
]

collect*: func3 [  ; variant giving NULL if no actual material kept
    return: [null? block!]
    body [block!]
    <local> out keeper
][
    out: null
    keeper: specialize (  ; SPECIALIZE to remove series argument
        enclose 'append func3 [  ; gets :LINE, :DUP
            f [frame!]
            <with> out
        ][
            assert [not action? get $f.value]
            if void? f.value [return void]  ; doesn't "count" as collected

            f.series: out: default [make block! 16]  ; won't return null now
            f.value  ; ELIDE leaves as result (F.VALUE invalid after EVAL F)
            elide eval f
        ]
    )[
        series: <replaced>
    ]

    reeval lambda3 [keep [action!] <with> return] body keeper/

    return out
]

/collect: cascade [  ; Gives empty block instead of null if no keeps
    :collect*  ; note: does not support , in bootstrap build
    specialize 'else [branch: [copy []]]
]

compose: func3 [block [block!] /deep <local> result pos product count] [
    if deep [
        panic:blame "COMPOSE bootstrap shim doesn't recurse, yet" $block
    ]
    pos: result: copy block
    until [tail? pos] [
        if not group? pos.1 [
            pos: next pos
            continue
        ]

        product: lift eval pos.1  ; can't SET-WORD! of VOID in bootstrap
        if void? unlift product [
            change3:part pos void 1
            continue
        ]
        product: unlift product
        if okay? :product [  ; e.g. compose [(print "HI")]
            panic:blame "~okay~ antiform compose found" $return
        ]
        if null? :product [  ; e.g. compose [(print "HI")]
            panic:blame "~null~ antiform compose found" $return
        ]
        if trash? :product [  ; e.g. compose [(print "HI")]
            panic:blame "~ antiform compose found" $return
        ]
        all [
            block? :product
            #splice! = first product
        ] then [
            ; Doing the change can insert more than one item, update pos
            ;
            pos: change3:part pos second product 1
        ] else [
            change3:only pos :product
            pos: next pos
        ]
    ]
    return result
]


collect-lets: func3 [
    return: [block!]
    list [block! group!]
    <local> lets
][
    lets: copy []
    for-next 'item list [
        case [
            :item.1 = 'let [
                item: next item
                if match [word! block! set-word3!] item.1 [
                    append3 lets item.1
                ]
            ]
            match [block! group!] :item.1 [
                append3 lets collect-lets item.1
            ]
        ]
    ]
    return lets
]


let: func3 [
    return: [any-value!]
    args [trash! any-value! <...>]
    :look [any-value! <...>]
][
    if word? first look [return take look]  ; otherwise leave SET-WORD! to run
    return take args
]


=== "MODERNIZE FUNCTION GENERATORS" ===

; Several changes were made, but the most significant is that refinements are
; now their own named arguments:
;
; https://forum.rebol.info/t/implifying-refinements-to-one-or-zero-args/1120
;
; This maps the new types in typespecs to old equivalents (or approximations)
; and does the work to simulate the new refinement mechanisms.

modernize-typespec: func3 [
    return: [block!]
    types [block!]
][
    types: copy types
    for-each [current bootstrap] [
        any-value?      [null? any-value!]
        any-string?     any-string!
        element?        any-value!
        action?         action!
        logic?          logic!
        <opt-out>       <opt-out>   ; !!! works in both now !
        <opt>           <undo-opt>  ; !!! will be updated at some point...
        <variadic>      <...>
        boolean?        word!
        onoff?          word!
        yesno?          word!
        run-word?       refinement3!
        any-word?       any-word3!
        lit-word?       lit-word3!
        refinement?     get-word3!
        char?           char3!
        set-word?       set-word3!
        get-word?       get-word3!
        set-tuple?      set-path3!
        get-tuple?      get-path3!
    ][
        replace3 types current bootstrap  ; splices blocks
    ]
    return types
]

modernize-action: func3 [
    "Account for refinements as own arguments (and type spec updates)"
    return: [block!]
    spec [block!]
    body [block!]
    <local> last-refine-word tryers proxiers proxy w types new-spec new-body
][
    last-refine-word: null

    tryers: copy []
    proxiers: copy []

    new-spec: collect3 [  ; Note: offers KEEP/ONLY
        while [text? spec.1] [
            keep3 spec.1
            spec: my next
            continue
        ]
        until [tail? spec] [
            if spec.1 = the return: [
                if spec.2 = [] [
                    keep3 [return: [trash!]]
                    spec: my next
                    spec: my next
                    continue
                ]
            ]

            if tag? spec.1 [
                last-refine-word: null
                keep3:only spec.1
                spec: my next
                continue
            ]

            if refinement3? spec.1 [  ; old /REFINEMENT
                panic:blame ["Old refinement in spec:" mold spec] $spec
            ]

            if refinement? spec.1 [  ; new :REFINEMENT
                last-refine-word: as word! spec.1
                keep3:only to refinement3! spec.1

                ; Feed through any TEXT!s following the PATH!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec.1
                ][
                    keep3:only spec.1
                ]

                ; If there's a block specifying argument types, we need to
                ; have a fake proxying parameter.

                if not block? spec.1 [
                    continue
                ]

                proxy: as word! unspaced [last-refine-word "-arg"]
                keep3:only proxy
                keep3:only modernize-typespec spec.1

                append3 proxiers compose3 [
                    (to-set-word last-refine-word) (to-get-word proxy)
                    (to-set-word proxy) ~
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args and locals)
            ;
            if w: match (get $any-word3!) spec.1 [
                if set-word? w [
                    assert [w = first [return:]]
                    keep3 spec.1, spec: my next
                    if tail? spec [continue]
                    if text? spec.1 [keep3 spec.1, spec: my next]
                    if block? spec.1 [
                        keep3:only modernize-typespec spec.1
                        spec: my next
                    ]
                    if tail? spec [continue]
                    if text? spec.1 [keep3 spec.1 spec: my next]
                    continue
                ]

                ; Transform the escapable argument convention, to line up
                ; GET-WORD! with things that are escaped by GET-WORD!s
                ; https://forum.rebol.info/t/1433
                ;
                keep3 case [
                    lit-word? w [to-get-word w]
                    get-word? w [to lit-word3! w]
                    <else> [w]
                ]

                if last-refine-word [
                    panic:blame [
                        "Refinements now *are* the arguments:" mold head spec
                    ] $spec
                ]

                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec.1
                ][
                    keep3:only spec.1
                ]

                if spec.1 = [void?] [
                    keep3:only []  ; old cue for invisibility
                    spec: my next
                    continue
                ]

                if types: match block! spec.1 [
                    types: modernize-typespec types
                    keep3:only types
                    spec: my next
                    continue
                ]
            ]

            if refinement? spec.1 [  ; new :REFINEMENT
                continue
            ]

            keep3:only spec.1
            spec: my next
        ]
    ]

    ; The bootstrap executable does not have support for true dynamic LET.
    ; We approximate it by searching the body for LET followed by SET-WORD!
    ; or WORD! and add that to locals.
    ;
    append3 new-spec <local>
    append3 new-spec collect-lets body  ; append3 splices blocks without :ONLY

    new-body: compose3 [  ; splices
        (tryers)
        (proxiers)
        (as group! body)  ; compose3 does not splice groups--just blocks
        return ~  ; functions now default to returning nothing
    ]
    return reduce [new-spec new-body]
]

lambda: adapt lambda3/ [set [spec body] modernize-action spec body]

func: adapt func3/ [set [spec body] modernize-action spec body]

function: ~#[FUNCTION deprecated (will be FUNC synonym, eventually)]#~

method: infix adapt $lib3.meth/ [set [spec body] modernize-action spec body]


=== "FUNCTION APPLICATION" ===

; This is a poor man's compatibility APPLY, that approximates some of the
; features of "APPLY II"
;
; https://forum.rebol.info/t/apply-ii-the-revenge/1834

apply: func3 [
    action [action!]
    args [block!]
    <local> f params result pos
][
    f: make frame! :action
    params: words of :action

    ; Get all the normal parameters applied
    ;
    result: _
    while [all [params.1, not refinement3? params.1]] [
        args: evaluate:step3 args 'result
        f.(to word! params.1): :result
        params: next params
    ]

    ; Now go by the SET-WORD!s.  If it's a refinement that takes an argument,
    ; we have to set the refinement to okay
    ;
    while [set-word? :args.1] [
        pos: find params to refinement3! args.1 else [
            panic ["Unknown refinement" args.1]
        ]
        args: evaluate:step3 (next args) 'result
        any [
            not :pos.2
            refinement3? pos.2  ; old refinement in params block
        ] then [  ; doesn't take an argument, set it to the logical next value
            case [
                any [
                    null? :result
                    void? :result
                ][
                    f.(to word! pos.1): null
                ]
                any [
                    okay = :result
                    refinement3? :result
                ][
                    f.(to word! pos.1): okay
                ]
                panic [
                    "No-Arg Refinements in Bootstrap must be OKAY or NULL:"
                        mold pos.1 "=" mold :result
                ]
            ]
        ] else [  ; takes an arg, so set refinement to okay and set NEXT param
            f.(to word! pos.1): okay
            f.(to word! pos.2): :result
        ]
    ]

    return eval f
]

//: infix lambda3 ['left [word! path!] right [block!]] [
    apply get left right
]

; For commentary purposes, e.g. old-append: runs lib.append
;
runs: func3 [action [~void~ action!]] [  ; !! bootstrap has stable void
    if void? action [return null]
    return action
]


mold: adapt mold3/ [  ; update so MOLD SPREAD works
    if all [
        block? value
        #splice! = first value
    ][
        only: okay
        value: next value
    ]
]

noquote: func3 [x [<undo-opt> any-value!]] [
    assert [not action? get $x]
    switch type of x [
        lit-word3! [return to word! x]
        lit-path3! [return to path! x]
    ]
    return x
]

resolve: func3 [x [any-word3! any-path3!]] [
    if any-word? x [return to word! x]
    return to path! x
]


=== "BINDING SHIM" ===

; The semantics surrounding BIND are completely rethought, and it's not a
; mutating operation.  But in the bootstrap executable it is a mutating
; operation, and requires mutable access...and source is locked...so you
; have to copy.  :COPY3 is a no-op in the modern executable.
;
; Another big change is that the parameters are reversed.
;
bind: lambda3 [context element /copy3] [
    either copy3 [
        bind3:copy element context
    ][
        bind3 element context
    ]
]

; This is a surrogate for being able to receive the environment for string
; interpolation from a block.  Instead, the words that aren't in the user
; context or lib have to be mentioned explicitly inside the block.  If you
; want to bind to an object as well, name it with a GET-WORD!.
;
cscape-inside: func3 [
    return: [block!]
    template [block!]
    code [block!]
    <local> obj
][
    intern code  ; baseline user or lib binding
    obj: make object! []  ; can't bind individual words, fake w/proxy object
    for-each 'item template [
        if path? item [item: first item]
        case [
            text? item [continue]  ; should only be last item
            file? item [continue]
            get-word? item [
                item: get item
                assert [object? item]
                code: bind item code
            ]
        ] else [
            assert [word? item]
            append obj spread reduce [item 0]
            obj.(item): get:any item
        ]
    ]
    code: bind obj code  ; simulates ability to bind to single words
    return code
]

; Weak subsetting of ENCODE capabilities needed by bootstrap.
;
encode: func3 [codec arg [text! integer!]] [
    if codec = [BE + 1] [
        assert [all [integer? arg, arg < 256, arg >= 0]]
        return head of change copy #{00} arg
    ]
    if codec = 'UTF-8 [
        assert [text? arg]
        return to blob! arg
    ]
    panic ["Very limited ENCODE abilities in bootstrap, no:" mold codec]
]

decode: func3 [codec bin [blob!]] [
    if codec = 'UTF-8 [
        return to text! bin
    ]
    panic ["Very limited DECODE abilities in bootstrap, no:" mold codec]
]

blockify: func3 [x] [
    if null? x [
        panic:blame "BLOCKIFY can't take ~null~ antiform" $x
    ]
    if void? x [
        return copy []
    ]
    if block? x [
        return x
    ]
    return reduce [x]
]

=== "QUICK TESTS TO MAKE SURE THINGS ARE WORKING" ===

assert [[a b c d e] = append [a b c] spread [d e]]
assert [[a b c [d e]] = append [a b c] [d e]]
assert [[a b c] = append [a b c] void]
assert [null = append void [d e]]

assert [[d e a b c] = head of insert [a b c] spread [d e]]
assert [[[d e] a b c] = head of insert [a b c] [d e]]
assert [[a b c] = head of insert [a b c] void]
assert [null = insert void [d e]]

assert [[d e c] = head of change [a b c] spread [d e]]
assert [[[d e] b c] = head of change [a b c] [d e]]
assert [[a b c] = head of change [a b c] void]
assert [null = change void [d e]]

=== "END ENCLOSURE THAT AVOIDED OVERWRITING TOP-LEVEL DECLS" ===

)  ; see earlier remarks for why this group exists, close it now
