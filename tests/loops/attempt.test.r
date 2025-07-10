; %attempt.test.r
;
; ATTEMPT is just a REPEAT specialized to 1 iteration.
;
; It is useful to help in writing loop compositions (when each step in
; the composition wants to run the body once, and detect if it did BREAK
; or CONTINUE.
;
; But it's also just a useful tool in general for control flow, giving
; you essentially a CATCH with two customized THROW options, and the
; ability to drop a value out of the bottom.

[(
    x: y: null
    all [
        null = attempt [
            x: 1020
            break
            y: 304
        ]
        x = 1020
        y = null
    ]
)(
    x: y: null
    all [
        trash? attempt [
            x: 1020
            continue
            y: 304
        ]
        x = 1020
        y = null
    ]
)(
    x: y: t: null
    all [
        null = attempt [
            x: 1020
            break
            y: 304
        ] then [
            t: <then>
        ]
        x = 1020
        y = null
        t = null
    ]
)(
    x: y: t: null
    all [
        <then> = attempt [
            x: 1020
            continue
            y: 304
        ] then [
            t: <then>
        ]
        x = 1020
        y = null
        t = <then>
    ]
)(
    x: y: e: null
    all [
        <else> = attempt [
            x: 1020
            break
            y: 304
        ] else [
            e: <else>
        ]
        x = 1020
        y = null
        e = <else>
    ]
)(
    x: y: e: null
    all [
        trash? attempt [
            x: 1020
            continue
            y: 304
        ] else [
            e: <else>
        ]
        x = 1020
        y = null
        e = null
    ]
)]
