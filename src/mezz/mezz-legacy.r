Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Legacy compatibility"
    homepage: https://trello.com/b/l385BE7a/porting-guide
    rights: --[
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    description: --[
        These are a few compatibility scraps left over from extracting the
        R3-Alpha emulation layer into %redbol.reb.
    ]--
]

binary!: blob!

loop: ~<Short word LOOP is reserved for a generalized looping dialect:
        https://forum.rebol.info/t/common-lisp-loop-and-iterate/1878>~


REBOL: ~<The Rebol [] header of a script must be interpreted by LOAD (and
       functions like DO).  It cannot be executed directly.>~

input: ~<Use ASK TEXT! or READ-LINE vs INPUT (consider using ASK dialect):
       https://forum.rebol.info/t/1124>~

repend: ~<REPEND is just (adapt append/ [value: reduce :value]), and is not
        provided in the box.>~

remold: ~<REMOLD is just (adapt mold/ [value: reduce :value]), but is not
        provided in the box.>~

rejoin: ~<REJOIN is replaced in textual sceanarios by UNSPACED, but in more
        general cases by JOIN, which accepts datatypes as a first parameter,
        e.g. (join blob! [-[ABC]- 1 + 2 3 + 4])
        https://forum.rebol.info/t/248>~


; CONSTRUCT is a "verb-ish" word slated to replace the "noun-ish" CONTEXT:
;
; http://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
;
; Note: Historically OBJECT was essentially a synonym for CONTEXT with the
; ability to tolerate a spec of `[a:]` by transforming it to `[a: none].
; Ren-C hasn't decided yet, but will likely support `construct [a: b: c:]`
;
/context: specialize make/ [type: object!]


; !!! These cases still linger as question mark routines that don't return
; LOGIC!, and they seem like they need greater rethinking in general.  What
; replaces them (for ones that are kept) might be new.
;
comment [
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

    return: []
    value [~null~ element?]
][
    write-stdout switch:type value [
        null?/ [return ~]  ; type of VOID is currently null
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
/forever: cycle/


/find: adapt (augment find/ [:reverse :last]) [
    if reverse or last [
        panic:blame [
            ":REVERSE and :LAST on FIND have been deprecated.  Use FIND-LAST"
            "or FIND-REVERSE specializations: https://forum.rebol.info/t/1126"
        ] $reverse
    ]
]
