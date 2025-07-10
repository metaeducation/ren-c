; %trap.test.r
;
; TRAP is a tool that's like the ? operator in Rust, which passes through a
; successful result, but propagates an ERROR! to whatever the definition of
; RETURN is in the current scope.

(
    trapless: func [] [
        let x: 1 * 0
        let y: 1 / 0
        return x + y
    ]

    trappy: func [] [
        let x: trap 1 * 0
        let y: trap 1 / 0
        return x + y
    ]

    all [
        'zero-divide = (sys.util/recover [
            (rescue [trapless]).id = 'zero-divide
        ]).id  ; hard panic, recover had to step in

        (rescue [trappy]).id = 'zero-divide  ; propagated!
    ]
)
