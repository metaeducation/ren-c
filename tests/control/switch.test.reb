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

(void? switch* 1 [1 []])
(blank? switch 1 [1 []])

(
    cases: reduce [1 head of insert copy [] try [1 / 0]]
    error? switch 1 cases
)

[#2242 (
    11 = eval does [switch/all 1 [1 [return 11 88]] 99]
)]

(t: 1 | 1 = switch t [(t)])
(1 = switch/default 1 [] [1])

