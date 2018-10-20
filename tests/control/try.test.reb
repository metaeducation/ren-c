; functions/control/try.r
(
    e: trap [1 / 0]
    e/id is 'Zero-Divide
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
; testing TRAP/WITH
[#822
    (error? trap/with [make error! ""] [0])
]
(trap/with [fail make error! ""] [true])
(trap/with [1 / 0] :error?)
(trap/with [1 / 0] func [e] [error? e])
(trap/with [true] func [e] [false])
[#1514
    (error? trap [trap/with [1 / 0] :add])
]

[#1506 ((
    10 == eval func [] [trap [return 10] 20]
))]

; ENTRAP (similar to TRAP, but puts normal result in a block)

(void? first entrap [])
(null? entrap [null])
([3] == entrap [1 + 2])
([[b c]] == entrap [skip [a b c] 1])
('No-Arg is (entrap [quote])/id)
