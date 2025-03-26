; %sigil.test.reb
;
; Sigils cover symbols that aren't legal words, but are useful to have
; in the evaluator and dialects:
;
;     SIGIL_META = 1     // ^
;     SIGIL_WILD = 2     // &
;     SIGIL_THE = 3      // @
;     SIGIL_VAR = 4      // $

[ ; Establish FOR-EACH test to simplify further testing
(
    for-each-sigil: specialize for-each/ [
        data: [^ & @ $ &]
    ]
    ok
)

; MOLD sigil (once verified, can take for granted in subsequent tests)

    ("^^" = mold '^)  ; caret is escape in Rebol strings
    ("&" = mold '&)
    ("@" = mold '@)
    ("$" = mold '$)

; SIGIL? sigil
(
    for-each-sigil 'sig [
        assert [sigil? sig]
        assert [sigil! = type of sig]
    ]
    ok
)

; MOLD and FORM
(
    for-each-sigil 'sig [
        assert [(mold sig) = to text! sig]
        assert [(form sig) = to text! sig]
    ]
    ok
)

; MATCH SIGIL!
(
    for-each-sigil 'sig [
        assert [sig = match sigil! sig]
    ]
    ok
)

; TRANSCODE
(
    let roundtrip: cascade [unspaced/ transcode:one/]

    for-each-sigil 'sig [
        assert [(quote sig) = roundtrip [-{'}- mold sig]]
        assert [(quote sig) = roundtrip [_ -{'}- mold sig]]
        assert [sig = second roundtrip ["[" "<t>" _ mold sig "]"]]
        assert [sig = second roundtrip ["[" "<t>" _ mold sig _ "]"]]
        assert [sig = first roundtrip ["[" mold sig _ "<t>" "]"]]
        assert [sig = first roundtrip ["[" _ mold sig space "<t>]"]]
        assert [
            let e: trap [roundtrip ["~" mold sig "~"]]
            e.id = 'scan-invalid  ; quasi/anti forms of sigil are illegal ATM
        ]
    ]
    ok
)


; Test SIGIL OF for each bindable type
(
    for-each [sigil items] [
        ~null~  [  word    tu.p.le    pa/th    [bl o ck]    (gr o up)  ]
        ^       [ ^word   ^tu.p.le   ^pa/th   ^[bl o ck]   ^(gr o up)  ]
        &       [ &word   &tu.p.le   &pa/th   &[bl o ck]   &(gr o up)  ]
        @       [ @word   @tu.p.le   @pa/th   @[bl o ck]   @(gr o up)  ]
        $       [ $word   $tu.p.le   $pa/th   $[bl o ck]   $(gr o up)  ]
    ][
        for-each 'item items [
            if blank? item [continue]
            assert [any [quoted? item, quasi? item, bindable? item]]
            if (degrade sigil) <> sigil of item [
                fail [mold item]
            ]
        ]
    ]
    ok
)

; ^ is META

    ((@ '3) = ^ 1 + 2)
    (^null = ^ null)

    ~need-non-end~ !! (^)


; $ is bind to current context (faster version of IN [])

    (
        foo: 10
        10 = get $ first [foo bar]
    )

    ~need-non-end~ !! ($)


; @ is THE, with exception that it has special handling in API feeds to
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
