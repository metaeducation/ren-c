; %does.test.reb
;
; Behavior varies from from R3-Alpha:
;
; * Unlike FUNC [] [...], the DOES [...] has no RETURN
; * For types like FILE! / URL! / STRING! it will act as DO when called
;
; It also locks the BLOCK!.  It's still experimental, but the idea of using
; a separate generator (e.g. not a FUNC) and being able to say `does %foo.r`
; are likely firm.

(
    three: does "1 + 2"
    3 = three
)

(
    make-x: does the 'x
    make-x = 'x
)

; DOES of BLOCK! as more an arity-0 func... block evaluated each time
(
    backup: block: copy [a b]
    f: does [append/only block [c d]]
    f
    block: copy [x y]
    f
    did all [
        backup = [a b [c d]]
        block = [x y [c d]]
    ]
)

; For a time, DOES quoted its argument and was "reframer-like" if it was
; a WORD! or PATH!.  Now that REFRAMER exists as a generalized facility, if
; you wanted a DOES that was like that, you make one...here's DOES+
[
    (does+: reframer lambda [f [frame!]] [
        does [do copy f]
    ]
    true)

    (
        backup: block: copy [a b]
        f: does+ append/only block [c d]
        f
        block: copy [x y]
        f
        did all [
            backup = [a b [c d] [c d]]
            block = [x y]
        ]
    )

    (
        x: 10
        y: 20
        flag: true
        z: does+ all [x: x + 1, flag, y: y + 2, <finish>]
        did all [
            z = <finish>, x = 11, y = 22
            elide (flag: false)
            z = null, x = 12, y = 22
        ]
    )

    (
        catcher: does+ catch [throw 10]
        catcher = 10
    )
]

; !!! The following tests were designed before the creation of METHOD, at a
; time when DOES was expected to obey the same derived binding mechanics that
; FUNC [] would have.  (See notes on its implementation about how that is
; tricky, as it tries to optimize the case of when it's just a DO of a BLOCK!
; with no need for relativization.)  At time of writing there is no arity-1
; METHOD-analogue to DOES.
(
    o1: make object! [
        a: 10
        b: bind (does [if true [a]]) binding of 'b
    ]
    o2: make o1 [a: 20]
    o2.b = 20
)(
    o1: make object! [
        a: 10
        b: bind (does [f: does [a] f]) binding of 'b
    ]
    o2: make o1 [a: 20]

    o2.b = 20
)(
    o1: make object! [
        a: 10
        b: bind (does [f: lambda [] [a] f]) binding of 'b
        bind :b binding of 'b
    ]
    o2: make o1 [a: 20]

    o2.b = 20
)(
    o1: make object! [
        a: 10
        b: meth [] [f: does [a], return f]
    ]
    o2: make o1 [a: 20]

    o2.b = 20
)
