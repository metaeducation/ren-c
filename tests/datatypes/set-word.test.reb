; datatypes/set-word.r
(set-word? first [a:])
(not set-word? 1)
(set-word! = type of first [a:])
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
    a: action!
    equal? :a action!
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
    did all [
        set-path? set-slash
        '/: = set-slash
    ]
)]

[https://github.com/metaeducation/ren-c/issues/876 (
    x: 1020
    1020 = (null, x: (void))
)(
    x: 1020
    did all [
        2 = (x: comment "Hi" 2)
        x = 1020
    ]
)(
    bad-word? ^ x: lib.print "Hi"  ; generally you shouldn't use PRINT
)]
