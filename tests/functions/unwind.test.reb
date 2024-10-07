; UNWIND allows one to name specific stack levels to jump up to
; and return a value from.  It uses the same mechanism as RETURN

(<good> = if 1 < 2 [reeval does [reduce [unwind :if <good>, <bad1>] <bad2>]])

; UNWIND the IF should stop the loop
(
    cycling: 'yes
    /f1: does [
        if 1 < 2 [
            while [yes? cycling] [cycling: 'no, unwind :if ~]
            <bad>
        ]
        <good>
    ]
    <good> = f1
)

; Related to #1519
(
    cycling: 'yes
    if-not: adapt if/ [condition: not get:any $condition]
    /f1: does [
        if-not 1 > 2 [
            while [if yes? cycling [unwind :if-not <ret>] cycle?] [
                cycling: 'no
                2
            ]
        ]
    ]
    f1 = <ret>
)
