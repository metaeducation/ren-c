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


; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; That is not pursued in Ren-C.
;
context: lambda [spec] [construct spec]


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
    encoding?: ~
    file-type?: ~
    speed?: ~
    info?: ~
    exists?: ~
]


; The legacy PRIN construct is replaced by WRITE-STDOUT SPACED and similar
;
prin: function [
    "Print without implicit line break, blocks are SPACED."

    return: [~]
    value [any-value!]
][
    write-stdout switch type of :value [
        null [return]
        text! char! [value]
        block! [spaced value]

        default [form :value]
    ]
]


; REJOIN is deprecated in Ren-C, due to its erratic semantics.
; Use UNSPACED and SPACED to make strings.
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
    while [pos: evaluate/step3 pos (the evaluated:)][
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
    return append result next values
]


; The name FOREVER likely dissuades its use, since many loops aren't intended
; to run forever.  CYCLE gives similar behavior without suggesting the
; permanence.  It also is unique among loop constructs by supporting a value
; return via STOP, since it has no "normal" loop termination condition.
;
forever: :cycle
