; CLASSIC ASSERT
; Ren-C's version is invisible, like a COMMENT

(
    null? e: sys.util/rescue [assert [1 = 1]]
)(
    e: sys.util/rescue [assert [1 = 2]]
    e.id = 'assertion-failure
)(
    null? e: sys.util/rescue [assert [1 = 1, 2 = 2]]
)(
    e: sys.util/rescue [assert [1 = 1, 304 = 1020]]
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
    nothing? (1 = 1 so)
)
~assertion-failure~ !! (
    1 = 2 so
)
(
    x: even? 4 so 10 * 20
    x = 200
)


; WAS POSTFIX
; Checks for "IS-ness" of left to the right
[
    (
        20 = (10 + 10 was 20)
    )
    ~assertion-failure~ !! (
        10 + 10 was 30
    )
    (
        40 = (10 + 30 was 20 + 20)
    )
    ~assertion-failure~ !! (
        (10 + 20) was 20 + 20
    )
]


; Invisibles
[
    (assert [] ok)
    (assert [comment "hi" 1] ok)
    (assert [1 elide 2 + 3] ok)
    (assert [comment "hi" (ok)] ok)
    (assert [(ok) elide 2 + 3] ok)
]


; Custom handler, can request to ~ignore~ the assert, otherwise fails as usual
[
    (
        [[1 = 2] [2 = 3]] = collect [
            assert:handler [1 = 2, 2 = 2, 2 = 3] [x] -> [keep x, ~<ignore>~]
        ]
    )
    (
        e: ~
        all [
            ["hooked"] = collect [e: sys.util/rescue [
                assert:handler [1 = 2, 2 = 2, 2 = 3] [keep "hooked"]
            ]]
            e.id = 'assertion-failure
        ]
    )
]
