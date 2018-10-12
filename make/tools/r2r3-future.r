REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        This file was originally used to make an R3-Alpha "act like a Ren-C".
        That way, bootstrapping code could be written under various revised
        language conventions--while still using older executables to build.

        Changes in the language have become drastic enough that an R3-Alpha
        lacks the compositional tools (such as ADAPT, SPECIALIZE, CHAIN) to
        feasibly keep up.  Hence, the shim is only used with older Ren-C
        executables...which are both more in sync with modern definitions,
        and have those composition tools available:

        https://github.com/metaeducation/ren-c/issues/815

        It also must remain possible to run it from a state-of-the-art build
        without disrupting the environment.  This is because the script does
        not know whether the R3-MAKE you are using is old or new.  No good
        versioning strategy has been yet chosen, so words are "sniffed" for
        existing definitions to upgrade in a somewhat ad-hoc way.
    }
]

; The snapshotted Ren-C existed when VOID? was the name for NULL?.  What we
; will (falsely) assume is that any Ren-C that knows NULL? is "modern" and
; does not need patching forward.  What this really means is that we are
; only catering the shim code to the snapshot.
;
; (It would be possible to rig up shim code for pretty much any specific other
; version if push came to shove, but it would be work for no obvious reward.)
;
if true = attempt [null? :some-undefined-thing] [
    ;
    ; COPY AS TEXT! can't be made to work in the old Ren-C, so it just
    ; aliases its SPELLING-OF to COPY-AS-TEXT.  Define that for compatibilty.
    ;
    copy-as-text: chain [
        specialize 'as [type: text!]
            |
        :copy
    ]

    QUIT
]

print "== SHIMMING OLDER R3 TO MODERN LANGUAGE DEFINITIONS =="

; NOTE: The slower these routines are, the slower the overall build will be.
; It's worth optimizing it as much as is reasonable.


unset 'forall ;-- use FOR-NEXT
unset 'forskip ;-- use FOR-SKIP
unset 'foreach ;-- use FOR-EACH


; https://forum.rebol.info/t/behavior-of-to-string-as-string-mold/630
;
copy-as-text: :spelling-of


; https://forum.rebol.info/t/null-in-the-librebol-api-and-void-null/597
;
null: :void
null?: :void?
unset 'void
unset 'void?
set*: :set ;-- used to allow nulls by default
unset?: chain [:lib/set? | :lib/not]
value?: :any-value?

; http://blog.hostilefork.com/did-programming-opposite-of-not/
;
; Note that ADAPT can't be used here, because TRUE?/FALSE? did not take <opt>
;
did: func [optional [<opt> any-value!]] compose/deep [
    (:lib/true?) (:lib/to-value) :optional
]
not: func [optional [<opt> any-value!]] compose/deep [
    (:lib/false?) (:lib/to-value) :optional
]

; https://forum.rebol.info/t/if-at-first-you-dont-select-try-try-again/589
;
try: :to-value


; https://forum.rebol.info/t/squaring-the-circle-of-length-and-length-of/385
;
type-of: chain [:lib/type-of | :opt] ;-- type of null is now null
of: enfix function [
    return: [<opt> any-value!]
    'property [word!]
    value [<opt> any-value!]
][
    lib/switch*/default property [ ;-- non-evaluative, pass through null
        index [index-of :value]
        offset [offset-of :value]
        length [length-of :value]
        type [type-of :value] ;-- type of null is null
        words [words-of :value]
        head [head-of :value]
        tail [tail-of :value]
        binding [context-of :value]
    ][
        fail/where ["Unknown reflector:" property] 'property
    ]
]


; https://forum.rebol.info/t/the-benefits-of-a-falsey-null-any-major-drawbacks/675
;
; Unfortunately, new functions are needed here vs. just adaptations, because
; the spec of IF and EITHER etc. do not take <opt> :-(
;
if: func [
    return: [<opt> any-value!]
    condition [<opt> any-value!]
    branch [block! action!]
    ;-- /OPT processing would be costly, omit the refinement for now
] compose/deep [
    (:lib/if) (:lib/to-value) :condition :branch
]
either: func [
    return: [<opt> any-value!]
    condition [<opt> any-value!]
    true-branch [block! action!]
    false-branch [block! action!]
    ;-- /OPT processing would be costly, omit the refinement for now
] compose/deep [
    (:lib/either) (:lib/to-value) :condition :true-branch :false-branch
]
while: adapt 'lib/while compose/deep [
    condition: reduce [quote (:lib/to-value) as group! :condition]
]
any: function [
    return: [<opt> any-value!]
    block [block!]
] compose/deep [
    (:lib/loop-until) [
        (:lib/if) value: (:lib/to-value) do/next block 'block [
            return :value
        ]
        (:lib/tail?) block
    ]
    return null
]
all: function [
    return: [<opt> any-value!]
    block [block!]
] compose/deep [
    value: null
    (:lib/loop-until) [
        ;-- NOTE: uses the old-style UNLESS, as its faster than IF NOT
        (:lib/unless) value: (:lib/to-value) do/next block 'block [
            return null
        ]
        (:lib/tail?) block
    ]
    :value
]
find: chain [:lib/find | :opt]
select: :lib/select* ;-- old variation that returned null when not found
case: function [
    return: [<opt> any-value!]
    cases [block!]
    /all
    ;-- /OPT processing would be costly, omit the refinement for now
] compose/deep [
    result: null

    (:lib/loop-until) [
        condition: do/next cases 'cases
        lib/if lib/tail? cases [return :condition] ;-- "fallout"
        (:lib/if) (:lib/to-value) :condition [
            result: (:lib/to-value) do ensure block! cases/1

            ;-- NOTE: uses the old-style UNLESS, as its faster than IF NOT
            (:lib/unless) all [return :result]
        ]
        (:lib/tail?) cases: (:lib/next) cases
    ]
    :result
]

; !!! PARSE returns null or BAR! by default, but this shim can't tell a RETURN
; explicitly of a true/false from a plain true/false based on end of input.
;
parse: chain [
    :parse
        |
    func [return: [<opt> any-value!] x [<opt> any-value!]] [
        lib/switch/opt :x [
            #[false] []
            #[true] ['|]
            (:x)
        ]
    ]
]

choose: function [
    {Like CASE but doesn't evaluate blocks https://trello.com/c/noVnuHwz}
    choices [block!] /local result
] compose/deep [
    (:lib/loop-until) [
        (:lib/if) (:lib/to-value) do/next choices 'choices [
            return choices/1
        ]
        (:lib/tail?) choices: (:lib/next) choices
    ]
    return null
]

; Renamed, tightened, and extended with new features
;
file-to-local: :to-local-file
local-to-file: :to-rebol-file
unset 'to-local-file
unset 'to-rebol-file


; https://forum.rebol.info/t/text-vs-string/612
;
text!: (string!)
text?: (:string?)
to-text: (:to-string)


; https://forum.rebol.info/t/reverting-until-and-adding-while-not-and-until-not/594
;
; Note: WHILE-NOT can't be written correctly in usermode R3-Alpha (RETURN
; won't work definitionally.)  Assume we'll never bootstrap under R3-Alpha.
;
until: :loop-until
while-not: adapt 'while compose/deep [
    condition: reduce [
        quote (:lib/not) quote (:lib/to-value) as group! :condition
    ]
]
until-not: adapt 'until compose/deep [
    body: reduce [
        quote (:lib/not) quote (:lib/to-value) as group! :body
    ]
]


; https://trello.com/c/XnDsvsM0
;
assert [find words-of :ensure 'test]
really: func [optional [<opt> any-value!]] [
    if null? :optional [
        fail/where [
            "REALLY expects argument to be non-null"
        ] 'optional
    ]
    return :optional
]

; https://trello.com/c/Bl6Znz0T
;
deflate: specialize 'compress [gzip: false | only: true] ;; "raw"
inflate: specialize 'decompress [gzip: false | only: true] ;; "raw"
gzip: specialize 'compress [gzip: true]
gunzip: specialize 'decompress [gzip: true]
compress: decompress: does [
    fail "COMPRESS/DECOMPRESS replaced by gzip/gunzip/inflate/deflate"
]

; https://trello.com/c/9ChhSWC4/
;
switch: adapt 'switch [
    if default [fail/where ["use DEFAULT [], not /DEFAULT, in SWITCH"] 'cases]
    ;-- re-use /DEFAULT slot as variable
    cases: map-each c cases [
        lib/case [
            lit-word? :c [to word! c]
            lit-path? :c [to path! c]

            path? :c [
                fail/where ["Switch now evaluative" c] 'cases
            ]
            word? :c [
                opt either c = 'default [
                    default: true ;-- signal next BLOCK! to be GROUP!'d
                    continue
                ][
                    if all [
                        c != 'default
                        not datatype? get c
                    ][
                        fail/where ["Switch now evaluative" c] 'cases
                    ]
                    get c
                ]
            ]

            block? :c [
                either default [
                    default: false
                    as group! :c
                ][
                    :c
                ]
            ]

            true [:c]
        ]
    ]
    if default [fail "DEFAULT must be followed by BLOCK! when used in SWITCH"]
]

default: enfix function [
    return: [<opt> any-value!]
    'target [<end> set-word! set-path!]
     branch [block! action!]
     /only
][
    if unset? 'target [return do :branch] ;-- `case [... default [...]]`
    either all [
        value? set* quote gotten: get target
        only or [not blank? :gotten]
    ][
        :gotten ;; so that `x: y: default z` leads to `x = y`
    ][
        if null? branch: do :branch [
            fail ["DEFAULT for" target "came back NULL"]
        ]
        set target :branch
    ]
]

; https://forum.rebol.info/t/method-and-the-argument-against-procedure/710
;
actionmaker: lib/function [
    return: [action!]
    gather-locals [logic!]
    spec [block!]
    body [block!]
][
    generator: either find spec [return: <void>] [
        spec: replace copy spec [<void>] [] ;-- keep RETURN: as local
        body: compose [return: :leave (body)]
        either gather-locals [:lib/procedure] [:lib/proc]
    ][
        either gather-locals [:lib/function] [:lib/func]
    ]
    generator spec body
]

func: specialize 'actionmaker [gather-locals: false]
function: specialize 'actionmaker [gather-locals: true]
unset 'procedure
unset 'proc


; https://forum.rebol.info/t/method-and-the-argument-against-procedure/710
; 
; This only does the <in> part, since the older Ren-C is like R3-Alpha and
; FUNCTION will automatically get copied and have the binding derived.
;
method: enfix func [
    {FUNCTION variant that creates an ACTION! implicitly bound in a context}

    return: [action!]
    :member [set-word! set-path!]
    spec [block!]
    body [block!]
    <local> context
][
    if not context: try binding of member [
        fail [member "must be bound to an ANY-CONTEXT! to use METHOD"]
    ]
    ;-- Older Ren-C don't take OBJECT! literally with <in>
    set member (function compose [(spec) <in> context] body)
]

meth: :func ;-- suitable enough synonym in the older Ren-C


; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
for-each modifier [append insert change] [
    set modifier adapt modifier [
        if all [
            not only
            any-path? :value
            not any-path? :series
        ][
            only: true
        ]
    ]
]

also: enfix function [
    return: [<opt> any-value!]
    optional [<opt> any-value!]
    :branch [block! action!]
][
    :optional then :branch
    return :optional
]

and: enfix function [
    return: [<opt> any-value!]
    left [<opt> any-value!]
    :right [group! block!]
][
    if group? :right [
        if not :left [return false]
        return did do as block! right ;-- old r3 didn't allow DO of GROUP!
    ]
    if not :left [return null]
    return do right
]

or: enfix function [
    return: [<opt> any-value!]
    left [<opt> any-value!]
    :right [group! block!]
][
    if group? :right [
        if :left [return true]
        return did do as block! right ;-- old r3 didn't allow DO of GROUP!
    ]
    if :left [return :left]
    return do right
]

;-- make COPY obey "blank in, null out"
copy: chain [:lib/copy | :opt]

; Old MAYBE was a very early implementation of MATCH...nowhere near as good as
; it is in modern Ren-C.  It returned BLANK! on failure, so adjust it for the
; new NULL world... can be used on limited things like TRY MATCH BLOCK! FOO
;
match: chain [:lib/maybe | :opt]

; New MAYBE definition runs all right in older Ren-Cs
;
maybe: enfix func [
    return: [<opt> any-value!]
    'target [set-word! set-path!]
    optional [<opt> any-value!]
    <local> gotten
][
    case [
        set-word? target [
            if null? :optional [return get target]
            set target :optional
        ]

        set-path? target [
            if null? :optional [return do compose [(as get-path! target)]]
            do compose/only [(target) quote (:optional)]
        ]
    ]
]

; Doesn't have any magic powers in the old Ren-C, but still helpful for
; showing args that span multiple lines.
;
set quote <- func [x [<end> any-value!]] [:x]

print: func [
    return: [<opt> blank!]
    line [blank! text! block!]
][
    write-stdout switch type of line [
        blank! [return null]
        text! [line]
        block! [spaced line]
    ]
    write-stdout newline
    _ ;-- would be a VOID! in modern Ren-C
]

print-newline: specialize 'write-stdout [value: newline]
