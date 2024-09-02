REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Legacy compatibility"
    Homepage: https://trello.com/b/l385BE7a/porting-guide
    Rights: {
        Copyright 2012-2018 Ren-C Open Source Contributors
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

loop: func [] [
    fail 'return [
        "Short word LOOP is reserved for a generalized looping dialect:"
        https://forum.rebol.info/t/common-lisp-loop-and-iterate/1878
    ]
]


; See notes on the future where FUNC and FUNCTION are synonyms (same will be
; true of METH and METHOD:
;
; https://forum.rebol.info/t/rethinking-auto-gathered-set-word-locals/1150
;
function: method: func [/dummy] [
    fail 'dummy [
        {The distinction between FUNC vs. FUNCTION, and METH vs. METHOD was}
        {the gathering of SET-WORD! as locals.  This behavior led to many}
        {problems with gathering irrelevant locals in the frame (e.g. any}
        {object fields for MAKE OBJECT! [KEY: ...]), and also made it hard}
        {to abstract functions.  With virtual binding, there is now LET...}
        {which has some runtime cost but is much more versatile.  If you}
        {don't want to pay the cost then use <local> in the spec.}
    ]
]


REBOL: func [] [
    fail 'return [
        "The REBOL [] header of a script must be interpreted by LOAD (and"
        "functions like DO).  It cannot be executed directly."
    ]
]


input: does [
    fail 'return [
        "Use ASK TEXT! or READ-LINE vs INPUT (consider using ASK dialect):"
        https://forum.rebol.info/t/1124
    ]
]


repend: func [<local> dummy] [
    fail 'dummy [
        "REPEND is just `adapt get $append [value: reduce :value]`, and is not"
        "provided in the box."
    ]
]

remold: func [<local> dummy] [
    fail 'dummy [
        "REMOLD is just `adapt get $mold [value: reduce :value]`, but is not"
        "provided in the box."
    ]
]

rejoin: func [<local> dummy] [
    fail 'dummy [
        "REJOIN is replaced in textual sceanarios by UNSPACED, but in more"
        "general cases by JOIN, which accepts datatypes as a first parameter,"
        "e.g. `join binary! spread [{ABC} 1 + 2 3 + 4]`"
        https://forum.rebol.info/t/rejoin-ugliness-and-the-usefulness-of-tests/
    ]
]


; CONSTRUCT is a "verb-ish" word slated to replace the "noun-ish" CONTEXT:
;
; http://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
;
; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; Ren-C hasn't decided yet, but will likely support `construct [a: b: c:]`
;
context: specialize get $make [type: object!]


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
length-of: specialize get $reflect [property: 'length]
words-of: specialize get $reflect [property: 'words]
values-of: specialize get $reflect [property: 'values]
index-of: specialize get $reflect [property: 'index]
type-of: specialize get $reflect [property: 'type]
binding-of: specialize get $reflect [property: 'binding]
head-of: specialize get $reflect [property: 'head]
tail-of: specialize get $reflect [property: 'tail]
file-of: specialize get $reflect [property: 'file]
line-of: specialize get $reflect [property: 'line]
body-of: specialize get $reflect [property: 'body]


; General renamings away from non-LOGIC!-ending-in-?-functions
; https://trello.com/c/DVXmdtIb
;
index?: specialize get $reflect [property: 'index]
offset?: runs get $offset-of
sign?: runs get $sign-of
suffix?: runs get $suffix-of
length?: runs get $length-of
head: runs get $head-of
tail: runs get $tail-of

comment [
    ; !!! Less common cases still linger as question mark routines that
    ; don't return LOGIC!, and they seem like they need greater rethinking in
    ; general. What replaces them (for ones that are kept) might be new.
    ;
    encoding?: null
    file-type?: null
    speed?: null
    info?: null
    exists?: null
]


; The legacy PRIN construct is replaced by WRITE-STDOUT SPACED and similar
;
prin: func [
    "Print without implicit line break, blocks are SPACED."

    return: [~]
    value [~null~ element?]
][
    write-stdout switch/type value [
        null?! [return ~]  ; type of VOID is currently null
        text! issue! [value]
        block! [spaced value]
    ] else [
        form value
    ]
]


; The name FOREVER likely dissuades its use, since many loops aren't intended
; to run forever.  CYCLE gives similar behavior without suggesting the
; permanence.  It also is unique among loop constructs by supporting a value
; return via STOP, since it has no "normal" loop termination condition.
;
forever: runs get $cycle


find: adapt (augment :find [/reverse /last]) [
    if reverse or last [
        fail 'reverse [
            {/REVERSE and /LAST on FIND have been deprecated.  Use FIND-LAST}
            {or FIND-REVERSE specializations: https://forum.rebol.info/t/1126}
        ]
    ]
]
