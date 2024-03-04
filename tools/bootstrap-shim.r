REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Type: module
    Name: Bootstrap-Shim
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2024 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
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
    }
    Notes: {
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
    }
]


if trap [
    :import/into  ; no /INTO means error here, so old r3 without import shim
][
    ; Don't use <{...}> in this error message, because if this message is
    ; being reported then this interpreter will think that's a tag.

    print ""
    print "!!! Bootstrapping with older Ren-C requires passing %import-shim.r"
    print "on the command line with the --import option, e.g."
    print ""
    print "    r3 --import import-shim.r make.r"
    print ""
    print "...instead of just `r3 make.r`.  Otherwise make.r can't use things"
    print "like <{...}> strings in older Ren-C."
    print ""
    print "(See %import-shim.r for more details)"
    print ""
    quit/with 1
]


; The snapshotted Ren-C (and R3C) existed before [~] was a legal return.
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [return: [~]] ["This definition should fail in old Ren-C"]
] then [
    ; Fall through to the body of this file, we are shimming version ~8994d23
] else [
    if trap [system.options.redbol-paths] [  ; old shim'd interpreter
        ;
        ; Old bootstrap executables that are already shimmed should not do
        ; tweaks for the modern import.  Otherwise, export load-all: would
        ; overwrite with LOAD instead of LOAD/ALL (for example).  It's just
        ; generally inefficient to shim multiple times.
        ;
        quit
    ]

    system.options.redbol-paths: true  ; new interpreter, make it act older

    === "TWEAKS SO MODERN REN-C DOESN'T ACT TOO MODERN" ===

    export parse: func [] [
        fail/where "Use PARSE2 in Bootstrap Process, not UPARSE/PARSE" 'return
    ]

    ; In the distant future, modern EXE bootstrap should use UPARSE-based code
    ; of Redbol's PARSE2.  For now, it relies on hacks to make PARSE3 act
    ; in a legacy way.
    ;
    export parse2: :parse3/redbol

    ; Bootstrap EXE doesn't support multi-returns so SPLIT-PATH takes a /DIR
    ; refinement of a variable to write to.
    ;
    export split-path3: enclose (
        augment :split-path [/file [any-word? any-path?]]
    ) f -> [
        let file: f.file
        let results: unquasi ^ do f  ; no [...]: in bootstrap load of this file
        set maybe file unmeta results.2
        unmeta results.1
    ]
    export split-path: func [] [
        fail/where "Use SPLIT-PATH3 in Bootstrap (no multi-return)" 'return
    ]

    export transcode3: enclose (augment :transcode [/next [word!]]) func [f] [
        let next: f.next  ; note: contention with NEXT series verb
        f.one: all [next make issue! 0]  ; # is invalid issue in bootstrap
        let result: ^ (do f except e -> [return raise e])
        if result = null' [return null]
        set maybe next unmeta second unquasi result
        return unmeta first unquasi result
    ]
    export transcode: func [] [
        fail/where "Use TRANSCODE3 in Bootstrap (no multi-return)" 'return
    ]

    export cscape-inside: :inside  ; modern string interpolation tool

    ; LOAD changed to have no /ALL, so it always enforces getting a block.
    ; But LOAD-VALUE comes in the box to load a single value.
    ;
    export load-all: :load

    export for: func [] [
        fail/where "FOR is being repurposed, use CFOR" 'return
    ]

    export unless: func [/dummy] [
        fail/where "Don't use UNLESS in Bootstrap, definition in flux" 'dummy
    ]

    export bar!: word!  ; signal there is no BAR! type, and | is a WORD!

    quit
]


=== "GIVE SHORT NAMES THAT CALL OUT BOOTSTRAP EXE'S VERSIONS OF FUNCTIONS" ===

; The shims use the functions in the bootstrap EXE's lib to make forwards
; compatible variations.  But it's not obvious to a reader that something like
; `lib/func` isn't the same as `func`.  These names help point it out what's
; happening more clearly (the 3 in the name means "sorta like R3-Alpha")
;
; 1. THIS IS WEIRD TO WORK AROUND BOOTSTRAP EXE'S UNRELIABLE CONTEXT EXPANSION.
;    When you do things like `system/contexts/user/(word): does ["hi"]` it
;    causes crashes sometimes, and having the load process see SET-WORD!s at
;    the top level seems to work around it.
;
;    (Using SET-WORD!s here also helps searchability if you're looking for
;    where `func3: ...` is set.)

aliases: lib3/reeval lib3/func [:item [any-value! <...>]] [  ; very weird [1]
    lib3/collect [while [item/1 != #end] [keep/only take item]]
    ]
    func3: func: *
    function3: function: *
    append3: append: *
    change3: change: *
    insert3: insert: *
    join3: join: *
    compose3: compose: *
    split-path3: split-path: *
    transcode3: transcode: *

    collect3: collect: (adapt :lib3/collect [  ; to make KEEP3 obvious
        body: lib3/compose [
            keep3: :keep  ; help point out keep3 will splice blocks, has /ONLY
            keep []  ; bootstrap workaround: force block result even w/no keeps
            lib3/unset 'keep
            (body)  ; compose3 will splice the body in here
        ]
    ])
#end

lib3/for-each [alias name shim] aliases [
    set alias either group? shim [
        do shim
    ][
        get (lib3/in lib3 name)
    ]

    ; Manually expanding contexts this way seems a bit buggy in bootstrap EXE
    ; Appending the word first, and setting via a PATH! seems okay.
    ;
    error: spaced [(mold name) "not shimmed yet, see" as word! (mold alias)]
    set name lib3/func [] lib3/compose [
        fail/where (error) 'return
    ]
]


;=== THESE REMAPPINGS ARE OKAY TO USE IN THE BOOTSTRAP SHIM ITSELF ===

; Done is used as a signal in the boot files that the expected end is reached.
; This is a QUASI-WORD? in modern Ren-C, but a plain word in the bootstrap EXE.
; Must use SET because even though we don't run this in modern Ren-C, the file
; gets scanned...and `~done~:` would be invalid.
;
set '~done~ does [~]

compose: func3 [block [block!] /deep <local> result pos product count] [
    if deep [
        fail/where "COMPOSE bootstrap shim doesn't recurse, yet" 'block
    ]
    pos: result: copy block
    while [not tail? pos] [
        if not group? pos/1 [
            pos: next pos
            continue
        ]

        product: do pos/1
        all [
            block? :product
            #splice! = first product
        ] then [
            ; Doing the change can insert more than one item, update pos
            ;
            pos: change3/part pos second product 1
        ] else [
            case [
                void? :product [
                    change3/part pos void 1
                ]
                trash? :product [  ; e.g. compose [(if true [null])]
                    fail/where "trash compose found" 'return
                ]
            ] else [
                change3/only pos :product
                pos: next pos
            ]
        ]
    ]
    return result
]

; Things like INTEGER! are now type constraints, not to be confused with
; "cell kinds" like &integer.  SWITCH/TYPE does not exist in the bootstrap
; so one must use the more limited `switch kind of` pattern.
;
of: enfix adapt :of [
    if property = 'type [fail "Use KIND OF not type of"]
    if property = 'kind [property: 'type]
]

quasiform!: word!  ; conflated, but can work in a very limited sense
quasi?: func3 [v <local> spelling] [
    if not word? v [return false]
    spelling: as text! v
    if #"~" <> first spelling [return false]
    if #"~" <> last spelling [return false]
    return true
]
unquasi: func3 [v <local> spelling] [
    assert [quasi? v]
    spelling: to text! v
    assert [#"~" = take spelling]
    assert [#"~" = take/last spelling]
    return as word! spelling
]


; Used to be `false and [print "TRUE" false]` would avoid running the right
; hand side, but a GROUP! would be run.  That was deemed ugly, so group
; now short-circuits.
;
and: enfix :lib3/and [assert [not block? right] right: as block! :right]
or: enfix :lib3/or [assert [not block? right] right: as block! :right]

eval: :do

to-logic: func3 [return: [logic!] optional [<opt> any-value!]] [
    case [
        void? :optional [fail "Can't turn void (null proxied) TO-LOGIC"]
        null? :optional [false]
        true = :optional [true]
        true [true]
    ]
]


; We don't have antiforms in the bootstrap build.  But if a branch produces
; NULL it will yield a "VOID!" (kind of like the quasiform ~)  Turn these
; into NULL, and trust that the current build will catch cases of something
; like a PRINT being turned into a NULL.
;
unrun: func3 [] [
    fail/where "No UNRUN in bootstrap, but could be done w/make FRAME!" 'return
]

has: :in  ; old IN behavior of word lookup achieved by HAS now
overbind: :in  ; works in a limited sense
bindable: func3 [what] [:what]
inside: func3 [where value] [:value]  ; no-op in bootstrap

in: func3 [] [
    fail/where "Use HAS or OVERBIND instead of IN in bootstrap" 'return
]

; !!! This isn't perfect, but it should work for the cases in rebmake
;
load-value: :load
load-all: :load/all

logic-to-word: func3 [logic [logic!]] [
    as word! either logic ["true"] ["false"]  ; want no binding, so AS it
]

reify-logic: func3 [logic [logic!]] [
    either logic ['~true~] ['~false~]
]

; Tricky way of getting simple non-definitional break extraction that looks
; like getting a definitional break.
;
set '^break does [does [:break]]
set '^continue does [does [:continue]]

quote: func3 [x [<opt> any-value!]] [  ; see the more general UNEVAL
    switch kind of x [
        word! [to lit-word! x]  ; to lit-word! not legal in new EXE
        path! [to lit-path! x]  ; to lit-path! not legal in new EXE

        fail/where [
            "QUOTE can only work on WORD!, PATH!, NULL in old Rebols"
        ] 'x
    ]
]

blank-to-void: func3 [x [<opt> any-value!]] [
    either blank? :x [void] [:x]
]


;=== BELOW THIS LINE, TRY NOT TO USE FUNCTIONS IN THE SHIM IMPLEMENTATION ===

; Not sure if LOGIC! will be able to act as its own type in the future, so
; using "weird" definition for now.

char?!: char!  ; modern char is ISSUE! constraint
logic?!: logic!  ; modern logic is ANTIFORM! constraint

; LIT-WORD and REFINEMENT will never be datatypes, so shim the type constraint
; so it works in old parse.

set '&any-word? any-word!  ; modern any-word is type constraint
set '&lit-word? lit-word!  ; modern lit-word is QUOTED! constraint
set '&refinement? refinement!  ; modern refinement is PATH! constraint

element?: :any-value?  ; used to exclude null
any-value?: func3 [x] [true]  ; now inclusive of null

spread: func3 [x [<opt> blank! block!]] [
    case [
        null? :x [return null]
        blank? :x [return blank]
        true [reduce [#splice! x]]
    ]
]

matches: func3 [x [<opt> datatype! typeset! block!]] [
    if :x [if block? x [make typeset! x] else [x]]
]

append: func3 [series value [<opt> any-value!] /line <local> only] [
    any [
        object? series
        map? series
    ] then [
        all [
            block? value
            #splice! = (first value)
        ] else [
            fail/where "Bootstrap shim for OBJECT! only APPENDs SPLICEs" 'return
        ]
        return append3 series second value
    ]

    only: 'only
    case [
        logic? :value [fail/where "APPEND LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-array? series [  ; itemwise appends for strings/etc.
                    for-each item value [
                        append series item
                    ]
                    return series
                ]
                only: _
            ]
        ]
    ]
    append3/(blank-to-void only)/(blank-to-void line) series :value
]

insert: func3 [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        logic? :value [fail/where "INSERT LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-array? series [  ; itemwise appends for strings/etc.
                    for-each item value [
                        series: insert series item
                    ]
                    return series
                ]
                only: void
            ]
        ]
    ]
    insert3/(blank-to-void only)/(blank-to-void line) series :value
]

change: func3 [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        logic? :value [fail/where "CHANGE LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-array? series [
                    fail ["CHANGE to SPLICE not currently in shim"]
                ]
                only: _
            ]
        ]
    ]
    change3/(blank-to-void only)/(blank-to-void line) series :value
]

; It obeys the "as-is" by default rule, so that `join 'a/b [c]` will give the
; predictable outcome of `a/b/[c]`.  This means that SPREAD must be used to
; get the splicing semantics, and plain `join "ab" [c]` is an error.
;
join: func3 [base value [void! any-value!]] [
    if void? :value [
        return copy base
    ]
    append copy base :value  ; shim APPEND, that offers SPLICE behavior
]



; Lambda was redefined to `->` to match Haskell/Elm vs. `=>` for JavaScript.
; It is lighter to look at, but also if the symbol `<=` is deemed to be
; "less than or equal" there's no real reason why `=>` shouldn't be "equal
; or greater".  So it's more consistent to make the out-of-the-box definition
; not try to suggest `<=` and `=>` are "arrows".
;
; !!! Due to scanner problems in the bootstrap build inherited from R3-Alpha,
; and a notion that ENFIX is applied to SET-WORD!s not ACTION!s (which was
; later overturned), remapping lambda to `->` is complicated.
;
do compose3 [(to set-word! first [->]) enfix :lambda]
unset first [=>]


; Historically WRITE did platform line endings (CRLF) when the string had no
; CR in it on Windows.  Ren-C's philosophy is on eliminating CRLF.
;
write: adapt :lib/write [
    if lines [fail ["WRITE/LINES defective in bootstrap EXE (CR in files)"]]
    if text? data [data: to binary! data]
]

; Bootstrap strategy is to base the code on Rebol2-style PARSE, which UPARSE
; should be able to emulate.  While it has the rules of Rebol2 PARSE, the
; result is null on failure in order to be ELSE-triggering.
;
parse2: :lib/parse/match

parse: does [
    fail "Only PARSE2 is available in bootstrap executable, not PARSE"
]
parse3: does [
    fail "Only PARSE2 is available in bootstrap executable, not PARSE3"
]

; Enfixedness was conceived as not a property of an action itself, but of a
; particular relationship between a word and an action.  While this had some
; benefits, it became less and less relevant in a world of "opportunistic
; left quoting constructs":
;
; https://forum.rebol.info/t/moving-enfixedness-back-into-the-action/1156
;
; Since the old version of ENFIX didn't affect its argument, you didn't need
; to say `+: enfix copy :add`.  But for efficiency, you likely would want to
; mutate most functions directly (though this concept is being reviewed).  In
; any case, "enfixed" suggests creating a tweaked version distinct from
; mutating directly.
;
enfixed: enfix :enfix

; EMPTY? in modern Ren-C only considers blanks and tail series empty.
;
empty-or-null?: :empty?


; We want KEEP to be based on the new rules and not have /ONLY.
; (Splices not implemented in bootstrap executable.)
;
collect*: func3 [  ; variant giving NULL if no actual material kept
    return: [<opt> block!]  ; actually BLANK! acts like ~null~, but FUNC3
    body [block!]
    <local> out keeper
][
    out: null
    keeper: specialize (  ; SPECIALIZE to remove series argument
        enclose 'append func3 [f [frame!] <with> out] [  ; gets /LINE, /DUP
            if void? :f/value [return void]  ; doesn't "count" as collected

            f/series: out: default [make block! 16]  ; won't return null now
            :f/value  ; ELIDE leaves as result (F/VALUE invalid after DO F)
            elide do f
        ]
    )[
        series: <replaced>
    ]

    lib/reeval func3 [keep [action!] <with> return] body :keeper

    :out
]

collect: chain [  ; Gives empty block instead of null if no keeps
    :collect*  ; note: does not support , in bootstrap build
    specialize 'else [branch: [copy []]]
]


collect-lets: func3 [
    return: [block!]
    array [block! group!]
    <local> lets
][
    lets: copy []
    for-next item array [
        case [
            item/1 = 'let [
                item: next item
                if match [set-word! word! block!] item/1 [
                    lib/append lets item/1
                ]
            ]
            match [block! group!] item/1 [
                lib/append lets collect-lets item/1
            ]
        ]
    ]
    return lets
]


let: func3 [
    return: []  ; [] was old-style invisibility
    :look [any-value! <...>]  ; old-style variadic
][
    if word? first look [take look]  ; otherwise leave SET-WORD! to runs
]

modernize-typespec: function3 [
    return: [block!]
    types [block!]
][
    types: copy types
    replace types '~null~ <opt>
    replace types 'any-value? [<opt> any-value!]
    replace types 'any-string? 'any-string!
    replace types 'element? 'any-value!
    replace types 'action? 'action!
    replace types 'lit-word? 'lit-word!
    replace types <variadic> <...>
    return types
]

modernize-action: function3 [
    "Account for <maybe> annotation, refinements as own arguments"
    return: [block!]
    spec [block!]
    body [block!]
][
    last-refine-word: _

    tryers: copy []
    proxiers: copy []

    spec: collect3 [  ; Note: offers KEEP/ONLY
        while [not tail? spec] [
            if tag? spec/1 [
                last-refine-word: _
                keep3/only spec/1
                spec: my next
                continue
            ]

            if refinement? spec/1 [  ; REFINEMENT! is ANY-WORD! in this r3
                last-refine-word: as word! spec/1
                keep3/only spec/1

                ; Feed through any TEXT!s following the PATH!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec/1
                ][
                    keep3/only spec/1
                ]

                ; If there's a block specifying argument types, we need to
                ; have a fake proxying parameter.

                if not block? spec/1 [
                    ; append3 proxiers compose3 [  ; no longer turn blank->null
                    ;    (as set-word! last-refine-word)
                    ;        (as get-word! last-refine-word)
                    ;]
                    continue
                ]

                proxy: as word! unspaced [last-refine-word "-arg"]
                keep3/only proxy
                keep3/only modernize-typespec spec/1

                append3 proxiers compose [
                    (as set-word! last-refine-word) (as get-word! proxy)
                    (as set-word! proxy) ~
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args/locals)
            ;
            if w: match any-word! spec/1 [
                if set-word? w [
                    assert [w = first [return:]]
                    keep3 spec/1, spec: my next
                    if [~] = spec/1 [keep3/only [trash!] spec: my next]
                    if tail? spec [continue]
                    if text? spec/1 [keep3 spec/1, spec: my next]
                    if block? spec/1 [
                        keep3/only append3 modernize-typespec spec/1 [
                            <opt> blank!  ; e.g. <maybe> may return "null" blank
                        ]
                        spec: my next
                    ]
                    if tail? spec [continue]
                    if text? spec/1 [keep3 spec/1 spec: my next]
                    continue
                ]

                ; Transform the escapable argument convention, to line up
                ; GET-WORD! with things that are escaped by GET-WORD!s
                ; https://forum.rebol.info/t/1433
                ;
                keep3 case [
                    lit-word? w [to get-word! w]
                    get-word? w [to lit-word! w]
                    true [w]
                ]

                if last-refine-word [
                    fail/where [
                        "Refinements now *are* the arguments:" mold head spec
                    ] 'spec
                ]

                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec/1
                ][
                    keep3/only spec/1
                ]

                if spec/1 = [~] [
                    keep3/only <void>  ; old cue for returning garbage
                    spec: my next
                    continue
                ]

                if spec/1 = [nihil?] [
                    keep3/only []  ; old cue for invisibility
                    spec: my next
                    continue
                ]

                ; Substitute <opt> for any <maybe> found, and save some code
                ; to inject for that parameter to return null if it's null
                ;
                if types: match block! spec/1 [
                    types: modernize-typespec types
                    keep3/only types
                    spec: my next
                    continue
                ]
            ]

            if refinement? spec/1 [
                continue
            ]

            keep3/only spec/1
            spec: my next
        ]
    ]

    ; The bootstrap executable does not have support for true dynamic LET.
    ; We approximate it by searching the body for LET followed by SET-WORD!
    ; or WORD! and add that to locals.
    ;
    append3 spec <local>
    append3 spec collect-lets body  ; append3 splices blocks without /ONLY

    body: compose3 [  ; splices
        (tryers)
        (proxiers)
        (as group! body)  ; compose3 does not splice groups--just blocks
        return ~  ; functions now default to returning trash
    ]
    return reduce [spec body]
]

func: adapt :func3 [set [spec body] modernize-action spec body]
lambda: func3 [spec body] [
    set [spec body] modernize-action spec body
    if not tail? next find spec <local> [
        fail "Lambda bootstrap doesn't support <local>"
    ]
    take find spec <local>
    make action! compose3/only [(spec) (body)]  ; gets no RETURN
]

function: does [
    fail "gathering FUNCTION deprecated (will be synonym for FUNC, eventually)"
]


meth: enfix adapt :lib/meth [set [spec body] modernize-action spec body]
method: func3 [] [
    fail/where "METHOD deprecated temporarily, use METH" 'return
]

trim: adapt :trim [  ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [(not tail? series) and (series/1 = LF)] [
            take series
        ]
    ]
]

call*: adapt 'call [
    if block? command [command: compose command]
]
call: specialize :call* [wait: true]

find-reverse: specialize :find [
    reverse: true

    ; !!! Specialize out /SKIP because it was not compatible--R3-Alpha
    ; and Red both say `find/skip tail "abcd" "bc" -1` is none.
    ;
    skip: false
]

find-last: specialize :find [
    ;
    ; !!! Old Ren-C committed for bootstrap had a bug of its own (a big reason
    ; to kill these refinements): `find/reverse tail "abcd" "bc"` was blank.
    ;
    last: true
]

mold: adapt :lib/mold [  ; update so MOLD SPREAD works
    if all [
        block? value
        #splice! = first value
    ][
        only: true
        value: next value
    ]
]

noquote: func3 [x [<opt> any-value!]] [
    switch kind of :x [
        lit-word! [return to word! x]
        lit-path! [return to path! x]
    ]
    :x
]

; We've scrapped the form of refinements via GROUP! for function dispatch, and
; you need to use APPLY now.  This is a poor man's compatibility APPLY
;
; https://forum.rebol.info/t/1813
;
apply: function3 [
    action [action!]
    args [block!]
][
    f: make frame! :action
    params: words of :action

    ; Get all the normal parameters applied
    ;
    result: _
    while [all [:params/1, not refinement? :params/1]] [
        args: evaluate/set args 'result
        f/(to word! :params/1): :result
        params: next params
    ]

    ; Now go by the refinements.  If it's a refinement that takes an argument,
    ; we have to set the refinement to true
    ;
    while [refinement? :args/1] [
        pos: find params args/1 else [fail ["Unknown refinement" args/1]]
        args: evaluate/set (next args) 'result
        any [
            not :pos/2
            refinement? pos/2
        ] then [  ; doesn't take an argument, set it to the logical next value
            if blank? :result [
                f/(to word! pos/1): null
            ] else [
                f/(to word! pos/1): :result
            ]
        ] else [  ; takes an arg, so set refinement to true and set NEXT param
            f/(to word! pos/1): true
            if blank? :result [
                f/(to word! pos/2): null
            ] else [
                f/(to word! pos/2): :result
            ]
        ]
    ]

    do f
]

split-path: func [] [
    fail/where "Use SPLIT-PATH3 in Bootstrap (no multi-return)" 'return
]


=== "SANITY CHECKS" ===


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
    intern code  ; baseline user/lib binding
    obj: make object! []  ; can't bind individual words, fake w/proxy object
    for-each item template [
        if path? item [item: first item]
        case [
            text? item [continue]  ; should only be last item
            get-word? item [
                item: get item
                assert [object? item]
                bind code item
            ]
        ] else [
            assert [word? item]
            append obj spread reduce [item 0]
            obj/(item): get/any item
        ]
    ]
    bind code obj  ; simulates ability to bind to single words
    return code
]
