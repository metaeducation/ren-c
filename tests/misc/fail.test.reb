; fail.test.reb
;
; The FAIL function in Ren-C is a usermode function that attempts to give
; a richer experience than the `DO MAKE ERROR!` behavior in R3-Alpha.
;
; Because DO of an ERROR! would have to raise an error if it did not have
; another behavior, this is still the mechanism for triggering errors.
; (used by FAIL in its implementation)


; FAIL may be used on its own.  If it didn't consider this a legitimate
; way of raising errors, it would have to raise an error anyway.
; This is convenient for throwaway code.
[
    (e: trap [fail], e.id = 'unknown-error)
    (e: trap [case [false [x] false [y] fail]], e.id = 'unknown-error)
]


; A simple FAIL with a string message will be a generic error ID
;
(e: trap [fail "hello"], (e.id = null) and (e.message = "hello"))


; Failing instead with a WORD! will make the error have that ID
;
(e: trap [fail 'some-error-id], e.id = 'some-error-id)


; FAIL can be given a QUOTED! of a parameter to blame.  This gives a
; more informative message, even when no text is provided.
;
; The QUOTED! is a skippable parameter, and can be used in combination
; with other error reason parameters
[
    (
        foo: func [x] [fail @x]

        e: trap [foo 10]
        did all [
            e.id = 'invalid-arg
            e.arg1 = 'foo
            e.arg2 = 'x
            e.arg3 = 10
            [foo 10] = copy/part e.near 2  ; implicates callsite
        ]
    )(
        foo: func [x] [fail @x "error reason"]

        e: trap [foo 10]
        did all [
            e.id = null  ; no longer an invalid arg error
            [foo 10] = copy/part e.near 2  ; still implicates callsite
        ]
    )
]
