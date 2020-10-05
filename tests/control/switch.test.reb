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
(void? switch 1 [1 []])

(
    cases: reduce [1 head of insert copy [] trap [1 / 0]]
    error? switch 1 cases
)

[#2242 (
    11 = reeval func [] [switch/all 1 [1 [return 11 88]] 99]
)]

(t: 1 | 1 = switch t [(t)])
(1 = switch 1 [1])


; SWITCH/ALL gives the last branch result, but prioritizes fallout

(<b> = switch/all 10 [5 + 5 [<a>] 5 + 5 [<b>]])
(<b> = switch/all 10 [0 + 0 [<a>] 5 + 5 [<b>]])
(<a> = switch/all 10 [5 + 5 [<a>] 0 + 0 [<b>]])
(null = switch/all 10 [0 + 0 [<a>] 0 + 0 [<b>]])

(<fallout> = switch/all 10 [5 + 5 [<a>] 5 + 5 [<b>] <fallout>])
(<fallout> = switch/all 10 [0 + 0 [<a>] 5 + 5 [<b>] <fallout>])
(<fallout> = switch/all 10 [5 + 5 [<a>] 0 + 0 [<b>] <fallout>])
(<fallout> = switch/all 10 [0 + 0 [<a>] 0 + 0 [<b>] <fallout>])


; New feature for specifying comparison functions via a TUPLE!

(<b> = switch 10 .(reduce pick [:greater? :lesser?] 1) [20 [<a>] 5 [<b>]])
(<a> = switch 10 .(reduce pick [:greater? :lesser?] 2) [20 [<a>] 5 [<b>]])
(<yep> = switch 10 .greater? [20 [<nope>] 5 [<yep>]])
(<yep> = switch 10 .> [20 [<nope>] 5 [<yep>]])
