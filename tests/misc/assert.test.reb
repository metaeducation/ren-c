; CLASSIC ASSERT
; Ren-C's version is invisible, like a COMMENT

(
    null? e: trap [assert [1 = 1]]
)(
    e: trap [assert [1 = 2]]
    e.id = 'assertion-failure
)(
    null? e: trap [assert [1 = 1, 2 = 2]]
)(
    e: trap [assert [1 = 1, 304 = 1020]]
    e.id = 'assertion-failure
)(
    10 = all [
        5 + 5
        assert [1 = 1]
    ]
)


; SO POSTFIX
; Checks a condition as logically true/false, asserts if false, evaluates to
; next value given

(
    '~none~ = ^ (1 = 1 so)
)(
    e: trap [1 = 2 so]
    e.id = 'assertion-failure
)(
    x: even? 4 so 10 * 20
    x = 200
)


; WAS POSTFIX
; Checks for "IS-ness" of left to the right

(
    20 = (10 + 10 was 20)
)(
    e: trap [10 + 10 was 30]
    e.id = 'assertion-failure
)(
    40 = (10 + 30 was 20 + 20)
)(
    e: trap [(10 + 20) was 20 + 20]
    e.id = 'assertion-failure
)


; Invisibles
[
    (assert [] true)
    (assert [comment "hi" 1] true)
    (assert [1 elide 2 + 3] true)
    (assert [comment "hi" (true)] true)
    (assert [(true) elide 2 + 3] true)
]


; Custom handler, can request to ~ignore~ the assert, otherwise fails as usual
[
    (
        [[1 = 2] [2 = 3]] = collect [
            assert/handler [1 = 2, 2 = 2, 2 = 3] func [x] [keep ^x, ~ignore~]
        ]
    )
    (
        did all [
            ["hooked"] = collect [e: trap [
                assert/handler [1 = 2, 2 = 2, 2 = 3] [keep "hooked"]
            ]]
            e.id = 'assertion-failure
        ]
    )
]
