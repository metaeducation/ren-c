(
    unset 'x
    x: default [10]
    x = 10
)
(
    x: _
    x: default [10]
    x = 10
)
(
    x: 20
    x: default [10]
    x = 20
)
(
    o: make object! [x: 10 y: _ z: null]
    o/x: default [20]
    o/y: default [20]
    o/z: default [20]
    [10 20 20] = reduce [o/x o/y o/z]
)
(
    o: make object! [x: 10 y: _ z: null]
    o.x: default [20]
    o.y: default [20]
    o.z: default [20]
    [10 20 20] = reduce [o.x o.y o.z]
)

; STEAL tests
(
    x: 10
    all [
        10 = steal x: 20
        x = 20
    ]
)
(
    x: _
    all [
        _ = steal x: default [20]
        x = 20
    ]
)

; Predicates allow the specification of an additional constraint, which if
; not met, will also lead to defaulting.
(
    x: "not an integer"
    x: default .integer? [10 + 20]
    x = 30
)(
    x: 304
    x: default .integer? [10 + 20]
    x = 304
)

; Only unfriendly bad-words count, and only ~void~ and ~unset~ are candidates
; that the system knows to go with the idea of "missing variables".
[(
    x: ~unset~
    x: default [1020]
    x = 1020
)(
    x: second [~void~ ~unset~]
    x: default [1020]
    x = '~unset~
)(
    x: ~problem-signal~
    x: default [1020]
    ^x = '~problem-signal~
)]
