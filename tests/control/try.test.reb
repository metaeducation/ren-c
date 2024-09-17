; functions/control/try.r
(
    e: sys/util/rescue [1 / 0]
    e/id = 'zero-divide
)
(
    success: true
    error? sys/util/rescue [
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
    error? sys/util/rescue [f1]
    success
)
[#822
    (sys/util/rescue [make error! ""] then [<branch-not-run>] else [true])
]
(sys/util/rescue [fail make error! ""] then [true])
(sys/util/rescue [1 / 0] then :error?)
(sys/util/rescue [1 / 0] then func [e] [error? e])
(sys/util/rescue [] then func [e] [<handler-not-run>] else [true])
[#1514
    (error? sys/util/rescue [sys/util/rescue [1 / 0] then :add])
]

[#1506 ((
    10 = reeval func [] [sys/util/rescue [return 10] 20]
))]

; ENRESCUE (similar to RESCUE, but puts normal result in a block)

(void? first sys/util/enrescue [])
(null? sys/util/enrescue [null])
([3] = sys/util/enrescue [1 + 2])
([[b c]] = sys/util/enrescue [skip [a b c] 1])
('no-arg = (sys/util/enrescue [the])/id)
