; functions/control/try.r
(
    e: sys/util/rescue [1 / 0]
    e/id = 'zero-divide
)
(
    success: okay
    error? sys/util/rescue [
        1 / 0
        success: null
    ]
    success
)
(
    success: okay
    f1: does [
        1 / 0
        success: null
    ]
    error? sys/util/rescue [f1]
    success
)
[#822
    (sys/util/rescue [make error! ""] then [<branch-not-run>] else [okay])
]
(sys/util/rescue [fail make error! ""] then [okay])
(sys/util/rescue [1 / 0] then :error?)
(sys/util/rescue [1 / 0] then lambda [e] [error? e])
(sys/util/rescue [] then func [e] [<handler-not-run>] else [okay])
[#1514
    (error? sys/util/rescue [sys/util/rescue [1 / 0] then :add])
]

[#1506 ((
    10 = reeval func [] [sys/util/rescue [return 10] 20]
))]

; ENRESCUE (similar to RESCUE, but puts results in groups or antiform words)

(void? unmeta sys/util/enrescue [])
(null? unmeta sys/util/enrescue [null])
(3 = unmeta sys/util/enrescue [1 + 2])
([b c] = unmeta sys/util/enrescue [skip [a b c] 1])
('no-arg = (sys/util/enrescue [the])/id)
