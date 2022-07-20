; %parse-quoted.test.reb
;
; Quoted values are matched as is, but for strings they are formed.

(
    did all [
        pos: parse* [... [a b]] [to '[a b], <here>]
        pos = [[a b]]
    ]
)
([a b] == parse [... [a b]] [thru '[a b]])
(1 == parse [1 1 1] [some '1])

; !!! Review: how do we SUBPARSE a QUOTED! series?
;
;   pos: parse* [''[1 + 2]] [subparse quoted! [copy x to <end>], <here>]
;   [] == pos
;   x == [1 + 2]
;

[
    (none? parse [] [])
    ('a == parse [a] ['a])
    (didn't parse [a] ['b])
    ('b == parse [a b] ['a 'b])
    ('a == parse [a] [['a]])
    ('b == parse [a b] [['a] 'b])
    ('b == parse [a b] ['a ['b]])
    ('b == parse [a b] [['a] ['b]])

    (
        res: ~
        did all [
            1 == parse [] [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == parse [a] ['a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't parse [a] ['b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            1 == parse [] [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == parse [a] [['a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't parse [a] [['b (res: 1)]]
            res = '~before~
        ]
    )
]


; Ren-C made it possible to use quoted WORD!s in place of CHAR! or TEXT! to
; match in strings.  This gives a cleaner look, as you drop off 3 vertical
; tick marks from everything like ["ab"] to become ['ab]
[
    (did all [
        pos: parse* "abbbbbc" ['a some ['b], <here>]
        "c" = pos
    ])
    (did all [
        pos: parse* "abbbbc" ['ab, some ['bc | 'b], <here>]
        "" = pos
    ])
    (did all [
        pos: parse* "abc10def" ['abc '10, <here>]
        "def" = pos
    ])
]

[#682 (  ; like the old way...
    t: ~
    parse "<tag>text</tag>" [thru '<tag> t: across to '</tag> to <end>]
    t == "text"
)(
    "text" = parse "<tag>text</tag>" [between '<tag> '</tag>]  ; ah, parse!
)]

[
    (
        res: ~
        did all [
            'a == parse [a] [res: 'a]
            res = 'a
        ]
    )
    (
        res: ~
        did all [
            'a == parse [a a] [res: repeat 2 'a]
            res = 'a
        ]
    )
    (
        res: '~before~
        did all [
            didn't parse [a a] [res: repeat 3 'a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            'a == parse [a] [res: ['a]]
            res = 'a
        ]
    )
    (
        res: 0
        did all [
            'b == parse [a a b] [<any> res: 'a <any>]
            res = 'a
        ]
    )
]
