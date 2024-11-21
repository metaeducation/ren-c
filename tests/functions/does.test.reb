; %does.test.reb
;
; DOES has no RETURN behavior (it did in R3-Alpha).


(
    backup: block: copy [a b]
    /f: does [append block [c d]]
    f
    block: copy [x y]
    f
    all [
        backup = [a b [c d]]
        block = [x y [c d]]
    ]
)

; For a time, DOES quoted its argument and was "reframer-like" if it was
; a WORD! or PATH!.  Now that REFRAMER exists as a generalized facility, if
; you wanted a DOES that was like that, you make one...here's DOES+
[
    (does+: reframer lambda [f [frame!]] [
        does [eval copy f]
    ]
    ok)

    (
        backup: block: copy [a b]
        /f: does+ append block [c d]
        f
        block: copy [x y]
        f
        all [
            backup = [a b [c d] [c d]]
            block = [x y]
        ]
    )

    (
        x: 10
        y: 20
        flag: 'true
        /z: does+ all [x: x + 1, true? flag, y: y + 2, <finish>]
        all [
            z = <finish>, x = 11, y = 22
            elide (flag: 'false)
            z = null, x = 12, y = 22
        ]
    )

    (
        /catcher: does+ catch [throw 10]
        catcher = 10
    )
]

(
    o1: make object! [
        a: 10
        /b: method [] [return if ok [.a]]
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)(
    o1: construct [
        a: 10
        /b: method [] [let /f: does [.a] return f]
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)(
    o1: construct [
        a: 10
        /b: method [] [let /f: lambda [] [.a] return f]
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)(
    o1: make object! [  ; "CONTEXT"
        a: 10
        /b: does [let /f: does [a], f]
    ]
    o2: make o1 [a: 20]

    o2/b = 10  ; need METHOD to get the member selection
)

(
    o1: construct [
        a: 10
        /b: method [] [let /f: lambda [] [.a: 30] return f]
    ]
    o2: make o1 [a: 20]

    all [
        o2/b = 30
        o2.a = 30
    ]
)
