; bad-word.test.reb

(not bad-word? 1)

; Labeled BAD-WORD!s can be literal, or created from WORD!
(
    v: make bad-word! 'labeled
    did all [
        bad-word? friendly get/any 'v
        '~labeled~ = friendly get/any 'v
        'labeled = label of friendly get/any 'v
    ]
)

; Plain ~ is not a void, but a WORD!.  But things like ~~~ are not WORD!,
; because that would be ambiguous with a BAD-WORD! with the word-label of ~.
; So ~ is the only "~-word"
;
(word? first [~])
('scan-invalid = ((trap [load-value "~~"])/id))
(bad-word? first [~~~])
('~ = label of '~~~)

; NULL is the response for when there is no content:
; https://forum.rebol.info/t/what-should-do-do/1426
;
('~void~ = ^ do [])
(
    foo: func [] []
    '~void~ = ^ ^ foo
)
('~void~ = ^ applique :foo [])
('~void~ = ^ do :foo)

; ~void~ is the convention for what you get by RETURN with no argument, or
; if the spec says to <void> any result.
[(
    foo: func [] [return]
    '~void~ = ^ ^ foo
)(
    foo: func [] [return ~void~]  ; /VOID only invisible if arg is void
    '~void~ = ^ foo
)(
    '~void~ = ^ applique :foo []
)(
    '~void~ = ^ do :foo
)]

[(
    foo: func [return: <none>] []
    '~none~ = ^ foo
)(
    data: [a b c]
    f: func [return: <none>] [append data [1 2 3]]
    '~none~ = ^ f
)]

; ~unset~ is the type of locals before they are assigned
(
    f: func [<local> loc] [friendly get/any 'loc]
    f = '~unset~
)(
    f: func [<local> loc] [^loc]
    f = '~unset~
)


; ~unset~ is also the type of things that just were never declared
(
    '~unset~ = friendly get/any 'asiieiajiaosdfbjakbsjxbjkchasdf
)

; MATCH will match a bad-word! as-is, but falsey inputs produce ~falsey~
[
    (''~preserved~ = ^ match bad-word! '~preserved~)
    ('~falsey~ = ^ match null null)
]

; CYCLE once differentiated a STOP result from BREAK with ~stopped~, but now
; it uses a null isotope for similar purposes.
[
    ('~null~ = ^ cycle [stop])
    (null = cycle [break])
]

; ~quit~ is the label of the BAD-WORD! you get by default from QUIT
; Note: DO of BLOCK! does not catch quits, so TEXT! is used here.
[
    (1 = do "quit 1")
    ('~quit~ =  ^ do "quit")
    (''~unmodified~ = ^ do "quit '~unmodified~")
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
; literally match ~unset~ vs. be a typo.
;
(parse? [~foo~ ~foo~] [some '~foo~])  ; acceptable
(parse? [~foo~ ~foo~] [some ~foo~])  ; !!! shady, rethink
(
    foo: '~foo~
    e: trap [
        parse [~foo~ ~foo~] [some foo]  ; not acceptable  !!! how to overcome?
    ]
    e/id = 'bad-word-get
)

(
    is-barrier?: func [x [<end> integer!]] [null? x]
    is-barrier? ()
)

[#68 https://github.com/metaeducation/ren-c/issues/876
    ('need-non-end = (trap [a: ()])/id)
]


(error? trap [a: ~void~, a])
(not error? trap [set 'a '~void~])

(
    a-value: ~void~
    e: trap [a-value]
    e/id = 'bad-word-get
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
