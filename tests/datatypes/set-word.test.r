; datatypes/set-word.r
(set-word? first [a:])
(not set-word? 1)
(chain! = type of first [a:])
; set-word is active
(
    a: abs/
    equal? a/ abs/
)
(
    a: #{}
    equal? a #{}
)
(
    a: charset ""
    equal? a charset ""
)
(
    a: []
    equal? a []
)
(
    a: frame!
    equal? a frame!
)
[#1817 (
    a: to map! []
    a.b: make object! [
        c: to map! []
    ]
    integer? a.b.c.d: 1
)]

[#1477 (
    set-slash: transcode:one "/:"
    all [
        set-word? set-slash
        '/: = set-slash
    ]
)]

[https://github.com/metaeducation/ren-c/issues/876 (
    x: 1020
    all [
        void? (null, x: (^void))
        void? x
    ]
)
~no-value~ !! (
    x: 1020
    all [
        2 = (x: comment "Hi" 2)
        unset? $x
    ]
)]

(
    name: 10
    all [
        20 = set:groups $($name) 20
        20 = name
    ]
)
(
    obj: make object! [field: ~]
    all [
        '~['10 '20]~ = lift set:groups $($obj.field) pack [10 20]
        (the '10) = lift ^obj.field
    ]
)
(
    obj: make object! [field: ~]
    all [
        '~['10 '20]~ = lift set:groups $(meta $obj.field) pack [10 20]
        '~['10 '20]~ = lift ^obj.field
    ]
)
~???~ !! (
    name: 10
    code: $($($name))
    set:groups $(code) 1020
)
(null? set:groups $() 100)
(null? set:groups () pack [10 20])
(null? set:groups ^void ~)
(null? set:groups (^void) ())
