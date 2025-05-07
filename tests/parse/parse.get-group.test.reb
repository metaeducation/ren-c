; %parse-get-group.test.reb
;
; GET-GROUP!s will evaluate code to produce a rule to splice as if it had
; been written in that spot.  If a WORD! is used as a rule, it cannot be
; variadic.  So this is legal:
;
;    >> parse [a a a] [:(if 1 = 1 ['bypass]) | some 'a]
;    == a
;
; But this is not legal:
;
;    >> parse [a a a] [:(if 1 = 1 ['some]) 'a]
;    ** Error: Too few parameters for combinator
;
; It is a particularly powerful facility--though unlike COMPOSE it will run
; the code in the group every time it is encountered in the PARSE, so can
; carry something of a runtime penalty.

[
    ; === SPECIAL ANTIFORM MEANINGS ===
    (
        ; NULL means no match (does not cause abrupt failure)
        'a = parse [a a a] [:(1 = 0) | some 'a]
    )
    (
        ; OKAY synthesizes VOID and continues matching
        'a = parse [a a a] ['a :(1 = 1) elide some 'a]
    )
    (
        ; VOID synthesizes VOID and continues matching
        void = parse [a a a] ['a :(void) elide some 'a]
    )
    (
        ; VOID synthesizes VOID and continues matching
        'a = parse [a a a] ['a :(comment "hi") elide some 'a]
    )
]

("aaa" = parse "aaa" [:(if null ["bbb"]) "aaa"])
("aaa" = parse "bbbaaa" [:(if ok ["bbb"]) "aaa"])

("b" = parse "aaabbb" [:([some "a"]) :([some "b"])])
("b" = parse "aaabbb" [:([some "a"]) :(if null [some "c"]) :([some "b"])])

; !!! Partial rule splicing doesn't work with GET-GROUP! being a combinator
; under the constraints of the current design...this would mean it would be
; effectively a "variadic combinator", or would be sending some signal up
; to the BLOCK! combinator about what it wanted to splice.  Punt on it for now,
; as being able to redefine the behavior of GET-GROUP! via a combinator is
; much more compelling.
;
[(comment [
    ("a" = parse "aaa" [:('some) "a"])
    ~parse-mismatch~ !! (parse "aaa" [:(1 + 1) "a"])
    ("a" = parse "aaa" [:(1 + 2) "a"])
    (
        count: 0
        "a" = parse ["a" "aa" "aaa"] [
            some [subparse text! [:(count: count + 1) "a"]]
        ]
    )
] ok)]

[https://github.com/red/red/issues/562
    ~parse-incomplete~ !! (parse [+] [opt some ['+ when (null)]])
    ~parse-incomplete~ !! (parse "+" [opt some [#+ when (null)]])
]


[
    (
        x: ~
        6 = parse [2 4 6] [some [x: integer! elide when (even? x)]]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse [1] [x: integer! elide when (even? x)]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse [1 5] [some [x: integer! elide when (even? x)]]
    )
]


[https://github.com/red/red/issues/563
    (
        res: ~
        f563: lambda [t [text!]] [did try parse t [opt some r]]

        r: [#+, :(res: f563 "-", assert [not res], maybe res)]

        all [
            not f563 "-"
            not f563 "+"
        ]
    )
]

; This is a weird test that recurses:
;
;     f "420,]]"      (l=4, x="20,]")
;     => f "20,]]"    (l=2, x="0,")
;        => f "0,"    (l=0, x="")
;
[https://github.com/red/red/issues/564
    (
        f: func [
            s [text!]
        ][
            let [r l x]
            r: [
                l: across one (l: transcode:one l)
                x: across repeat (l) one
                [
                    #","
                    | #"]" :(f x)
                ]
            ]
            return unraised? parse s [opt some r <end>]
        ]

        f "420,]]"
    )
]

; Void/nihil handling, just vanishes
[
    ('z = parse [x z] ['x :(if null 'y) 'z])

    ('z = parse [x z] ['x :(assert [okay]) 'z])
]
