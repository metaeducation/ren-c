; functions/control/switch.r
(
    11 = switch 1 [
        1 [11]
        2 [12]
    ]
)
(
    12 = switch 2 [
        1 [11]
        2 [12]
    ]
)

(null? switch 2 [1 []])
(trash? switch 1 [1 []])

[#2242 (
    11 = reeval func [] [switch/all 1 [1 [return 11 88]] 99]
)]

(t: 1  1 = switch t [(t)])
(1 = switch 1 [1])
