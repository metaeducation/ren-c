; %parse-get-group.test.reb
;
; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.
;
; They act like a COMPOSE that runs each time the GET-GROUP! is passed.

("aaa" == parse "aaa" [:(if false ["bbb"]) "aaa"])
("aaa" == parse "bbbaaa" [:(if true ["bbb"]) "aaa"])

("b" == parse "aaabbb" [:([some "a"]) :([some "b"])])
("b" == parse "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])

; !!! Partial rule splicing doesn't work with GET-GROUP! being a combinator
; under the constraints of the current design...this would mean it would be
; effectively a "variadic combinator", or would be sending some signal up
; to the BLOCK! combinator about what it wanted to splice.  Punt on it for now,
; as being able to redefine the behavior of GET-GROUP! via a combinator is
; much more compelling.
;
[(comment [
    ("a" == parse "aaa" [:('some) "a"])
    (didn't parse "aaa" [:(1 + 1) "a"])
    ("a" == parse "aaa" [:(1 + 2) "a"])
    (
        count: 0
        "a" == parse ["a" "aa" "aaa"] [
            some [subparse text! [:(count: count + 1) "a"]]
        ]
    )
] true)]

[https://github.com/red/red/issues/562
    (didn't parse [+] [opt some ['+ :(no)]])
    (didn't parse "+" [opt some [#+ :(no)]])
]


[
    (
        x: ~
        6 == parse [2 4 6] [some [x: integer! :(even? x)]]
    )
    (
        x: ~
        didn't parse [1] [x: integer! :(even? x)]
    )
    (
        x: ~
        didn't parse [1 5] [some [x: integer! :(even? x)]]
    )
]

[
    (
        x: ~
        "6" == parse "246" [some [
            x: across <any> :(even? load-value x)
        ]]
    )
    (
        x: ~
        didn't parse "1" [x: across <any> :(even? load-value x)]
    )
    (
        x: ~
        didn't parse "15" [some [x: across <any> :(even? load-value x)]]
    )
]


[https://github.com/red/red/issues/563
    (
        f563: lambda [t [text!]] [did parse t [opt some r]]

        r: [#+, :(res: f563 "-", assert [not res], res)]

        did all [
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
            r: [
                l: across <any> (l: load-value l)
                x: across repeat (l) <any>
                [
                    #","
                    | #"]" :(f x)
                ]
            ]
            return "" == parse s [opt some r <end>]
        ]

        f "420,]]"
    )
]

; Void handling, just vanishes
[
    ('z = parse [x z] ['x :(if false 'y) 'z])

    ('z = parse [x z] ['x :(assert [true]) 'z])
]
