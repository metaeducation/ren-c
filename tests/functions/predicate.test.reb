; PREDICATE TESTS
;
; Generic tests of predicate abilities (errors, etc.)

(
    e: trap [until .not.even? ["a"]]
    did all [
        e/id = 'expect-arg
        e/arg1 = 'even?
    ]
)

(
    e: trap [until .even?.not ["a"]]
    did all [
        e/id = 'expect-arg
        e/arg1 = 'even?
    ]
)
