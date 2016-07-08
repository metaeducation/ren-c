; datatypes/set-word.r
[set-word? first [a:]]
[not set-word? 1]
[set-word! = type-of first [a:]]
; set-word is active
[
    a: :abs
    equal? :a :abs
]
[
    a: #{}
    equal? :a #{}
]
[
    a: charset ""
    equal? :a charset ""
]
[
    a: []
    equal? a []
]
[
    a: function!
    equal? :a function!
]
; bug#1817
[
    a: make map! []
    a/b: make object! [
        c: make map! []
    ]
    integer? a/b/c/d: 1
]
