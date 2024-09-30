(
    x: ~
    x: default [10]
    x = 10
)
(
    x: '_  ; blank is no longer considered empty
    x: default [10]
    x = '_
)
(
    x: 20
    x: default [10]
    x = 20
)
(
    o: make object! [x: 10 y: null z: null]
    o.x: default [20]
    o.y: default [20]
    o.z: default [20]
    [10 20 20] = reduce [o.x o.y o.z]
)
(
    o: make object! [x: 10 y: null z: null]
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
    x: '_
    all [
        '_ = steal x: default [20]  ; default considers BLANK! a value
        x = '_
    ]
)

; Predicates allow the specification of an additional constraint, which if
; not met, will also lead to defaulting.  (SHOVE temporarily needed for
; running refined path, won't be with CHAIN!)
(
    x: "not an integer"
    x: ->- default/predicate [10 + 20] :integer?
    x = 30
)(
    x: 304
    x: ->- default/predicate [10 + 20] :integer?
    x = 304
)

; Antiform `~` specifically means "unset variable".
[(
    x: ~
    x: default [1020]
    x = 1020
)(
    x: second [~()~ ~]  ; quasiform of void
    x: default [1020]
    x = '~
)(
    x: ~problem-signal~
    x: default [1020]
    ^x = '~problem-signal~
)]
