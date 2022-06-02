; bad-word.test.reb

(not bad-word? 1)

; Labeled BAD-WORD!s can be literal, or created from WORD!
(
    v: make bad-word! 'labeled
    did all [
        bad-word? v
        '~labeled~ = v
        'labeled = label of v
    ]
)

; BAD-WORD!s were initially neither true nor false, but this came to be the
; role of the isotopic forms.  Having BAD-WORD! be truthy along with QUOTED!
; means that NULL is the only falsey result from a ^META operation, which is
; useful for writing stylized code (see %for-both.test.reb)
;
(did first [~void~])

; Plain ~ is a BAD-WORD!, but its spelling is nullptr.  It thus cannot be made
; into a WORD!.
[
    (bad-word? first [~])
    (null = label of '~)
    ; !!! Add tests here when WORD!/BAD-WORD! conversions via AS and TO allowed
]

(
    valid: ["~abc~" "~a|b~"]
    for-each str valid [
        word: uparse str [to-word/ between '~ '~]
        bad: load-value str
        assert [bad-word? bad]
        assert [word = label of bad]
        isotope: do str
        assert [bad = ^isotope]
    ]
    true
)

(
    invalid: ["~~" "~~~" "~a" "~~~a"]
    for-each str invalid [
        e: trap [
            load-value str
        ]
        assert [e.id = 'scan-invalid]
    ]
    true
)


; Functions are able to return VOID as "invisible".  But to avoid wantonly
; creating variant arity situations in code, generic function execution tools
; like DO or APPLIQUE return ~none~ isotopes when given empty blocks...unless
; they are in a ^META context.
; https://forum.rebol.info/t/what-should-do-do/1426
;
(none? do [])
(
    x: <overwritten>
    '~ = ^ x: do []
    '~ = ^x  ; DO doesn't mess around with void isotopes--use EVAL instead
)
(
    x: 10
    did all [
        '~ = ^ x: eval []
        unset? 'x
    ]
)
(
    x: 10
    did all [
        10 maybe x: eval []
        unset? 'x
    ]
)

[
    (foo: func [] [], true)

    (void? foo)

    ('~void~ = ^ applique :foo [])
    (void? applique :foo [])

    ('~void~ = ^ eval :foo)
    (void? eval :foo)

    ('~ = ^ do :foo)
]

; Explicit return of VOID is also invisible
[
    (did foo: func [return: [<opt> <void> any-value!]] [return void])

    (void? foo)
    ('~void~ = ^ foo)

    (3 = (1 + 2 foo))
]

; Not providing an argument acts the same
[
    (did foo: func [return: [<opt> <void> any-value!]] [return])

    (void? foo)
    ('~void~ = ^ foo)

    (3 = (1 + 2 foo))
]

; The ~void~ isotope can be used as a synonym for VOID
[
    (did foo: func [return: [<opt> <void> any-value!]] [return ~void~])

    (void? foo)
    ('~void~ = ^ foo)

    ('~void~ = ^ applique :foo [])
    (none? do :foo)
]


[(
    foo: func [return: <none>] []
    '~ = ^ foo
)(
    data: [a b c]
    f: func [return: <none>] [append data [1 2 3]]
    '~ = ^ f
)]

; `~` isotope is the type of locals before they are assigned
(
    f: func [<local> loc] [reify get/any 'loc]
    f = '~
)(
    f: func [<local> loc] [^loc]
    f = '~
)


; Genuine unbound words exist (e.g. product of MAKE WORD!) but there are also
; words that are attached to a context, but have no definition in that context
; or anything it inherits from.  These have never had an "emerge" operation
; (at time of writing, just assigning a SET-WORD! with the binding is enough
; to do such an emergence, though something like a JavaScript strict mode
; would demand some kind of prior declaration of intent to use the name).
;
(did all [
    e: trap [get/any 'asiieiajiaosdfbjakbsjxbjkchasdf]
    e.id = 'unassigned-attach
    e.arg1 = 'asiieiajiaosdfbjakbsjxbjkchasdf
])

; MATCH will match a bad-word! as-is, but falsey inputs produce isotopes
[
    (''~preserved~ = ^ match bad-word! '~preserved~)
    ('~null~ = ^ match null null)
]

; ~quit~ is the label of the BAD-WORD! isotope you get by default from QUIT.
; If the result is meant to be used, then QUIT should be passed an argument,
; but the idea is to help draw attention to when a script was cut short
; prematurely via a QUIT command.  Isotopes may be passed.
;
; Note: DO of BLOCK! does not catch quits, so TEXT! is used here.
[
    (1 = do "quit 1")
    ('~quit~ =  ^ do "quit")
    ('~isotope~ = ^ do "quit ~isotope~")
    ('~plain~ = do "quit '~plain~")
]

; Isotopes make it easier to write generic routines that handle BAD-WORD!
; values, so long as they are "friendly" (e.g. come from picking out of a
; block vs. running it, or come from a quote evaluation).
;
([~abc~ ~def~] = collect [keep [~abc~], keep [~def~]])

; Erroring modes of BAD-WORD! are being fetched by WORD! and logic tests.
; They are inert values otherwise, so PARSE should treat them such.
;
; !!! Review: PARSE should probably error on rules like `some ~foo~`, and
; there needs to be a mechanism to indicate that it's okay for a rule to
; literally match something that's not set vs. be a typo.
;
(did parse3 [~foo~ ~foo~] [some '~foo~])  ; acceptable
(did parse3 [~foo~ ~foo~] [some ~foo~])  ; !!! shady, rethink
(
    foo: '~foo~
    did parse3 [~foo~ ~foo~] [some foo]
)
(
    foo: ~foo~
    e: trap [
        parse3 [~foo~ ~foo~] [some foo]
    ]
    e.id = 'bad-word-get
)

[#68 https://github.com/metaeducation/ren-c/issues/876
    ('need-non-end = (trap [a:]).id)
    (
        a: 1020
        did all [
            none? a: ()
            unset? 'a
        ]
    )
]


(error? trap [a: ~none~, a])
(not error? trap [set 'a '~none~])

(
    a-value: ~none~
    e: trap [a-value]
    e.id = 'bad-word-get
)


; REIFY is used to make isotopes into the non-isotope form, pass through all
; other values.
[
    ('~foo~ = reify ~foo~)
    ('~null~ = reify null)
    ('~null~ = reify ~null~)

    (10 = reify 10)
    ((the '''a) = reify the '''a)
]
