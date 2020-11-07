; UNWIND allows one to name specific stack levels to jump up to
; and return a value from.  It uses the same mechanism as RETURN

(<good> = if 1 < 2 [reeval does [reduce [unwind :if <good>, <bad1>] <bad2>]])

; UNWIND the IF should stop the loop
(
    cycle?: true
    f1: does [
        if 1 < 2 [
            while [cycle?] [cycle?: false, unwind :if]
            <bad>
        ]
        <good>
    ]
    <good> = f1
)

; Related to #1519
(
    cycle?: true
    if-not: adapt 'if [condition: not :condition]
    f1: does [
        if-not 1 > 2 [
            while [if cycle? [unwind :if-not <ret>] cycle?] [cycle?: false 2]
        ]
    ]
    f1 = <ret>
)
