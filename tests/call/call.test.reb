; call/call.test.reb

[
    ; small - works
    data: copy {}
    call/wait/output [%../make/r3 "--suppress" "*" %call/print.reb "100"] data
    100 == (length-of data)
]
[
    ; medium - fails test (just under 5000 bytes transferred)
    data: copy {}
    call/wait/output [%../make/r3 "--suppress" "*" %call/print.reb "9000"] data
    9000 == (length-of data)
]
[
    ; crashes :(
    data: copy {}
    call/wait/output [%../make/r3 "--suppress" "*" %call/print.reb "80000"] data
    80'000 == (length-of data)
]

