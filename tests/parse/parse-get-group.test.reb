; %parse-get-group.test.reb
;
; GET-GROUP!s will splice rules, null means no rule but succeeds...FALSE is
; useful for failing, and TRUE is a synonym for NULL in this context.
;
; They act like a COMPOSE/ONLY that runs each time the GET-GROUP! is passed.

(uparse? "aaa" [:(if false ["bbb"]) "aaa"])
(uparse? "bbbaaa" [:(if true ["bbb"]) "aaa"])

(uparse? "aaabbb" [:([some "a"]) :([some "b"])])
(uparse? "aaabbb" [:([some "a"]) :(if false [some "c"]) :([some "b"])])
(uparse? "aaa" [:('some) "a"])
(not uparse? "aaa" [:(1 + 1) "a"])
(uparse? "aaa" [:(1 + 2) "a"])
(
    count: 0
    uparse? ["a" "aa" "aaa"] [some [into text! [:(count: count + 1) "a"]]]
)

[https://github.com/red/red/issues/562
    (not uparse? [+] [while ['+ :(no)]])
    (not uparse? "+" [while [#+ :(no)]])
]


[
    (
        x: ~
        uparse? [2 4 6] [while [x: integer! :(even? x)]]
    )
    (
        x: ~
        not uparse? [1] [x: integer! :(even? x)]
    )
    (
        x: ~
        not uparse? [1 5] [some [x: integer! :(even? x)]]
    )
]

[
    (
        x: ~
        uparse? "246" [while [x: across <any> :(even? load-value x)]]
    )
    (
        x: ~
        not uparse? "1" [x: across <any> :(even? load-value x)]
    )
    (
        x: ~
        not uparse? "15" [some [x: across <any> :(even? load-value x)]]
    )
]


[https://github.com/red/red/issues/563
    (
        f563: func [t [text!]] [uparse? t [while r]]

        r: [#+, :(res: f563 "-", assert [not res], res)]

        did all [
            not f563 "-"
            not f563 "+"
        ]
    )
]

[https://github.com/red/red/issues/564
    (
        f: func [
            s [text!]
        ] [
            r: [
                l: across <any> (l: load-value l)
                x: across repeat (l) <any>
                [
                    #","
                    | #"]" :(f x)
                ]
            ]
            uparse? s [while r <end>]
        ]
        f "420,]]"
    )
]
