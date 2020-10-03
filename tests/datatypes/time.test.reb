; datatypes/time.r
(time? 0:00)
(not time? 1)
(time! = type of 0:00)
(0:0:10 = make time! 10)
(0:0:10 = to time! 10)
(error? trap [to time! "a"])
("0:00" = mold 0:00)

; small value
(
    did any [
        error? trap [t: -596522:0:0 - 1:00]
        t = load mold t
    ]
)

; big value
(
    did any [
        error? trap [t: 596522:0:0 + 1:00]
        t = load mold t
    ]
)

; strange value
(error? trap [load "--596523:-14:-07.772224"])

; minimal time
(time? -596523:14:07.999999999)

; maximal negative time
(negative? -0:0:0.000000001)

; minimal positive time
(positive? 0:0:0.000000001)

; maximal time
(time? 596523:14:07.999999999)

[#96 (
    time: 1:23:45.6
    1:23:45.7 = (time + 0.1)
)]
[#96 (
    time: 1:23:45.6
    0:41:52.8 = (time * 0.5)
)]


[#1156 (
    0:01:00 / 0:00:07 = 8.571428571428571
)]
[#1156 (
    8 * 0:00:07 = 0:00:56
)]
