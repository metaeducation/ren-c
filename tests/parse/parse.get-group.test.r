; %parse-get-group.test.r
;
; GET-GROUP!s will evaluate code to produce a rule to splice as if it had
; been written in that spot.  If a WORD! is used as a rule, it cannot be
; variadic.  So this is legal:
;
;    >> parse [a a a] [inline (if 1 = 1 ['veto]) | some 'a]
;    == a
;
; But this is not legal:
;
;    >> parse [a a a] [inline (if 1 = 1 ['some]) 'a]
;    ** Error: Too few parameters for combinator
;
; It is a particularly powerful facility--though unlike COMPOSE it will run
; the code in the group every time it is encountered in the PARSE, so can
; carry something of a runtime penalty.

[
    ; === SPECIAL ANTIFORM MEANINGS ===
    ;
    ; At one time ~null~ and ~okay~ could be used to say to continue or not.
    ; That is now taken over with the ability to VETO out of an INLINE vs.
    ; returning void.  So we get protection against nulls.  But void is
    ; willing to just make the INLINE vaporize.

    ~bad-antiform~ !! (
        'a = parse [a a a] [inline (1 = 0) | some 'a]
    )
    ~bad-antiform~ !! (
        'a = parse [a a a] ['a inline (1 = 1) elide some 'a]
    )
    (
        void = parse [a a a] ['a inline (void) elide some 'a]
    )
    (
        'a = parse [a a a] ['a inline (comment "hi") elide some 'a]
    )
]

("aaa" = parse "aaa" [inline (when null ["bbb"]) "aaa"])
("aaa" = parse "bbbaaa" [inline (when ok ["bbb"]) "aaa"])

("b" = parse "aaabbb" [inline ([some "a"]) inline ([some "b"])])
("b" = parse "aaabbb" [
    inline ([some "a"]) inline (when null [some "c"]) inline ([some "b"])]
)

; !!! Partial rule splicing doesn't work with GET-GROUP! being a combinator
; under the constraints of the current design...this would mean it would be
; effectively a "variadic combinator", or would be sending some signal up
; to the BLOCK! combinator about what it wanted to splice.  Punt on it for now,
; as being able to redefine the behavior of GET-GROUP! via a combinator is
; much more compelling.
;
[(comment [
    ("a" = parse "aaa" [inline ('some) "a"])
    ~parse-mismatch~ !! (parse "aaa" [repeat inline (1 + 1) "a"])
    ("a" = parse "aaa" [repeat inline (1 + 2) "a"])
    (
        count: 0
        "a" = parse ["a" "aa" "aaa"] [
            some [subparse text! [repeat inline (count: count + 1) "a"]]
        ]
    )
] ok)]

[https://github.com/red/red/issues/562
    ~parse-incomplete~ !! (parse [+] [opt some ['+ cond (null)]])
    ~parse-incomplete~ !! (parse "+" [opt some [#+ cond (null)]])
]


[
    (
        x: ~
        6 = parse [2 4 6] [some [x: integer! elide cond (even? x)]]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse [1] [x: integer! elide cond (even? x)]
    )
    ~parse-mismatch~ !! (
        x: ~
        parse [1 5] [some [x: integer! elide cond (even? x)]]
    )
]


[https://github.com/red/red/issues/563
    (
        res: ~
        f563: lambda [t [text!]] [did try parse t [opt some r]]

        r: [#+, inline (res: f563 "-", assert [not res], opt res)]

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
                    | #"]" inline (f x)
                ]
            ]
            return not error? parse s [opt some r <end>]
        ]

        f "420,]]"
    )
]

; Void/nihil handling, just vanishes
[
    ('z = parse [x z] ['x inline (when null 'y) 'z])

    ('z = parse [x z] ['x inline (assert [okay]) 'z])
]
