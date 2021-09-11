; PREDICATE TESTS
;
; Generic tests of predicate abilities (errors, etc.)

(
    e: trap [until/predicate ["a"] chain [:even? | :not]]
    did all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)

(
    e: trap [until/predicate ["a"] chain [:even? | :not]]
    did all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)
