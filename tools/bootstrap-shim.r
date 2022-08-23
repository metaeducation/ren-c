REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Type: module
    Name: Bootstrap-Shim
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2021 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Ren-C "officially" supports two executables for doing a bootstrap
        build.  One is a frozen "stable" version (`8994d23`) which was
        committed circa Dec-2018:

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
        * Version 8994d23 does not allow WORD!-access of NULL, because the null
        state was conflated with the "unset" state at that time.  This is
        not something that can be overridden by a shim.  So the easiest way
        to do `null? var` in a forward-compatible way is `null? :var`, but
        if the intent is on a variable that would be considered actually
        "unset" in modern versions then use `unset?` (it will be null though)

        * There was an issue with the Linux bootstrap executable not working
        on GitHub Actions, due to some strange issue with the binary and the
        container.  Rebuilding with a newer version of GCC seemed to resolve
        that, but Linux executables compiled circa 2018 in the wild may not
        work on some modern systems.
    }
]

; The snapshotted Ren-C existed before <try> was legal to mark on arguments.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<try> integer!]] [...]  ; modern interpreter or already shimmed
    if in (pick system 'options) 'redbol-paths [
        system.options.redbol-paths: true
    ]
] then [
    ; Fall through to the body of this file, we are shimming version ~8994d23
] else [
    trap [
        lib/func [i [<try> integer!]] [...]
    ] then [
        ;
        ; Old bootstrap executables that are already shimmed should not do
        ; tweaks for the modern import.  Otherwise, export load-all: would
        ; overwrite with LOAD instead of LOAD/ALL (for example).  It's just
        ; generally inefficient to shim multiple times.
        ;
        quit
    ]

    === {TWEAKS SO MODERN REN-C DOESN'T ACT TOO MODERN} ===

    export parse: func [] [
        fail/where "Use PARSE2 in Bootstrap Process, not UPARSE/PARSE" 'return
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

set '~ :null  ; most similar behavior to bad-word isotope available
none: :void  ; again, most similar thing

; Done is used as a signal in the boot files that the expected end is reached.
; This is a BAD-WORD! in modern Ren-C, but a plain word in the bootstrap EXE.
; Must use SET because even though we don't run this in modern Ren-C, the file
; gets scanned...and `~done~:` would be invalid.
;
set '~done~ :null

repeat: :loop

compose: lib/func [block [block!] /deep <local> result pos product count] [
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
            pos: lib/change/part pos second product 1
        ] else [
            if null? :product [
                ;
                ; Permit NULL but only in bootstrap build (as it has no pure
                ; void and null is the product of non-branch conditionals).
                ; Trust current build to catch errors.
                ;
                lib/change/part pos null 1
            ] else [
                lib/change/only pos :product
                pos: next pos
            ]
        ]
    ]
    return result
]

; Modern DO is more limited in giving back "void intent" so it doesn't go
; well in situations like `map-each code blocks-of-code [do code]`...because
; situations that would have returned NULL and opted out don't opt out.
; You are supposed to use EVAL for that.
;
reeval: :eval
eval: :do

; We don't have the distinctions between NULL and "unsets" in the bootstrap
; build.  But make them distinct at the source level.

null: enfix lib/func [:left [<skip> set-word!]] [
    if :left [lib/unset left]
    lib/null
]

did: func [return: [logic!] optional [<opt> any-value!]] [
    any [
        blank? :optional
        logic? :optional
    ] then [
        fail/where [
            "DID semantics changing, only tests for NULL, fixup"
        ] 'optional
    ]
    not null? :optional
]

didn't: func [return: [logic!] optional [<opt> any-value!]] [
    null? :optional
]

to-logic: func [return: [logic!] optional [<opt> any-value!]] [
    if null? :optional [return false]
    to logic! :optional
]

opt: func [v [<opt> any-value!]] [
    if null? :v [fail/where "OPT on NULL" 'v]
    if blank? :v [return null]
    :v
]

; The safety aspect of new TRY isn't really worth attempting to emulate in the
; bootstrap shim.  So the <try> parameters are simply "null in, null out" with
; no requirement that a TRY be on the callsite.
;
; However, NULL word/path fetches cause errors in the bootstrap shim.  While
; we can avoid the use of NULL and just use BLANK! for various opt-out
; purposes in bootstrap, some variables are best initialized to null... so
; we adapt TRY so it's variadic and works around fetch errors for WORD!/PATH!.
;
; This results in putting TRY in places that are superfluous for the current
; build on WORD! and PATH!, and then the current build needs TRY on the results
; of <try> functions that the bootstrap build does not.  So it's a kind of
; "try inflation", but since TRY is a no-op on null variable fetches in the
; current build this seems a reasonable enough mitigation strategy for now.
;
try: lib/func [
    :look [<...> any-value!]  ; <...> old variadic notation
    args [<...> <opt> any-value!]  ; <...> old variadic notation
][
    all [
        match [path! word!] first look
        not match action! get first look
    ] then [
        return get take look
    ]
    return take* args
]

null?: lib/func [
    :look [<...> any-value!]  ; <...> old variadic notation
    args [<...> <opt> any-value!]  ; <...> old variadic notation
][
    all [
        match [path! word!] first look
        not match action! get first look
    ] then [
        return lib/null? get take look
    ]
    return lib/null? take* args
]

; We don't have isotopes in the bootstrap build.  But if a branch produces
; NULL it will yield a "VOID!" (kind of like a BAD-WORD! of ~void~)  Turn these
; into NULL, and trust that the current build will catch cases of something
; like a PRINT being turned into a NULL.
;
decay: func [v [<opt> any-value!]] [
    if void? :v [return null]
    :v
]


; !!! This isn't perfect, but it should work for the cases in rebmake
;
load-value: :load
load-all: :load/all

maybe: :try  ; for use in compose, to new semantics... leave NULL alone

the: :quote  ; Renamed due to the QUOTED! datatype
quote: lib/func [x [<opt> any-value!]] [
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

spread: lib/func [x [<opt> block!]] [
    if :x [reduce [#splice! x]]
]

matches: lib/func [x [<opt> datatype! typeset! block!]] [
    if :x [if block? x [make typeset! x] else [x]]
]

append: lib/func [series value [<opt> any-value!] /line <local> only] [
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
        return lib/append series second value
    ]

    only: 'only
    case [
        null? :value []
        void? :value [fail/where "APPEND of VOID! disallowed" 'value]
        blank? :value [fail/where "APPEND blanks with [_] only" 'value]
        block? :value [
            if lib/find value void! [
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
    lib/append/(only)/(line) series :value
]

insert: lib/func [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        null? :value []
        void? :value [fail/where "INSERT of VOID! disallowed" 'value]
        blank? :value [fail/where "INSERT blanks with [_] only" 'value]
        block? :value [
            if lib/find value void! [
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
    lib/insert/(only)/(line) series :value
]

change: lib/func [series value [<opt> any-value!] /line <local> only] [
    only: 'only
    case [
        null? :value []
        void? :value [fail/where "CHANGE of VOID! disallowed" 'value]
        blank? :value [fail/where "CHANGE blanks with [_] only" 'value]
        block? :value [
            if lib/find value void! [
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
    lib/change/(only)/(line) series :value
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
do compose [(to set-word! first [->]) enfix :lambda]
unset first [=>]

; SET was changed to accept BAD-WORD! isotopes
;
set: specialize :lib/set [opt: true]

; PRINT was changed to tolerate NEWLINE to mean print a newline only.
;
; !!! Also, PRINT of large blobs of data is buggy.  Print line by line of
; anything that has newlines in it.
;
print: lib/func [value <local> pos] [
    if value = newline [
        lib/print ""
        return
    ]
    value: unspaced value  ; uses bootstrap shim unspaced
    while [pos: find value newline] [
        line: copy/part value pos
        lib/print line
        value: next pos
    ]
    lib/print value
]

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
parse2: func [series rules] [
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


; COLLECT in the bootstrap version would return NULL on no keeps.  But beyond
; wanting to change that, we also want KEEP to be based on the new rules and
; not have /ONLY.  So redo it here in the shim.
;
collect*: lib/func [  ; variant giving NULL if no actual material kept
    return: [<opt> block!]
    body [block!]
    <local> out keeper
][
    keeper: specialize (  ; SPECIALIZE to remove series argument
        enclose 'append lib/func [f [frame!] <with> out] [  ; gets /LINE, /DUP
            if null? :f/value [return null]  ; doesn't "count" as collected

            f/series: out: default [make block! 16]  ; won't return null now
            :f/value  ; ELIDE leaves as result (F/VALUE invalid after DO F)
            elide do f
        ]
    )[
        series: <replaced>
    ]

    lib/eval lib/func [keep [action!] <with> return] body :keeper

    :out
]

collect: chain [  ; Gives empty block instead of null if no keeps
    :collect*  ; note: does not support , in bootstrap build
    specialize 'else [branch: [copy []]]
]


collect-lets: lib/func [
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
            value? match [block! group!] item/1 [
                lib/append lets collect-lets item/1
            ]
        ]
    ]
    return lets
]


let: lib/func [
    return: []  ; [] was old-style invisibility
    :look [any-value! <...>]  ; old-style variadic
][
    if word? first look [take look]  ; otherwise leave SET-WORD! to runs
]


modernize-action: lib/function [
    "Account for <try> annotation, refinements as own arguments"
    return: [block!]
    spec [block!]
    body [block!]
][
    last-refine-word: _

    tryers: copy []
    proxiers: copy []

    spec: lib/collect [  ; Note: offers KEEP/ONLY
        keep []  ; so bootstrap COLLECT won't be NULL if no KEEPs

        while [not tail? spec] [
            if tag? spec/1 [
                last-refine-word: _
                keep/only spec/1
                spec: my next
                continue
            ]

            if refinement? spec/1 [  ; REFINEMENT! is a word in this r3
                last-refine-word: as word! spec/1
                keep/only spec/1

                ; Feed through any TEXT!s following the PATH!
                ;
                while [
                    if (tail? spec: my next) [break]
                    text? spec/1
                ][
                    keep/only spec/1
                ]

                ; If there's a block specifying argument types, we need to
                ; have a fake proxying parameter.

                if not block? spec/1 [
                    lib/append proxiers compose [  ; turn blank to null
                        (as set-word! last-refine-word)
                            lib/opt (as get-word! last-refine-word)
                    ]
                    continue
                ]

                proxy: as word! unspaced [last-refine-word "-arg"]
                keep/only proxy
                keep/only spec/1

                lib/append proxiers compose [
                    (as set-word! last-refine-word) (as get-word! proxy)
                    set (as lit-word! proxy) void
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args/locals)
            ;
            if w: match any-word! spec/1 [
                ;
                ; Transform the escapable argument convention, to line up
                ; GET-WORD! with things that are escaped by GET-WORD!s
                ; https://forum.rebol.info/t/1433
                ;
                keep case [
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
                    keep/only spec/1
                ]

                if spec/1 = <none> [  ; new semantics: <none> -> ~none~
                    keep/only <void>  ; old cue for returning garbage
                    spec: my next
                    continue
                ]

                if spec/1 = <void> [
                    keep/only []  ; old cue for invisibility
                    spec: my next
                    continue
                ]

                ; Substitute <opt> for any <try> found, and save some code
                ; to inject for that parameter to return null if it's null
                ;
                if lib/find (lib/try match block! spec/1) <try> [
                    keep/only replace copy spec/1 <try> <opt>
                    lib/append tryers compose [
                        if null? (as get-word! w) [return null]
                    ]
                    spec: my next
                    continue
                ]

                if lib/find (lib/try match block! spec/1) <variadic> [
                    keep/only replace copy spec/1 <variadic> <...>
                    spec: my next
                    continue
                ]
            ]

            if refinement? spec/1 [
                continue
            ]

            keep/only spec/1
            spec: my next
        ]
    ]

    ; The bootstrap executable does not have support for true dynamic LET.
    ; We approximate it by searching the body for LET followed by SET-WORD!
    ; or WORD! and add that to locals.
    ;
    lib/append spec <local>
    lib/append spec collect-lets body

    body: compose [  ; new COMPOSE, with SPREAD
        (spread tryers)
        (spread proxiers)
        (as group! body)
    ]
    return reduce [spec body]
]

func: adapt :lib/func [set [spec body] modernize-action spec body]
function: adapt :lib/function [set [spec body] modernize-action spec body]

meth: enfixed adapt :lib/meth [set [spec body] modernize-action spec body]
method: lib/func [] [
    fail/where "METHOD deprecated temporarily, use METH" 'return
]

trim: adapt :trim [  ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [(not tail? series) and (series/1 = LF)] [
            take series
        ]
    ]
]

mutable: lib/func [x [any-value!]] [
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
join: lib/func [base value [<opt> any-value!]] [
    append copy base :value  ; shim APPEND, that offers SPLICE behavior
]

; https://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
has: ~

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
; Only supports strings in bootstrap, because sea of words is not in bootstrap
; executable, so plain words here creates a bunch of variables...could confuse
; the global state more than it already is.
;
===: lib/func [
    ; note: <...> is now a TUPLE!, and : used to be "hard quote" (vs ')
    label [text!]
    'terminal [word!]
][
    assert [equal? terminal '===]
]

const?: lib/func [x] [return false]

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
lib/read: read: enclose :lib-read lib/function [f [frame!]] [
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

transcode: lib/function [
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

split: lib/function [
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
delimit: lib/func [
    return: [<opt> text!]
    delimiter [<opt> blank! char! text!]
    line [blank! text! block!]
    <local> text value pending anything
][
    if blank? line [return null]
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


noquote: lib/func [x [<opt> any-value!]] [
    switch type of :x [
        lit-word! [to word! x]
        lit-path! [to path! x]
    ] else [:x]
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
zip: enclose :zip lib/function [f] [
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

    lib/match: lib/func [type value [<opt> any-value!] <local> answer] [
        if bad-word? set* 'answer old-match type value [
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
apply: lib/function [
    action [action!]
    args [block!]
][
    f: make frame! :action
    params: words of :action

    ; Get all the normal parameters applied
    ;
    result: null
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
            f/(to word! :pos/1): :result
        ] else [  ; takes an arg, so set refinement to true and set NEXT param
            f/(to word! :pos/1): true
            f/(to word! :pos/2): :result
        ]
    ]

    do f
]

local-to-file-old: :lib/local-to-file
local-to-file: lib/local-to-file: lib/func [path [<opt> text! file!] /pass /dir] [
    path: default [_]
    local-to-file-old/(pass)/(dir) path
]

file-to-local-old: :lib/file-to-local
file-to-local: lib/file-to-local: lib/func [
    path [<opt> text! file!] /pass /full /no-tail-slash /wild
][
    path: default [_]
    file-to-local-old/(pass)/(full)/(no-tail-slash)/(wild) path
]

select: lib/func [
    series [<opt> any-series! any-context! map!]
    value [any-value!]
][
    if null? :series [return null]
    lib/select :series :value
]
