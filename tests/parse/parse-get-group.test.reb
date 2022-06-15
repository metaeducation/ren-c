; %parse-get-group.test.reb
;
; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.
;
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

("aaa" == uparse "aaa" [:(if false ["bbb"]) "aaa"])
("aaa" == uparse "bbbaaa" [:(if true ["bbb"]) "aaa"])

("b" == uparse "aaabbb" [:([some "a"]) :([some "b"])])
("b" == uparse "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])

; !!! Partial rule splicing doesn't work with GET-GROUP! being a combinator
; under the constraints of the current design...this would mean it would be
; effectively a "variadic combinator", or would be sending some signal up
; to the BLOCK! combinator about what it wanted to splice.  Punt on it for now,
; as being able to redefine the behavior of GET-GROUP! via a combinator is
; much more compelling.
;
[(comment [
    ("a" == uparse "aaa" [:('some) "a"])
    (didn't uparse "aaa" [:(1 + 1) "a"])
    ("a" == uparse "aaa" [:(1 + 2) "a"])
    (
        count: 0
        "a" == uparse ["a" "aa" "aaa"] [some [into text! [:(count: count + 1) "a"]]]
    )
] true)]

[https://github.com/red/red/issues/562
    (didn't uparse [+] [opt some ['+ :(no)]])
    (didn't uparse "+" [opt some [#+ :(no)]])
]


[
    (
        x: ~
        #[true] == uparse [2 4 6] [some [x: integer! :(even? x)]]
    )
    (
        x: ~
        didn't uparse [1] [x: integer! :(even? x)]
    )
    (
        x: ~
        didn't uparse [1 5] [some [x: integer! :(even? x)]]
    )
]

[
    (
        x: ~
        #[true] == uparse "246" [some [
            x: across <any> :(even? load-value x)
        ]]
    )
    (
        x: ~
        didn't uparse "1" [x: across <any> :(even? load-value x)]
    )
    (
        x: ~
        didn't uparse "15" [some [x: across <any> :(even? load-value x)]]
    )
]


[https://github.com/red/red/issues/563
    (
        f563: lambda [t [text!]] [did uparse t [opt some r]]

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
            return "" == uparse s [opt some r <end>]
        ]

        f "420,]]"
    )
]

; Void handling, just vanishes
[
    ('z = uparse [x z] ['x :(if false 'y) 'z])

    ('z = uparse [x z] ['x :(assert [true]) 'z])
]
