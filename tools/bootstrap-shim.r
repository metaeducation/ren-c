REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Ren-C Open Source Contributors
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
]

; The snapshotted Ren-C existed right before <blank> was legal to mark an
; argument as meaning a function returns null if that argument is blank.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<blank> integer!]] [...]
] else [
    ; OPT has behavior of turning NULLs into VOID! to keep you from optioning
    ; something you don't need to, but with refinement changes bootstrap code
    ; would get ugly if it had to turn every OPT of a refinement into OPT TRY.
    ; Bypass the voidification for the refinement sake.
    ;
    opt: func [v [<opt> any-value!]] [
        if blank? :v [return null]
        return :v
    ]

    QUIT
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

; SET was changed to accept VOID!, use SET VAR NON VOID! (...EXPRESSION...)
; if that is what was intended.
;
set: specialize 'lib/set [opt: true]

; PRINT was changed to tolerate NEWLINE to mean print a newline only
;
print: func [value] [
    lib/print either value == newline [""][value]
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

; NULL was used for cases that were non-set variables that were unmentioned,
; which could be also thought of as typos.  This was okay because NULL access
; would cause errors through word or path access.  As NULL became more
; normalized, the idea of an "unset" variable (no value) was complemented with
; "undefined" variables (set to a VOID! value).  Older Ren-C conflated these.
;
defined?: :set?
undefined?: :unset?

; COLLECT was changed back to default to returning an empty block on no
; collect, but it is built on a null collect lower-level primitive COLLECT*
;
collect*: :collect
collect: :collect-block

modernize-action: function [
    "Account for <blank> annotation, refinements as own arguments"
    return: [block!]
    spec [block!]
    body [block!]
][
    last-refine-word: _

    blankers: copy []
    proxiers: copy []

    spec: collect [
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
                while [if (tail? spec: my next) [break] | text? spec/1] [
                    keep/only spec/1
                ]

                ; If there's a block specifying argument types, we need to
                ; have a fake proxying parameter.

                if not block? spec/1 [
                    continue
                ]

                proxy: as word! unspaced [last-refine-word "-arg"]
                keep/only proxy
                keep/only spec/1

                append proxiers compose [
                    (as set-word! last-refine-word) try (as get-word! proxy)
                    set (as lit-word! proxy) void
                ]
                spec: my next
                continue
            ]

            ; Find ANY-WORD!s (args/locals)
            ;
            if keep w: match any-word! spec/1 [
                if last-refine-word [
                    fail [
                        "Refinements now *are* the arguments:" mold head spec
                    ]
                ]

                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [if (tail? spec: my next) [break] | text? spec/1] [
                    keep/only spec/1
                ]

                ; Substitute BLANK! for any <blank> found, and save some code
                ; to inject for that parameter to return null if it's blank
                ;
                if find (try match block! spec/1) <blank> [
                    keep/only replace copy spec/1 <blank> 'blank!
                    append blankers compose [
                        if blank? (as get-word! w) [return null]
                    ]
                    spec: my next
                    continue
                ]

                if find (try match block! spec/1) <variadic> [
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
    body: compose [
        ((blankers))
        ((proxiers))
        (as group! body)
    ]
    return reduce [spec body]
]

func: adapt 'func [set [spec body] modernize-action spec body]
function: adapt 'function [set [spec body] modernize-action spec body]

meth: enfixed adapt 'meth [set [spec body] modernize-action spec body]
method: enfixed adapt 'method [set [spec body] modernize-action spec body]

trim: adapt 'trim [  ; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [(not tail? series) and [series/1 = LF]] [
            take series
        ]
    ]
]

mutable: func [x [any-value!]] [
    ;
    ; Some cases which did not notice immutability in the bootstrap build
    ; now do, e.g. MAKE OBJECT! on a block that you LOAD.  This is a no-op
    ; in the older build, but should run MUTABLE in the new build when it
    ; emerges as being needed.
    ;
    :x
]

lit: :quote  ; Renamed due to the QUOTED! datatype
quote: func [x [<opt> any-value!]] [
    switch type of x [
        null [lit ()]
        word! [to lit-word! x]
        path! [to lit-path! x]

        fail "QUOTE can only work on WORD!, PATH!, NULL in old Rebols"
    ]
]

join: :join-of
join-of: func [] [
    fail 'return [  ; bootstrap EXE does not support @word
        "JOIN has returned to Rebol2 semantics, JOIN-OF is no longer needed"
        https://forum.rebol.info/t/its-time-to-join-together/1030
    ]
]

; https://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
has: null

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [:remarks [any-value! <...>]] [  ; note: <...> is now a TUPLE!
    until [
        equal? '=== take remarks
    ]
]

const?: func [x] [return false]

call*: :call
call: specialize 'call* [wait: true]

; Due to various weaknesses in the historical Rebol APPLY, a frame-based
; method retook the name.  A usermode emulation of the old APPLY was written
; under the quirky name "APPLIQUE" that nobody used, but that provided a good
; way to keep running tests of the usermode construct to make sure that a
; FRAME!-based custom apply operation worked.
;
; But the quirks in apply with refinements were solved, meaning a plain
; positional APPLY retakes the term.  The usermode APPLIQUE should work the
; same as long as you aren't invoking refinements.

redbol-apply: :applique
applique: :apply
apply: :redbol-apply

find-reverse: specialize 'find [
    reverse: true

    ; !!! Specialize out /SKIP because it was not compatible--R3-Alpha
    ; and Red both say `find/skip tail "abcd" "bc" -1` is none.
    ;
    skip: false
]

find-last: specialize 'find [
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
lib/read: read: enclose 'lib-read function [f [frame!]] [
    saved-source: :f/source
    if e: trap [bin: do f] [
        parse e/message [
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

transcode: function [
    return: [<opt> any-value!]
    source [text! binary!]
    /next [word!]
    ; /relax not supported in shim... it could be
][
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

reeval: :eval
eval: func [] [
    fail 'return [
        "EVAL is now REEVAL:"
        https://forum.rebol.info/t/eval-evaluate-and-reeval-reevaluate/1173
    ]
]

split: function [
    {Split series in pieces: fixed/variable size, fixed number, or delimited}

    return: [block!]
    series "The series to split"
        [any-series!]
    dlm "Split size, delimiter(s) (if all integer block), or block rule(s)"
        [block! integer! char! bitset! text! tag! word! bar!]
    /into "If dlm is integer, split in n pieces (vs. pieces of length n)"
][
    if all [any-string? series tag? dlm] [dlm: form dlm]
    if any [word? dlm bar? dlm tag? dlm] [
        return collect [parse series [
            [
                some [
                    copy t: [to dlm | to end]
                    (keep/only t)
                    opt thru dlm
                ]
                end
            ]
        ]]
    ]

    apply :lib/split [series: series dlm: dlm into: into]
]

; Unfortunately, bootstrap delimit treated "" as not wanting a delimiter.
; Also it didn't have the "literal BLANK!s are space characters" behavior.
;
delimit: func [
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
            append text space
            line: next line
            anything: true
            pending: false
            continue
        ]
        line: evaluate/set line 'value
        any [unset? 'value | blank? value] then [continue]
        any [char? value | issue? value] then [
            append text form value
            anything: true
            pending: false
            continue
        ]
        if pending [
            if delimiter [append text delimiter]
            pending: false
        ]
        append text form value
        anything: true
        pending: true
    ]
    if not anything [
        assert [text = ""]
        return null
    ]
    text
]

unspaced: specialize 'delimit [delimiter: _]
spaced: specialize 'delimit [delimiter: space]
