; datatypes/closure.r
[closure? closure [] ["OK"]]
[not closure? 1]
[closure! = type-of closure [] ["OK"]]
; minimum
[closure? closure [] []]
; return-less return value tests
[
    f: closure [] []
    void? f
]
[
    f: closure [] [:abs]
    :abs = f
]
[
    a-value: #{}
    f: closure [] [a-value]
    same? a-value f
]
[
    a-value: charset ""
    f: closure [] [a-value]
    same? a-value f
]
[
    a-value: []
    f: closure [] [a-value]
    same? a-value f
]
[
    a-value: blank!
    f: closure [] [a-value]
    same? a-value f
]
[
    f: closure [] [1/Jan/0000]
    1/Jan/0000 = f
]
[
    f: closure [] [0.0]
    0.0 == f
]
[
    f: closure [] [1.0]
    1.0 == f
]
[
    a-value: me@here.com
    f: closure [] [a-value]
    same? a-value f
]
[
    f: closure [] [try [1 / 0]]
    error? f
]
[
    a-value: %""
    f: closure [] [a-value]
    same? a-value f
]
[
    a-value: does []
    f: closure [] [:a-value]
    same? :a-value f
]
[
    a-value: first [:a]
    f: closure [] [:a-value]
    (same? :a-value f) and* (:a-value == f)
]
[
    f: closure [] [#"^@"]
    #"^@" == f
]
[
    a-value: make image! 0x0
    f: closure [] [a-value]
    same? a-value f
]
[
    f: closure [] [0]
    0 == f
]
[
    f: closure [] [1]
    1 == f
]
[
    f: closure [] [#a]
    #a == f
]
[
    a-value: first ['a/b]
    f: closure [] [:a-value]
    :a-value == f
]
[
    a-value: first ['a]
    f: closure [] [:a-value]
    :a-value == f
]
[
    f: closure [] [true]
    true = f
]
[
    f: closure [] [false]
    false = f
]
[
    f: closure [] [$1]
    $1 == f
]
[
    f: closure [] [:type-of]
    same? :type-of f
]
[
    f: closure [] [_]
    blank? f
]
[
    a-value: make object! []
    f: closure [] [:a-value]
    same? :a-value f
]
[
    a-value: first [()]
    f: closure [] [:a-value]
    same? :a-value f
]
[
    f: closure [] [get '+]
    same? get '+ f
]
[
    f: closure [] [0x0]
    0x0 == f
]
[
    a-value: 'a/b
    f: closure [] [:a-value]
    :a-value == f
]
[
    a-value: make port! http://
    f: closure [] [:a-value]
    port? f
]
[
    f: closure [] [/a]
    /a == f
]
[
    a-value: first [a/b:]
    f: closure [] [:a-value]
    :a-value == f
]
[
    a-value: first [a:]
    f: closure [] [:a-value]
    :a-value == all [:a-value]
]
[
    a-value: ""
    f: closure [] [:a-value]
    same? :a-value f
]
[
    a-value: make tag! ""
    f: closure [] [:a-value]
    same? :a-value f
]
[
    f: closure [] [0:00]
    0:00 == f
]
[
    f: closure [] [0.0.0]
    0.0.0 == f
]
[
    f: closure [] [()]
    void? f
]
[
    f: closure [] ['a]
    'a == f
]
; basic test for recursive closure! invocation
[i: 0 countdown: closure [n] [if n > 0 [++ i countdown n - 1]] countdown 10 i = 10]
; bug#21
[
    c: closure [a] [return a]
    1 == c 1
]
; two-function return test
[
    g: closure [f [function!]] [f [return 1] 2]
    1 = g :do
]
; BREAK out of a closure
[
    1 = loop 1 [
        f: closure [] [break/return 1]
        f
        2
    ]
]
; THROW out of a closure
[
    1 = catch [
        f: closure [] [throw 1]
        f
        2
    ]
]
; "error out" of a closure
[
    error? try [
        f: closure [] [1 / 0 2]
        f
        2
    ]
]
; BREAK out leaves a "running" closure in a "clean" state
[
    1 = loop 1 [
        f: closure [x] [
            either x = 1 [
                loop 1 [f 2]
                x
            ] [break/return 1]
        ]
        f 1
    ]
]
; THROW out leaves a "running" closure in a "clean" state
[
    1 = catch [
        f: closure [x] [
            either x = 1 [
                catch [f 2]
                x
            ] [throw 1]
        ]
        f 1
    ]
]
; "error out" leaves a "running" closure in a "clean" state
[
    f: closure [x] [
        either x = 1 [
            error? try [f 2]
            x = 1
        ] [1 / 0]
    ]
    f 1
]
; bug#1659
; inline closure test
[
    f: closure [] reduce [closure [] [true]]
    f
]
; rebind test
[
    a: closure [b] [does [b]]
    b: a 1
    c: a 2
    all [
        1 = b
        2 = c
    ]
]
; bug#447
[slf: 'self eval closure [x] [same? slf 'self] 1]
; bug#1528
[closure? closure [self] []]
[
    f: make closure! reduce [[x] f-body: [x + x]]
    change f-body 'x ;-- makes copies now
    x: 1
    4 == f 2 ; #2048 said this should be 3, but it should not.
    ; function and closure bodies are not "swappable", because keeping the
    ; original series would mean that the original formation would always
    ; drop the index position (there is no index slot in the body series).
    ; A copy must be made -or- series forced to be at their head.
]
