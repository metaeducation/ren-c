; datatypes/set-word.r
(set-word? first [a:])
(not set-word? 1)
(set-word! = kind of first [a:])
; set-word is active
(
    a: :abs
    equal? :a :abs
)
(
    a: #{}
    equal? :a #{}
)
(
    a: charset ""
    equal? :a charset ""
)
(
    a: []
    equal? a []
)
(
    a: frame!
    equal? :a frame!
)
[#1817 (
    a: make map! []
    a.b: make object! [
        c: make map! []
    ]
    integer? a.b.c.d: 1
)]

[#1477 (
    set-slash: load-value "/:"
    all [
        set-word? set-slash
        '/: = set-slash
    ]
)]

[https://github.com/metaeducation/ren-c/issues/876 (
    x: 1020
    all [
        void? (null, x: (void))
        void? x
    ]
)
~no-value~ !! (
    x: 1020
    all [
        2 = (x: comment "Hi" 2)
        void? :x
    ]
)]
