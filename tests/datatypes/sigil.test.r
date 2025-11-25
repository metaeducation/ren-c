; %sigil.test.r
;
; Sigils cover symbols that aren't legal words, but are useful to have
; in the evaluator and dialects:
;
;     SIGIL_META = 1     // ^
;     SIGIL_PIN = 2      // @
;     SIGIL_TIE = 3      // $

[ ; Establish FOR-EACH test to simplify further testing
(
    for-each-sigil: specialize for-each/ [
        data: [^ @ $]
    ]
    ok
)

; MOLD sigil (once verified, can take for granted in subsequent tests)

    ("^^" = mold '^)  ; caret is escape in Rebol strings
    ("@" = mold '@)
    ("$" = mold '$)

; SIGIL? sigil
(
    for-each-sigil 'sig [
        assert [sigil? sig]
        assert [space? plain sig]
    ]
    ok
)

; MOLD and FORM
(
    for-each-sigil 'sig [
        assert [(mold sig) = (form sig)]
    ]
    ok
)

; MATCH SIGIL!
(
    for-each-sigil 'sig [
        assert [sig = match [sigil?] sig]
    ]
    ok
)

; TRANSCODE
(
    roundtrip: cascade [unspaced/ transcode:one/]

    for-each-sigil 'sig [
        assert [(quote sig) = roundtrip [-[']- mold sig]]
        assert [(quote sig) = roundtrip [_ -[']- mold sig]]
        assert [sig = second roundtrip ["[" "<t>" _ mold sig "]"]]
        assert [sig = second roundtrip ["[" "<t>" _ mold sig _ "]"]]
        assert [sig = first roundtrip ["[" mold sig _ "<t>" "]"]]
        assert [sig = first roundtrip ["[" _ mold sig space "<t>]"]]
        assert [(quasi sig) = roundtrip ["~" mold sig "~"]]
    ]
    ok
)


; Test SIGIL OF for each bindable type
;
; !!! UPDATE: now legal on *all* datatypes that can appear in sequences
(
    for-each [sigil items] [
        ~null~  [  word    tu.p.le    pa/th    [bl o ck]    (gr o up)  ]
        ^       [ ^word   ^tu.p.le   ^pa/th   ^[bl o ck]   ^(gr o up)  ]
        @       [ @word   @tu.p.le   @pa/th   @[bl o ck]   @(gr o up)  ]
        $       [ $word   $tu.p.le   $pa/th   $[bl o ck]   $(gr o up)  ]
    ][
        for-each 'item items [
            assert [any [quoted? item, quasi? item, bindable? item]]
            if (degrade sigil) <> sigil of item [
                panic [mold item]
            ]
        ]
    ]
    ok
)

; ^ is APPROVE

    (3 = (1 + 2 ^ eval []))

    ~need-non-end~ !! (^)


; $ acts like BIND HERE

    (
        foo: 10
        10 = get $ first [foo bar]
    )

    ~need-non-end~ !! ($)


; @ acts like THE, with exception that it has special handling in API feeds to
; be able to reconstitute antiforms.  (See TEST-LIBREBOL)  It will bind
; its argument.

    ('x = @ x)
    ('(a b c) = @ (a b c))
    (''3 = @ '3)

    ('~null~ = @ ~null~)

    (
        var: 1020
        word: @ var  ; binds
        1020 = get word
    )
    (
        var: 1020
        word: the var  ; binds
        1020 = get word
    )
    ~need-non-end~ !! (@)
]

; Not all types can carry Sigils
(
    for-each 'item compose [
        12-Dec-2012
        ,
        (make object! [x: 10])
    ][
        assert [meta item except e -> [e.id = 'bad-sigil]]
        assert [pin item except e -> [e.id = 'bad-sigil]]
        assert [tie item except e -> [e.id = 'bad-sigil]]
    ]
    ok
)

; Limited reversible TO conversions hacked in as a demo of intent:
;
; https://rebol.metaeducation.com/t/embracing-a-useless-definition-of-to/2325/3
[
    ('a = to word! @a)
    (@a = to pinned! 'a)
    (1 = to integer! '^1)
    ('^1 = to metaform! 1)
    (_ = to rune! '$)
    ('$ = to tied! _)
]
