REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Type: module
    Name: Bootstrap-Shim
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2022 Ren-C Open Source Contributors
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

; The snapshotted Ren-C existed before <maybe> was legal to mark on arguments.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<maybe> integer!]] [...]  ; modern interpreter or already shimmed
] then [
    ; Fall through to the body of this file, we are shimming version ~8994d23
] else [
    if not in (pick system 'options) 'redbol-paths [  ; old shim'd interpreter
        ;
        ; Old bootstrap executables that are already shimmed should not do
        ; tweaks for the modern import.  Otherwise, export load-all: would
        ; overwrite with LOAD instead of LOAD/ALL (for example).  It's just
        ; generally inefficient to shim multiple times.
        ;
        quit
    ]

    system.options.redbol-paths: true  ; new interpreter, make it act older

    === {TWEAKS SO MODERN REN-C DOESN'T ACT TOO MODERN} ===

    export parse: func [] [
        fail/where "Use PARSE2 in Bootstrap Process, not UPARSE/PARSE" 'return
    ]

    ; Bootstrap EXE doesn't support multi-returns so SPLIT-PATH takes a /DIR
    ; refinement of a variable to write to.
    ;
    export split-path: enclose (augment :split-path [/dir [word!]]) f -> [
        let dir: f.dir
        let results: unquasi ^ do f  ; no [...]: in bootstrap load of this file
        set maybe dir unmeta second results
        unmeta first results
    ]

    export transcode: enclose (augment :transcode [/next [word!]]) func [f] [
        let next: f.next  ; note: contention with NEXT series verb
        f.one: all [next make issue! 0]  ; # is invalid issue in bootstrap
        let result: ^ (do f except e -> [return raise e])
        if result = null' [return null]
        set maybe next unmeta second unquasi result
        return unmeta first unquasi result
    ]

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

    export strip-commas-and-null-apostrophes: x -> [x]  ; not needed

    quit
]


;=== THESE REMAPPINGS ARE OKAY TO USE IN THE BOOTSTRAP SHIM ITSELF ===

set '~ :null3  ; most similar behavior to bad-word isotope available
none: :void3  ; again, most similar thing

; Done is used as a signal in the boot files that the expected end is reached.
; This is a BAD-WORD! in modern Ren-C, but a plain word in the bootstrap EXE.
; Must use SET because even though we don't run this in modern Ren-C, the file
; gets scanned...and `~done~:` would be invalid.
;
set '~done~ :void3

null?: :blank?
null: blank

void?: :null3?
void: :null3

repeat: :loop

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
        all3 [
            block? :product
            #splice! = first product
        ] then [
            ; Doing the change can insert more than one item, update pos
            ;
            pos: change3/part pos second product 1
        ] else [
            case3 [
                ;
                ; Permit NULL but only in bootstrap build (as it has no pure
                ; void and null is the product of non-branch conditionals).
                ; Trust current build to catch errors.
                ;
                null3? :product [
                    change3/part pos null3 1
                ]
                void3? :product [  ; e.g. compose [(if true [null])]
                    comment [change3/part pos null 1]  ; we *could* support it
                    fail/where "#[void] compose found, disabled" 'return
                ]
                blank? :product [
                    fail/where "COMPOSE blanks with SPREAD [_]" 'return
                ]
            ] else [
                change3/only pos :product
                pos: next pos
            ]
        ]
    ]
    return result
]

isotopify-blanks: func3 [x [<opt> any-value!]] [
    lib3/if blank? :x [return void3]
    :x
]
null3-to-blank: func3 [x [<opt> any-value!]] [
    lib3/if null3? :x [return blank]
    :x
]

any: chain [:any3 :null3-to-blank]
all: chain [:all3 :null3-to-blank]
get: chain [:lib3/get :null3-to-blank]
find: chain [
    adapt :find3 [
        if blank? value [
            fail "Can't FIND a NULL (BLANK! in shim), use MAYBE"
        ]
    ]
    :null3-to-blank
]
select: chain [
    adapt :select3 [
        if blank? value [
            fail "Can't SELECT a NULL (BLANK! in shim), use MAYBE"
        ]
    ]
    :null3-to-blank
]

if: chain [:lib3/if :isotopify-blanks]
case: chain [:lib3/case :isotopify-blanks]
switch: chain [:lib3/switch :isotopify-blanks]

quasi!: word!  ; conflated, but can work in a very limited sense
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

else: enfix chain [
    adapt :lib3/else [if blank? :optional [optional: null3]]
    ; we don't isotopify blanks to simulate isotopic null via shim blank null
]
then: enfix chain [
    adapt :lib3/then [if blank? :optional [optional: null3]]
    :isotopify-blanks
]
also: enfix adapt :lib3/also [if blank? :optional [optional: null3]]


; Modern DO is more limited in giving back "void intent" so it doesn't go
; well in situations like `map-each code blocks-of-code [do code]`...because
; situations that would have returned NULL and opted out don't opt out.
; You are supposed to use EVAL for that.
;
reeval: chain [:reeval3 :null3-to-blank]
eval: :do


did: func3 [return: [logic!] optional [<opt> any-value!]] [
    not any [
        blank? :optional  ; acts like NULL
        null3? :optional  ; acts like VOID
    ]
]

didn't: chain [:did :not]

to-logic: func3 [return: [logic!] optional [<opt> any-value!]] [
    case [
        null3? :optional [fail "Can't turn void (null proxied) TO-LOGIC"]
        blank? :optional [false]
        true [to logic! :optional]
    ]
]


; We don't have isotopes in the bootstrap build.  But if a branch produces
; NULL it will yield a "VOID!" (kind of like a QUASI! of ~void~)  Turn these
; into NULL, and trust that the current build will catch cases of something
; like a PRINT being turned into a NULL.
;
~null~: :null3  ; e.g. _
~true~: #[true]
~false~: #[false]
decay: func3 [v [<opt> any-value!]] [
    assert [not null3? :v]
    if void3? :v [fail "Attempt to decay a void, may have been _, try ~null~"]
    if null3? :v [fail "Attempt to decay a blank where ~null~ may be meant"]
    if :v = '~null~ [return null]
    if :v = '~ [fail "decay ~ would be ambiguous with decay '"]
    if :v = the3 ' [return void]
    :v
]
reify: func3 [v [<opt> any-value!]] [
    if void? :v [return the3 ']  ; ambiguous with ~, but favor invisibility
    if null? :v [return '~null~]
    if :v = #[true] [return '~true~]
    if :v = #[false] [return '~false~]
    :v
]
unrun: func3 [v [action!]] [:v]
opt: ~  ; replaced by DECAY word
try: ~  ; reviewing uses


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

maybe: enfix func3 [
    :left [<skip> set-word!]
    v [<opt> any-value!]
][
    if :left [
        assert [set-word? left]
        if not null3? :v [
            set left :v
        ] else [
            set 'v get left
        ]
    ]
    if null? :v [return null3]
    if blank? :v [return null3]
    :v
]

the: :the3  ; Renamed due to the QUOTED! datatype
quote: func3 [x [<opt> any-value!]] [
    switch type of x [
        null [the ()]
        word! [to lit-word! x]
        path! [to lit-path! x]

        fail/where [
            "QUOTE can only work on WORD!, PATH!, NULL in old Rebols"
        ] 'x
    ]
]


;=== BELOW THIS LINE, TRY NOT TO USE FUNCTIONS IN THE SHIM IMPLEMENTATION ===


any-inert!: make typeset! [
    any-string! binary! char! any-context! time! date! any-number! object!
]

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
        null3? :value []
        void3? :value [fail/where "APPEND of VOID! disallowed" 'value]
        blank? :value [fail/where "APPEND blanks with [_] only" 'value]
        logic? :value [fail/where "APPEND LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if find3 value void! [
                fail/where "APPEND of BLOCK! w/VOID! disallowed" 'value
            ]
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
    append3/(only)/(line) series :value
]

insert: func3 [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        null3? :value []
        void3? :value [fail/where "INSERT of VOID! disallowed" 'value]
        blank? :value [fail/where "INSERT blanks with [_] only" 'value]
        logic? :value [fail/where "INSERT LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if find3 value void! [
                fail/where "INSERT of BLOCK! w/VOID! disallowed"
            ]
            if #splice! = (first value) [
                value: second value
                if not any-array? series [  ; itemwise appends for strings/etc.
                    for-each item value [
                        series: insert series item
                    ]
                    return series
                ]
                only: _
            ]
        ]
    ]
    insert3/(only)/(line) series :value
]

change: func3 [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        null3? :value []
        void3? :value [fail/where "CHANGE of VOID! disallowed" 'value]
        blank? :value [fail/where "CHANGE blanks with [_] only" 'value]
        logic? :value [fail/where "CHANGE LOGIC! LOGIC-TO-WORD, REIFY" 'value]
        block? :value [
            if find3 value void! [
                fail/where "CHANGE of BLOCK! w/VOID! disallowed" 'value
            ]
            if #splice! = (first value) [
                value: second value
                if not any-array? series [
                    fail ["CHANGE to SPLICE not currently in shim"]
                ]
                only: _
            ]
        ]
    ]
    change3/(only)/(line) series :value
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
unset3 first [=>]

; SET was changed to accept BAD-WORD! isotopes
;
set: specialize :lib/set [opt: true]


; Historically WRITE did platform line endings (CRLF) when the string had no
; CR in it on Windows.  Ren-C's philosophy is on eliminating CRLF.
;
write: adapt :lib/write [
    if lines [fail ["WRITE/LINES defective in bootstrap EXE (CR in files)"]]
    if text? data [data: to binary! data]
]

; PARSE is being changed to a more powerful interface that returns synthesized
; parse products.  So just testing for matching or not is done with PARSE?,
; to avoid conflating successful-but-null-bearing-parses with failure.
;
parse2: func3 [series rules] [
    ;
    ; Make it so that if the rules end in `|| <input>` then the parse will
    ; return the input.
    ;
    if lib/all [
        (the <input>) = last rules
        (the ||) =  first back tail rules
    ][
        rules: copy rules
        take back tail rules
        take back tail rules
        if not lib/parse series rules [return null]
        return series
    ]

    if not lib/parse series rules [return null]
    return lib/void
]
parse: does [
    fail "Only PARSE2 is available in bootstrap executable, not PARSE"
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


; COLLECT in the bootstrap version would return NULL on no keeps.  But beyond
; wanting to change that, we also want KEEP to be based on the new rules and
; not have /ONLY.  So redo it here in the shim.
;
collect*: func3 [  ; variant giving NULL if no actual material kept
    return: [blank! block!]  ; actually BLANK! acts like <opt>, but FUNC3
    body [block!]
    <local> out keeper
][
    out: _
    keeper: specialize (  ; SPECIALIZE to remove series argument
        enclose 'append func3 [f [frame!] <with> out] [  ; gets /LINE, /DUP
            if null? :f/value [return null]  ; doesn't "count" as collected

            f/series: out: default [make block! 16]  ; won't return null now
            :f/value  ; ELIDE leaves as result (F/VALUE invalid after DO F)
            elide do f
        ]
    )[
        series: <replaced>
    ]

    lib/eval func3 [keep [action!] <with> return] body :keeper

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
                if match3 [set-word! word! block!] item/1 [
                    lib/append lets item/1
                ]
            ]
            match3 [block! group!] item/1 [
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

            if refinement? spec/1 [  ; REFINEMENT! is a word in this r3
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
                keep3/only spec/1

                append3 proxiers compose [
                    (as set-word! last-refine-word) lib3/try (as get-word! proxy)
                    set (as lit-word! proxy) void
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args/locals)
            ;
            if w: match3 any-word! spec/1 [
                if set-word? w [
                    assert [w = first [return:]]
                    keep3 spec/1, spec: my next
                    if <none> = spec/1 [keep3 <void>, spec: my next]
                    if tail? spec [continue]
                    if text? spec/1 [keep3 spec/1, spec: my next]
                    if block? spec/1 [
                        keep3/only append3 copy spec/1 [
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

                if spec/1 = <none> [  ; new semantics: <none> -> ~[]~
                    keep3/only <void>  ; old cue for returning garbage
                    spec: my next
                    continue
                ]

                if spec/1 = <void> [
                    keep3/only []  ; old cue for invisibility
                    spec: my next
                    continue
                ]

                ; Substitute <opt> for any <maybe> found, and save some code
                ; to inject for that parameter to return null if it's null
                ;
                if types: match3 block! spec/1 [
                    types: copy types
                    if find3 types <opt> [  ; <opt> first (<maybe> is real opt)
                        replace types <opt> 'blank!
                    ]
                    if find3 types <maybe> [
                        replace types <maybe> <opt>
                        append3 tryers compose [  ; splices
                            if null3? (as get-word! w) [return _]
                        ]
                    ]
                    replace types <variadic> <...>
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
    ]
    return reduce [spec body]
]

func: adapt :func3 [set [spec body] modernize-action spec body]
function: adapt :function3 [set [spec body] modernize-action spec body]

; Bootstrap MATCH was designed very strangely as a variadic for some since
; dropped features.  But it seems to not be able to be CHAIN'd or ADAPTed
; due to that quirky interface.  It's simple, just rewrite it.
;
match: func [
    return: [<opt> any-value!]
    types [block! datatype! typeset!]
    value [<maybe> any-value!]
][
    case [
        datatype? types [types: make typeset! reduce [types]]  ; circuitious :-(
        block? types [types: make typeset! types]
    ]
    if find3 types type of value [return value]
    return _
]


meth: enfixed adapt :lib/meth [set [spec body] modernize-action spec body]
method: func3 [] [
    fail/where "METHOD deprecated temporarily, use METH" 'return
]

for-each: func [  ; add opt-out ability with <maybe>
    return: [<opt> any-value!]
    'vars [word! lit-word! block!]
    data [<maybe> any-series! any-context! map! datatype! action!]
    body [block!]
][
    return for-each3 (vars) data body else [_]
]

trim: adapt :trim [  ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [(not tail? series) and (series/1 = LF)] [
            take series
        ]
    ]
]

mutable: func3 [x [any-value!]] [
    ;
    ; Some cases which did not notice immutability in the bootstrap build
    ; now do, e.g. MAKE OBJECT! on a block that you LOAD.  This is a no-op
    ; in the older build, but should run MUTABLE in the new build when it
    ; emerges as being needed.
    ;
    :x
]


; Historical JOIN reduced.  Modern JOIN does not; one of the big justifications
; of its existence is the assembly of PATH! and TUPLE! which are immutable
; and can't use normal APPEND.
;
; It obeys the "as-is" by default rule, so that `join 'a/b [c]` will give the
; predictable outcome of `a/b/[c]`.  This means that SPREAD must be used to
; get the splicing semantics, and plain `join "ab" [c]` is an error.
;
join: func3 [base value [<opt> any-value!]] [
    append copy base :value  ; shim APPEND, that offers SPLICE behavior
]

; https://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
has: ~

const?: func3 [x] [return false]

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


; The bootstrap executable was picked without noticing it had an issue with
; reporting errors on file READ where it wouldn't tell you what file it was
; trying to READ.  It has been fixed, but won't be fixed until a new bootstrap
; executable is picked--which might be a while since UTF-8 Everywhere has to
; stabilize and speed up.
;
; So augment the READ with a bit more information.
;
lib-read: copy :lib/read
lib/read: read: enclose :lib-read function3 [f [frame!]] [
    saved-source: :f/source
    if e: trap [bin: do f] [
        parse2 e/message [
            [
                {The system cannot find the } ["file" | "path"] { specified.}
                | "No such file or directory"  ; Linux
            ]
            to end
        ] then [
            fail/where ["READ could not find file" saved-source] 'f
        ]
        print "Some READ error besides FILE-NOT-FOUND?"
        fail e
    ]
    bin
]

transcode: function3 [
    return: [<opt> any-value!]
    source [text! binary!]
    /next
    next-arg [word!]
][
    next: lib/try :next-arg

    values: lib/transcode/(either next ['next] [blank])
        either text? source [to binary! source] [source]
    pos: take/last values
    assert [binary? pos]

    if next [
        assert [1 >= length of values]

        ; In order to return a text position in pre-UTF-8 everywhere, fake it
        ; by seeing how much binary was consumed and assume skipping that many
        ; bytes will sync us.  (From @rgchris's LOAD-NEXT).
        ;
        if text? source [
            rest: to text! pos
            pos: skip source subtract (length of source) (length of rest)
        ]
        set next pos
        return pick values 1  ; may be null
    ]

    return values
]

split: function3 [
    return: [block!]
    series [any-series!]
    dlm "Split size, delimiter(s) (if all integer block), or block rule(s)"
        [block! integer! char! bitset! text! tag! word! bar!]
    /into "If dlm is integer, split in n pieces (vs. pieces of length n)"
][
    all [
        any-string? series
        tag? dlm
    ] then [
        dlm: form dlm
    ]
    any [
        word? dlm
        bar? dlm  ; just a WORD! `|` in non-bootstrap executable
        tag? dlm
    ] then [
        return lib/collect [  ; Note: offers KEEP/ONLY
            keep []  ; so bootstrap COLLECT won't be NULL if no KEEPs

            parse2 series [
                some [
                    copy t: [to dlm | to end]
                    (keep/only t)
                    opt thru dlm
                ]
                end
            ]
        ]
    ]

    apply :lib/split [series: series dlm: dlm into: into]
]

; Unfortunately, bootstrap delimit treated "" as not wanting a delimiter.
; Also it didn't have the "literal BLANK!s are space characters" behavior.
;
delimit: func [
    return: [<opt> text!]
    delimiter [<opt> char! text!]
    line [<maybe> text! block!]
    <local> text value pending anything
][
    if text? line [return copy line]

    text: copy ""
    pending: false
    anything: false

    cycle [
        if tail? line [stop]
        if blank? line/1 [
            lib/append text space
            line: next line
            anything: true
            pending: false
            continue
        ]
        line: evaluate/set line 'value
        any [
            null? get 'value
        ] then [
            continue
        ]
        any [
            char? value
            issue? value
        ] then [
            lib/append text form value
            anything: true
            pending: false
            continue
        ]
        if pending [
            if delimiter [lib/append text delimiter]
            pending: false
        ]
        lib/append text form value
        anything: true
        pending: true
    ]
    if not anything [
        assert [text = ""]
        return null
    ]
    text
]

unspaced: specialize :delimit [delimiter: _]
spaced: specialize :delimit [delimiter: space]


noquote: func3 [x [<opt> any-value!]] [
    switch type of :x [
        lit-word! [return to word! x]
        lit-path! [return to path! x]
    ]
    :x
]


; Temporarily work around MATCH usage bug in bootstrap unzip:
;
;    data: if match [file! url! blank!] try :source/2 [
;
; If there is no SOURCE/2, it gets NULL...which it turns into a blank because
; there was no <opt> in match.
;
; But then if that blank matches, it gives ~falsey~ so you don't get misled
; in tests exactly like this one.  (!)
;
; Temporarily make falsey matches just return true for duration of the zip.
; Also, make PRINT accept FILE! and TEXT! so the /VERBOSE option will work.
;
zip: enclose :zip function3 [f] [
    old-match: :match
    old-print: :print

    if f/verbose [
        fail/where [
            "/VERBOSE not working due to PRINT problem, broken in bootstrap"
        ] 'f
    ]

    ; !!! This workaround is crashing the bootstrap EXE, let it go for now
    ;lib/print: adapt :print [
    ;    if match [file! text!] :line [
    ;        line: reduce [line]
    ;    ]
    ;]

    lib/match: func3 [type value [<opt> any-value!] <local> answer] [
        if quasi? set* 'answer old-match type value [
            return true
        ]
        return get 'answer
    ]

    result: do f

    lib/match: :old-match
    ;lib/print: :old-print

    return result
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
    f: make frame! unrun :action
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
                f/(to word! pos/1): null3
            ] else [
                f/(to word! pos/1): :result
            ]
        ] else [  ; takes an arg, so set refinement to true and set NEXT param
            f/(to word! pos/1): true
            if blank? :result [
                f/(to word! pos/2): null3
            ] else [
                f/(to word! pos/2): :result
            ]
        ]
    ]

    do f
]

local-to-file: lib/local-to-file: func3 [path [<opt> text! file!] /pass /dir] [
    path: default [_]
    local-to-file3/(pass)/(dir) path
]

file-to-local: lib/file-to-local: func3 [
    path [<opt> text! file!] /pass /full /no-tail-slash /wild
][
    path: default [_]
    file-to-local3/(pass)/(full)/(no-tail-slash)/(wild) path
]

select: func3 [
    series [<opt> any-series! any-context! map!]
    value [any-value!]
][
    if null? :series [return null]
    select3 :series :value
]

split-path: func3 [  ; interface changed to multi-return in new Ren-C
    return: [file!]
    in [file! url!]
    /dir  ; no multi-return, simulate it
    darg [word!]
    <local> path+file
][
    dir+file: split-path3 in
    if dir [set darg decay first dir+file]
    return decay second dir+file
]


=== {SANITY CHECKS} ===

if not void3? (
    if true [null] else [fail "ELSE shim running when it shouldn't"]
) [
    fail "shim IF/ELSE did not voidify null result"
]

if not all [
    null? either true [null] [<unused>]
    null? either false [<unused>] [null]
][
    fail "EITHER not preserving null"
]
