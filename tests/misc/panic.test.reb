; panic.test.reb
;
; The PANIC function in Ren-C is a usermode function that attempts to give
; a richer experience than the `DO MAKE ERROR!` behavior in R3-Alpha.


; PANIC may be used on its own.  If it didn't consider this a legitimate
; way of panicking, it would have to panic with some error anyway.
; This is convenient for throwaway code.
[
    ~unknown-error~ !! (panic)
    ~unknown-error~ !! (case [null [x] null [y] panic])
]


; A simple PANIC with a string message has no error ID
;
(e: sys.util/rescue [panic "hello"], (e.id = null) and (e.message = "hello"))


; PANIC instead with a WORD! will make the error have that ID
;
(e: sys.util/rescue [panic 'some-error-id], e.id = 'some-error-id)


; PANIC can be given a :BLAME parameter.  It gives a more informative message,
; even when no text is provided.
[
    (
        foo: func [x] [panic @x]

        e: sys.util/rescue [foo 10]
        all [
            e.id = 'invalid-arg
            e.arg1 = 'foo
            e.arg2 = 'x
            e.arg3 = 10
            [foo 10] = copy:part e.near 2  ; implicates callsite
        ]
    )(
        foo: func [x] [panic:blame "error reason" $x]

        e: sys.util/rescue [foo 10]
        all [
            e.id = null  ; no longer an invalid arg error
            [foo 10] = copy:part e.near 2  ; still implicates callsite
        ]
    )
]

; Multi-returns which aren't writing to a variable should be willing to
; tolerate an arbitrary result coming out.
[
    (error? [{_}]: fail "hi")
]

; A ^META'd error! still does a lookahead step for infix, and if that step
; does not need to lookahead it should respect the meta'd status and not
; raise an error.
[
    (
        x': ~
        all [
            (error? unmeta [x']: meta fail "hi" void)
            error? unmeta x'
        ]
    )
]

; Non-^META cases should be able to get away with fail + except if it doesn't
; actually try to do the assignment.
[
    (null? insist [x: fail "hi" except [break]])
    (null? insist [[x]: fail "hi" except [break]])
    (
        x: ~
        all [
            'true = insist [x: fail "hi" except ['true]]
            x = 'true
        ]
    )
    (
        x: ~
            all [
            'true = insist [[x]: fail "hi" except ['true]]
            x = 'true
        ]
    )

    (e: 1020, all [(trap [e: fail "hi"]).message = "hi", e = 1020])
    (e: 1020, all [(trap [[e]: fail "hi"]).message = "hi", e = 1020])
]
