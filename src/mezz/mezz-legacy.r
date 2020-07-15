REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Legacy compatibility"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        These are a few compatibility scraps left over from extracting the
        R3-Alpha emulation layer into %redbol.reb.
    }
]


; CONSTRUCT (arity 2) and HAS (arity 1) have arisen as the OBJECT!-making
; routines, parallel to FUNCTION (arity 2) and DOES (arity 1).  By not being
; nouns like CONTEXT and OBJECT, they free up those words for other usages.
; For legacy support, both CONTEXT and OBJECT are just defined to be HAS.
;
; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; The tolerance of ending with a set-word has been added to CONSTRUCT+HAS
; so this distinction is no longer required.
;
context: object: :has


; To be more visually pleasing, properties like LENGTH can be extracted using
; a reflector as simply `length of series`, with no hyphenation.  This is
; because OF quotes the word on the left, and passes it to REFLECT.
;
; There are bootstrap reasons to keep versions like WORDS-OF alive.  Though
; WORDS OF syntax could be faked in R3-Alpha (by making WORDS a function that
; quotes the OF and throws it away, then runs the reflector on the second
; argument), that faking would preclude naming variables "words".
;
; Beyond the bootstrap, there could be other reasons to have hyphenated
; versions.  It could be that performance-critical code would want faster
; processing (a TYPE-OF specialization is slightly faster than TYPE OF, and
; a TYPE-OF native written specifically for the purpose would be even faster).
;
; Also, HELP isn't designed to "see into" reflectors, to get a list of them
; or what they do.  (This problem parallels others like not being able to
; type HELP PARSE and get documentation of the parse dialect...there's no
; link between HELP OF and all the things you could ask about.)  There's also
; no information about specific return types, which could be given here
; with REDESCRIBE.
;
length-of: specialize 'reflect [property: 'length]
words-of: specialize 'reflect [property: 'words]
values-of: specialize 'reflect [property: 'values]
index-of: specialize 'reflect [property: 'index]
type-of: specialize 'reflect [property: 'type]
binding-of: specialize 'reflect [property: 'binding]
head-of: specialize 'reflect [property: 'head]
tail-of: specialize 'reflect [property: 'tail]
file-of: specialize 'reflect [property: 'file]
line-of: specialize 'reflect [property: 'line]
body-of: specialize 'reflect [property: 'body]


; General renamings away from non-LOGIC!-ending-in-?-functions
; https://trello.com/c/DVXmdtIb
;
index?: specialize 'reflect [property: 'index]
offset?: :offset-of
sign?: :sign-of
suffix?: :suffix-of
length?: :length-of
head: :head-of
tail: :tail-of

comment [
    ; !!! Less common cases still linger as question mark routines that
    ; don't return LOGIC!, and they seem like they need greater rethinking in
    ; general. What replaces them (for ones that are kept) might be new.
    ;
    encoding?: _
    file-type?: _
    speed?: _
    info?: _
    exists?: _
]


; The legacy PRIN construct is replaced by WRITE-STDOUT SPACED and similar
;
prin: function [
    "Print without implicit line break, blocks are SPACED."

    return: <void>
    value [<opt> any-value!]
][
    write-stdout switch type of :value [
        null [return]
        text! char! [value]
        block! [spaced value]

        default [form :value]
    ]
]


; REJOIN in R3-Alpha meant "reduce and join"; the idea of JOIN in Ren-C
; already implies reduction of the appended data.  JOIN-ALL is a friendlier
; name, suggesting the join result is the type of the first reduced element.
;
; But JOIN-ALL doesn't act exactly the same as REJOIN--in fact, most cases
; of REJOIN should be replaced not with JOIN-ALL, but with UNSPACED.  Note
; that although UNSPACED always returns a STRING!, the AS operator allows
; aliasing to other string types (`as tag! unspaced [...]` will not create a
; copy of the series data the way TO TAG! would).
;
rejoin: function [
    "Reduces and joins a block of values."
    return: [any-series!]
        "Will be the type of the first non-null series produced by evaluation"
    block [block!]
        "Values to reduce and join together"
][
    ; An empty block should result in an empty block.
    ;
    if empty? block [return copy []]

    ; Act like REDUCE of expression, but where null does not cause an error.
    ;
    values: copy []
    pos: block
    while [pos: evaluate/set pos (quote evaluated:)][
        append/only values :evaluated
    ]

    ; An empty block of values should result in an empty string.
    ;
    if empty? values [return copy {}]

    ; Take type of the first element for the result, or default to string.
    ;
    result: if any-series? first values [
        copy first values
    ] else [
        form first values
    ]
    append result next values
]

; In Ren-C, MAKE for OBJECT! does not use the "type" slot for parent
; objects.  You have to use the arity-2 CONSTRUCT to get that behavior.
; Also, MAKE OBJECT! does not do evaluation--it is a raw creation,
; and requires a format of a spec block and a body block.
;
; Because of the commonality of the alternate interpretation of MAKE, this
; bridges until further notice.
;
make: enclose 'lib/make func [f] [
    all [
        :f/type = object!
        block? :f/def
        not block? first f/def
    ] then [
        return has f/def
    ]
    if object? :f/type [
        return construct :f/type :f/def
    ]
    do f
]


; For the moment, Ren-C has taken APPLY for a function that names parameters
; and refinements directly in a block of code.  APPLIQUE acts like R3-Alpha's
; APPLY, demonstrating that such a construct could be written in userspace--
; even implementing the /ONLY refinement:
;
; `APPEND/ONLY/DUP A B 2` => `applique :append [a b none none true true 2]`
;
; This is hoped to be a "design lab" for figuring out what a better apply
; might look like.
;
applique: function [
    {APPLY interface is still evolving, see https://trello.com/c/P2HCcu0V}
    return: [<opt> any-value!]
    action [action!]
    block [block!]
    /only
][
    frame: make frame! :action
    params: words of :action
    using-args: true

    while [block: sync-invisibles block] [
        block: if only [
            arg: block/1
            try next block
        ] else [
            try evaluate/set block quote arg:
        ]

        if refinement? params/1 [
            using-args: did set (in frame params/1) :arg
        ] else [
            if using-args [
                set (in frame params/1) :arg
            ]
        ]

        params: try next params
    ]

    comment [
        ;
        ; Too many arguments was not a problem for R3-alpha's APPLY, it would
        ; evaluate them all even if not used by the function.  It may or
        ; may not be better to have it be an error.
        ;
        if not tail? block [
            fail "Too many arguments passed in R3-ALPHA-APPLY block."
        ]
    ]

    do frame ;-- nulls are optionals
]


; The name FOREVER likely dissuades its use, since many loops aren't intended
; to run forever.  CYCLE gives similar behavior without suggesting the
; permanence.  It also is unique among loop constructs by supporting a value
; return via STOP, since it has no "normal" loop termination condition.
;
forever: :cycle
