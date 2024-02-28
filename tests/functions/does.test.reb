; Note that DOES in Ren-C differs from R3-Alpha's DOES:
;
; * Unlike FUNC [] [...], the DOES [...] has no RETURN
; * Blocks you pass to it are LOCKed (if they weren't already)
;


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
