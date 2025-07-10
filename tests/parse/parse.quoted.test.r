; %parse-quoted.test.r
;
; Quoted values are matched as is, but for strings they are molded.


('(b c) = parse "<a> 100 (b c)" ['<a> space '100 space '(b c)])

(
    all [
        let pos: parse-thru [... [a b]] [to '[a b]]
        pos = [[a b]]
    ]
)
([a b] = parse [... [a b]] [thru '[a b]])
(1 = parse [1 1 1] [some '1])

; !!! Review: how do we SUBPARSE a QUOTED! series?
;
;   pos: parse-thru [''[1 + 2]] [subparse quoted! [copy x to <end>]]
;   [] = pos
;   x = [1 + 2]
;

[
    ('a = parse [a] ['a])
    ~parse-mismatch~ !! (parse [a] ['b])

    ('b = parse [a b] ['a 'b])
    ('a = parse [a] [['a]])
    ('b = parse [a b] [['a] 'b])
    ('b = parse [a b] ['a ['b]])
    ('b = parse [a b] [['a] ['b]])

    (
        res: ~
        all [
            1 = parse [] [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 = parse [a] ['a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a] ['b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            1 = parse [] [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 = parse [a] [['a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a] [['b (res: 1)]]
            res = '~before~
        ]
    )
]


; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become ['ab]
[
    (all [
        let pos: parse-thru "abbbbbc" ['a some ['b]]
        "c" = pos
    ])
    (all [
        let pos: parse-thru "abbbbc" ['ab, some ['bc | 'b]]
        "" = pos
    ])
    (all [
        let pos: parse-thru "abc10def" ['abc '10]
        "def" = pos
    ])

    (1 = parse "1 1 1" [some ['1 [space | <end>]]])
]

[#682 (  ; like the old way...
    t: ~
    parse "<tag>text</tag>" [thru '<tag> t: across to '</tag> to <end>]
    t = "text"
)(
    "text" = parse "<tag>text</tag>" [between '<tag> '</tag>]  ; ah, parse!
)]

[
    (
        res: ~
        all [
            'a = parse [a] [res: 'a]
            res = 'a
        ]
    )
    (
        res: ~
        all [
            'a = parse [a a] [res: repeat 2 'a]
            res = 'a
        ]
    )
    (
        res: '~before~
        all [
            error? parse [a a] [res: repeat 3 'a]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            'a = parse [a] [res: ['a]]
            res = 'a
        ]
    )
    (
        res: 0
        all [
            'b = parse [a a b] [one res: 'a one]
            res = 'a
        ]
    )
]

; Case sensitivity (consider this also for THE or other quoted construct
[
    (equal? 'a (parse [a] ['a]))
    (equal? 'A (parse [a] ['A]))

    (equal? 'a (parse:case [a] ['a]))
    ~parse-mismatch~ !! (parse:case [a] ['A])

    (equal? 'p/a (parse [p/a] ['p/a]))
    (equal? 'p/A (parse [p/a] ['p/A]))

    (equal? 'p/a (parse:case [p/a] ['p/a]))
    ~parse-mismatch~ !! (parse:case [p/a] ['p/A])
]
