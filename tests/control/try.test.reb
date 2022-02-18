; functions/control/try.r
(
    e: trap [1 / 0]
    e.id = 'zero-divide
)
(
    success: true
    error? trap [
        1 / 0
        success: false
    ]
    success
)
(
    success: true
    f1: does [
        1 / 0
        success: false
    ]
    error? trap [f1]
    success
)
[#822
    (trap [make error! ""] then [<branch-not-run>] else [true])
]
(trap [fail make error! ""] then [true])
(trap [1 / 0] then :error?)
(trap [1 / 0] then e -> [error? e])
(trap [] then (func [e] [<handler-not-run>]) else [true])
[#1514
    (error? trap [trap [1 / 0] then :add])
]

[#1506 ((
    10 = reeval func [return: [integer!]] [trap [return 10] 20]
))]

; ENTRAP (similar to TRAP but single result, ^META result if not an error)

('~none~ = entrap [])
(null = ^ entrap [null])  ; !!! should this be '~null~ = ^ entrap [null] ?
((the '3) = entrap [1 + 2])
((the '[b c]) = entrap [skip [a b c] 1])
('no-arg = (entrap [the]).id)


; Multiple return values
(
    [e v]: trap [10 + 20]
    did all [
        null? e
        v = 30
    ]
)
(
    [e v]: trap [fail "Hello"]
    did all [
        error? e
        unset? 'v
    ]
)
