; Note that DOES in Ren-C differs from R3-Alpha's DOES:
;
; * Unlike FUNC [] [...], the DOES [...] has no RETURN
; * It soft-quotes its argument
; * For types like FILE! / URL! / STRING! it will act as DO when called
; * It can be used to specialize functions other than DO
; * Blocks you pass to it are LOCKed (if they weren't already)
;

(
    three: does "1 + 2"
    3 = three
)

(
    make-x: does lit x
    make-x = 'x
)

; DOES as specialization of APPEND/ONLY: captures block at DOES time
(
    backup: block: copy [a b]
    f: does append/only block [c d]
    f
    block: copy [x y]
    f
    did all [
        backup = [a b [c d] [c d]]
        block = [x y]
    ]
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

(
    x: 10
    y: 20
    flag: true
    z: does all [x: x + 1, flag, y: y + 2, <finish>]
    did all [
        z = <finish>, x = 11, y = 22
        elide (flag: false)
        z = null, x = 12, y = 22
    ]
)

(
    catcher: does catch [throw 10]
    catcher = 10
)

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
    o2/b = 20
)(
    o1: make object! [
        a: 10
        b: bind (does [f: does [a] f]) binding of 'b
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)(
    o1: make object! [
        a: 10
        b: bind (does [f: func [] [a] f]) binding of 'b
        bind :b binding of 'b
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)(
    o1: make object! [
        a: 10
        b: method [] [f: does [a] f]
    ]
    o2: make o1 [a: 20]

    o2/b = 20
)
