; PREDICATE TESTS
;
; Generic tests of predicate abilities (errors, etc.)

(
    e: sys.util/rescue [until/predicate ["a"] chain [get $even?, get $not]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)

(
    e: sys.util/rescue [until/predicate ["a"] chain [get $even?, get $not]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)
