; PREDICATE TESTS
;
; Generic tests of predicate abilities (errors, etc.)

(
    e: sys.util/recover [until:predicate ["a"] cascade [even?/ not/]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)

(
    e: sys.util/recover [until:predicate ["a"] cascade [even?/ not/]]
    all [
        e.id = 'expect-arg
        e.arg1 = 'even?
    ]
)
