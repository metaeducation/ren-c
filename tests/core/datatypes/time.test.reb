; datatypes/time.r
[time? 0:00]
[not time? 1]
[time! = type-of 0:00]
[0:0:10 = make time! 10]
[0:0:10 = to time! 10]
[error? try [to time! "a"]]
["0:00" = mold 0:00]
; small value
[
    any? [
        error? try [t: -596522:0:0 - 1:00]
        t = load mold t
    ]
]
; big value
[
    any? [
        error? try [t: 596522:0:0 + 1:00]
        t = load mold t
    ]
]
; strange value
[error? try [load "--596523:-14:-07.772224"]]
; minimal time
[time? -596523:14:07.999999999]
; maximal negative time
[negative? -0:0:0.000000001]
; minimal positive time
[positive? 0:0:0.000000001]
; maximal time
[time? 596523:14:07.999999999]
; bug#96
[
    time: 1:23:45.6
    1:23:45.7 = (time + 0.1)
]
; bug#96
[
    time: 1:23:45.6
    0:41:52.8 = (time * .5)
]
