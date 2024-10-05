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


if not find (words of import/) 'into [
    ; no /INTO means error here, so old r3 without import shim

    ; Don't use -{...}- in this error message, because if this message is
    ; being reported then this interpreter will not understand it.

    print ""
    print "!!! Bootstrapping with older Ren-C requires passing %import-shim.r"
    print "on the command line with the --import option, e.g."
    print ""
    print "    r3 --import import-shim.r make.r"
    print ""
    print "...instead of just `r3 make.r`.  Otherwise make.r can't use things"
    print "like -{...}- strings in older Ren-C."
    print ""
    print "(See %import-shim.r for more details)"
    print ""
    quit 1
]


; The bootstrap executable does not have SPREAD (or isotopes, generally)
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
sys.util/rescue [
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

    export /parse: func [] [
        fail:where "Use PARSE3 in Bootstrap Process, not UPARSE/PARSE" 'return
    ]

    export /split-path3: enclose (
        augment split-path/ [:file [any-word? any-path?]]
    ) f -> [
        let file: f.file
        let results: meta:lite eval f  ; no [...]: in bootstrap load of file
        set maybe file unmeta results.2
        unmeta results.1
    ]
    export /split-path: func [] [
        fail:where "Use SPLIT-PATH3 in Bootstrap (no multi-return)" 'return
    ]

    export /transcode: enclose (
        augment lib.transcode/ [:next3 [word!] "set to transcoded value"]
    ) func [f] [
        if not f.next3 [
            return eval f
        ]
        f.next: okay
        let [dummy block]
        block: compose [dummy (to path! f.next3)]  ; no SET-BLOCK, @, in boot
        return eval compose [(setify block) eval f]
    ]

    export /cscape-inside: inside/  ; modern string interpolation tool

    ; LOAD changed to have no /ALL, so it always enforces getting a block.
    ; But LOAD-VALUE comes in the box to load a single value.
    ;
    export /load-all: load/

    export /for: func [] [
        fail:where "FOR is being repurposed, use CFOR" 'return
    ]

    export /unless: func [/dummy] [
        fail:where "Don't use UNLESS in Bootstrap, definition in flux" 'dummy
    ]

    export /boolean?!: boolean?/
    export /onoff?!: onoff?/
    export /yesno?!: yesno?/

    quit 0
]


=== "ENCLOSE REST OF MODULE IN GROUP TO AVOID OVERWRITING TOP-LEVEL DECLS" ===

; This module does things like overwrite IF.  But were we to make a top-level
; statement like `if: ...` then in a modern executable, before any code in
; the module ran, IF would be WRAP*'d as a module-level declaration and set
; to nothing.  To avoid this, we simply put all the emulation code in a group.

(  ; closed at end of file


=== "SHORT NAMES FOR LIB3/XXX, CATCH USES OF SHIMMED FUNCTIONS BEFORE SHIM" ===

; The shims use the functions in the bootstrap EXE's lib to make forwards
; compatible variations.  But it's easy while editing this file to get
; confused and use the wrong version--possibly before it has been defined.
; This defaults all the functions that need shims to errors, and offer a
; shorter name for lib3 variants while doing so (e.g. FUNC3 for LIB3/FUNC)
;
; 1. Using SET-WORD!s here also helps searchability if you're looking for
;    where `func3: ...` is defined.

eval: :do

for-each [alias] [  ; SET-WORD!s for readability + findability [1]
    func3:                      ; FUNC refinements are their own args, more...
    function3:                  ; no FUNCTION at present (TBD: FUNC synonym)
    append3:                    ; APPEND handles splices
    change3:                    ; CHANGE handles splices
    insert3:                    ; INSERT handles splices
    join3:                      ; JOIN handles splices
    compose3:                   ; COMPOSE processes splices
    split-path3:                ; bootstrap uses SPLIT-PATH3 not SPLIT-PATH
    collect3:                   ; COLLECT's KEEP processes splices
    mold3:                      ; MOLD takes splices instead of MOLD/ONLY
    and3:                       ; AND takes GROUP!s on right (not BLOCK!)
    or3:                        ; OR takes GROUP!s on right (not BLOCK!)
][
    ; Assign the alias what the existing version (minus the terminal "3") is
    ; (e.g. func3: :func)
    ;
    name: copy as text! alias
    assert [#"3" = take/last name]
    name: to word! name
    lib3/append system.contexts.user reduce [alias :lib3.(name)]

    ; Make calling the undecorated version an error, until it becomes shimmed
    ; (e.g. func: does [fail ...])
    ;
    error: spaced [
        (lib/mold name) "not shimmed yet, see" as word! (lib/mold alias)
    ]
    system.contexts.user.(name): lib3/func [] lib3/compose [
        fail/where (error) 'return
    ]
]

function3: func3 [] [
    fail/where "FUNCTION slated for synonym of FUNC, so no FUNCTION3" 'return
]


=== "WORD!-BASED LOGIC" ===

; Modern Ren-C has no LOGIC! fundamental datatype.  The words TRUE and FALSE
; are used as the currency of logic, while IF only tests for nullness (or
; NaN-ness).

if: adapt :if [
    all [
        :condition
        find [true false yes no on off] :condition
        fail/where "IF not supposed to take [true false yes no off]" 'return
    ]
]

either: adapt :either [
    all [
        :condition
        find [true false yes no on off] :condition
        fail/where "EITHER not supposed to take [true false yes no off]" 'return
    ]
]

boolean?: func3 [x] [any [:x = 'true, :x = 'false]]
yesno?: func3 [x] [any [:x = 'on, :x = 'off]]
onoff?: func3 [x] [any [:x = 'yes, :x = 'no]]

boolean?!: word!
onoff?!: word!
yesno?!: word!

logic?!: make typeset! [~null~ nothing!]
to-logic: func3 [x] [
    either x [~] [null]
]

boolean: func3 [x [~null~ any-value!]] [
    either x ['true] ['false]
]

to-yesno: func3 [x [~null~ any-value!]] [  ; should this be DID?
    either x ['yes] ['no]
]

ok: okay: true
okay?: :true?


=== "MAKE THE KEEP IN COLLECT3 OBVIOUS AS KEEP3" ===

; Even if you see you are using the COLLECT3 version of COLLECT, it's easy
; to not remember that the KEEP is the kind that takes /ONLY.  Renaming the
; keeper to KEEP3 makes that clearer.

collect3: adapt get $collect3 [
    body: compose3 [
        keep3: :keep  ; help point out keep3 will splice blocks, has /ONLY
        keep: ~
        (body)  ; splices block body because it's COMPOSE3
    ]
]


=== "BACKWARDS-LEANING BOOTSTRAP FUNCTIONS (SHIM ERRORS)" ===

; At the top of the file, modern executables are dialed back on features which
; can't be emulated by older executables.  Here we raise errors in the old
; executable on any undecorated functions that have no emulation equivalent.

parse3: get $lib/parse

parse: does [
    fail "Only PARSE3 is available in bootstrap executable, not PARSE"
]

split-path: func3 [] [
    fail/where "Use SPLIT-PATH3 in Bootstrap (no multi-return)" 'return
]

=== "THESE REMAPPINGS ARE OKAY TO USE IN THE BOOTSTRAP SHIM ITSELF" ===

; Done is used as a signal in the boot files that the expected end is reached.
; This is a QUASI-WORD? in modern Ren-C, but a plain word in the bootstrap EXE.
; Must use SET because even though we don't run this in modern Ren-C, the file
; gets scanned...and `~done~:` would be invalid.
;
set '~done~ does [~]


=== "FAKE UP QUASIFORM! AND THE-WORD! TYPES" ===

; In the case of the iteration functions, they take ISSUE! (a WORD! type in
; the bootstrap executable) to mean that the variable has a binding already
; to use vs. create a new one.  It's essential to use with ITERATE in modern
; Ren-C, but we can't say `iterate @block [...]` in bootstrap (no @).  Hence
; instead, INERT (which adds @ in new executables) is defined to add a #.

inert: func3 [word [word!]] [return to-issue word]

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
and: enfix :and3 [assert [not block? right] right: as block! :right]
or: enfix :or3 [assert [not block? right] right: as block! :right]

to-logic: func3 [return: [logic!] optional [~null~ any-value!]] [
    case [
        void? :optional [fail "Can't turn void (null proxied) TO-LOGIC"]
        null? :optional [false]
        true = :optional [true]
        true [true]
    ]
]

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
load-value: get $load
load-all: get $load/all

; Tricky way of getting simple non-definitional break extraction that looks
; like getting a definitional break.
;
set '^break does [does [:break]]
set '^continue does [does [:continue]]

quote: func3 [x [~null~ any-value!]] [  ; see the more general UNEVAL
    switch type of x [
        word! [to lit-word! x]  ; to lit-word! not legal in new EXE
        path! [to lit-path! x]  ; to lit-path! not legal in new EXE

        fail/where [
            "QUOTE can only work on WORD!, PATH!, NULL in old Rebols"
        ] 'x
    ]
]

blank-to-void: func3 [x [~null~ any-value!]] [
    either blank? :x [void] [:x]
]


;=== BELOW THIS LINE, TRY NOT TO USE FUNCTIONS IN THE SHIM IMPLEMENTATION ===

; Not sure if LOGIC! will be able to act as its own type in the future, so
; using "weird" definition for now.

char?!: char!  ; modern char is ISSUE! constraint
logic?!: logic!  ; modern logic is ANTIFORM! constraint
set-word?!: set-word!  ; modern set-word is CHAIN constraint
get-word?!: get-word!  ; modern get-word is CHAIN constraint
set-tuple?!: set-path!  ; modern tuple exists, set-tuple is CHAIN constraint
get-tuple?!: get-path!  ; modern tuple exists, set-tuple is CHAIN constraint
set-word!: func3 [] [
    fail/where "SET-WORD! is now a CHAIN!, try SET-WORD?!" 'return
]
get-word!: func3 [] [
    fail/where "GET-WORD! is now a CHAIN!, try GET-WORD?!" 'return
]
set-path!: func3 [] [
    fail/where "SET-PATH! can no longer exist, try SET-TUPLE?!" 'return
]
get-path!: func3 [] [
    fail/where "GET-PATH! can no longer exist, try GET-TUPLE?!" 'return
]

setify: func3 [plain [word! path!]] [
    either word? plain [to-set-word plain] [to-set-path plain]
]


; LIT-WORD and REFINEMENT will never be datatypes, so shim the type constraint
; so it works in old parse.

set '&any-word? any-word!  ; modern any-word is type constraint
set '&lit-word? lit-word!  ; modern lit-word is QUOTED! constraint
set '&refinement? refinement!  ; modern refinement is PATH! constraint

element?: :any-value?  ; used to exclude null
any-value?: func3 [x] [true]  ; now inclusive of null

matches: func3 [x [~null~ datatype! typeset! block!]] [
    if :x [if block? x [make typeset! x] else [x]]
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
    return: [~void~ ~null~ block!]
    x [~null~ blank! block!]
][
    case [
        null? :x [return null]
        blank? :x [return void]
        true [reduce [#splice! x]]
    ]
]

append: func3 [series value [any-value!] /line <local> only] [
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
        logic? :value [
            fail/where "APPEND LOGIC! ILLEGAL, use REIFY, BOOLEAN, etc." 'value
        ]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-list? series [  ; itemwise appends for strings/etc.
                    for-each 'item value [
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

insert: func3 [series value [any-value!] /line <local> only] [
    only: 'only
    case [
        logic? :value [
            fail/where "INSERT LOGIC! ILLEGAL, use REIFY, BOOLEAN, etc." 'value
        ]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-list? series [  ; itemwise appends for strings/etc.
                    for-each 'item value [
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

change: func3 [series value [any-value!] /line <local> only] [
    only: 'only
    case [
        logic? :value [
            fail/where "CHANGE LOGIC! ILLEGAL, use REIFY, BOOLEAN, etc." 'value
        ]
        block? :value [
            if #splice! = (first value) [
                value: second value
                if not any-list? series [
                    fail ["CHANGE to SPLICE not currently in shim"]
                ]
                only: _
            ]
        ]
    ]
    change3/(blank-to-void only)/(blank-to-void line) series :value
]

join: func3 [
    base [binary! any-string! path!]
    value [void! any-value!]
][
    if void? :value [
        return copy base
    ]
    append copy base :value  ; not APPEND3, shim APPEND w/SPLICE behavior
]

collect*: func3 [  ; variant giving NULL if no actual material kept
    return: [~null~ block!]
    body [block!]
    <local> out keeper
][
    out: null
    keeper: specialize (  ; SPECIALIZE to remove series argument
        enclose 'append func3 [f [frame!] <with> out] [  ; gets /LINE, /DUP
            assert [not action? get $f.value]
            if void? f.value [return void]  ; doesn't "count" as collected

            f.series: out: default [make block! 16]  ; won't return null now
            f.value  ; ELIDE leaves as result (F.VALUE invalid after EVAL F)
            elide eval f
        ]
    )[
        series: <replaced>
    ]

    reeval func3 [keep [action!] <with> return] body get $keeper

    out
]

collect: cascade [  ; Gives empty block instead of null if no keeps
    :collect*  ; note: does not support , in bootstrap build
    specialize 'else [branch: [copy []]]
]

compose: func3 [block [block!] /deep <local> result pos product count] [
    if deep [
        fail/where "COMPOSE bootstrap shim doesn't recurse, yet" 'block
    ]
    pos: result: copy block
    while [not tail? pos] [
        if not group? pos.1 [
            pos: next pos
            continue
        ]

        product: eval pos.1
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
                nothing? :product [  ; e.g. compose [(if ok [null])]
                    fail/where "nothing compose found" 'return
                ]
            ] else [
                change3/only pos :product
                pos: next pos
            ]
        ]
    ]
    return result
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

collect-lets: func3 [
    return: [block!]
    list [block! group!]
    <local> lets
][
    lets: copy []
    for-next 'item list [
        case [
            item.1 = 'let [
                item: next item
                if match [set-word?! word! block!] item.1 [
                    append3 lets item.1
                ]
            ]
            match [block! group!] item.1 [
                append3 lets collect-lets item.1
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

modernize-typespec: func3 [
    return: [block!]
    types [block!]
][
    types: copy types
    replace types 'any-value? [~null~ any-value!]
    replace types 'any-string? 'any-string!
    replace types 'element? 'any-value!
    replace types 'action? 'action!
    replace types 'lit-word? 'lit-word!
    replace types <variadic> <...>
    return types
]

modernize-action: func3 [
    "Account for <maybe> annotation, refinements as own arguments"
    return: [block!]
    spec [block!]
    body [block!]
    <local> last-refine-word tryers proxiers proxy w types new-spec new-body
][
    last-refine-word: null

    tryers: copy []
    proxiers: copy []

    new-spec: collect3 [  ; Note: offers KEEP/ONLY
        while [not tail? spec] [
            if tag? spec.1 [
                last-refine-word: null
                keep3/only spec.1
                spec: my next
                continue
            ]

            if lib3/refinement? spec.1 [  ; old refinement
                fail/where ["Old refinement in spec:" mold spec] 'spec
            ]

            if get-word? spec.1 [  ; new refinement (GET-WORD is ANY-WORD!)
                last-refine-word: as word! spec.1
                keep3/only to refinement! spec.1

                ; Feed through any TEXT!s following the PATH!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec.1
                ][
                    keep3/only spec.1
                ]

                ; If there's a block specifying argument types, we need to
                ; have a fake proxying parameter.

                if not block? spec.1 [
                    continue
                ]

                proxy: as word! unspaced [last-refine-word "-arg"]
                keep3/only proxy
                keep3/only modernize-typespec spec.1

                append3 proxiers compose [
                    (to-set-word last-refine-word) (to-get-word proxy)
                    (to-set-word proxy) ~
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args and locals)
            ;
            if w: match any-word! spec.1 [
                if set-word? w [
                    assert [w = first [return:]]
                    keep3 spec.1, spec: my next
                    if tail? spec [continue]
                    if text? spec.1 [keep3 spec.1, spec: my next]
                    if block? spec.1 [
                        keep3/only modernize-typespec spec.1
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
                    text? spec.1
                ][
                    keep3/only spec.1
                ]

                if spec.1 = [nihil?] [
                    keep3/only []  ; old cue for invisibility
                    spec: my next
                    continue
                ]

                if types: match block! spec.1 [
                    types: modernize-typespec types
                    keep3/only types
                    spec: my next
                    continue
                ]
            ]

            if get-word? spec.1 [  ; new refinement
                continue
            ]

            keep3/only spec.1
            spec: my next
        ]
    ]

    ; The bootstrap executable does not have support for true dynamic LET.
    ; We approximate it by searching the body for LET followed by SET-WORD!
    ; or WORD! and add that to locals.
    ;
    append3 new-spec <local>
    append3 new-spec collect-lets body  ; append3 splices blocks without /ONLY

    new-body: compose3 [  ; splices
        (tryers)
        (proxiers)
        (as group! body)  ; compose3 does not splice groups--just blocks
        return ~  ; functions now default to returning nothing
    ]
    return reduce [new-spec new-body]
]

func: adapt get $func3 [set [spec body] modernize-action spec body]
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

meth: enfix adapt get $lib/meth [set [spec body] modernize-action spec body]
method: func3 [] [
    fail/where "METHOD deprecated temporarily, use METH" 'return
]

mold: adapt get $lib/mold [  ; update so MOLD SPREAD works
    if all [
        block? value
        #splice! = first value
    ][
        only: true
        value: next value
    ]
]

noquote: func3 [x [~null~ any-value!]] [
    assert [not action? get $x]
    switch type of x [
        lit-word! [return to word! x]
        lit-path! [return to path! x]
    ]
    x
]

; We've scrapped the form of refinements via GROUP! for function dispatch, and
; you need to use APPLY now.  This is a poor man's compatibility APPLY
;
; https://forum.rebol.info/t/1813
;
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
    while [all [:params.1, not refinement? :params.1]] [
        args: evaluate/set args 'result
        f.(to word! :params.1): :result
        params: next params
    ]

    ; Now go by the refinements.  If it's a refinement that takes an argument,
    ; we have to set the refinement to true
    ;
    while [get-word? :args.1] [  ; new-style refinements are GET-WORD! in boot
        pos: find params to refinement! args.1 else [
            fail ["Unknown refinement" args.1]
        ]
        args: evaluate/set (next args) 'result
        any [
            not :pos.2
            refinement? pos.2  ; old refinement in params block
        ] then [  ; doesn't take an argument, set it to the logical next value
            case [
                any [
                    null? :result
                    void? :result
                    blank? :result
                    false = :result
                ][
                    f.(to word! pos.1): null
                ]
                true = :result [
                    f.(to word! pos.1): true
                ]
                fail "No-Arg Refinements in Bootstrap must be TRUE or NULL"
            ]
        ] else [  ; takes an arg, so set refinement to true and set NEXT param
            f.(to word! pos.1): true
            if blank? :result [
                f.(to word! pos.2): null
            ] else [
                f.(to word! pos.2): :result
            ]
        ]
    ]

    eval f
]

//: enfix func3 ['left [word! path!] right [block!]] [
    apply get left right
]

; For commentary purposes, e.g. old-append: runs lib.append
;
runs: func3 [action [~void~ action!]] [
    if void? action [return null]
    return action
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
            get-word? item [
                item: get item
                assert [object? item]
                bind code item
            ]
        ] else [
            assert [word? item]
            append obj spread reduce [item 0]
            obj.(item): get/any item
        ]
    ]
    bind code obj  ; simulates ability to bind to single words
    return code
]


=== "END ENCLOSURE THAT AVOIDED OVERWRITING TOP-LEVEL DECLS" ===

)  ; see earlier remarks for why this group exists, close it now
