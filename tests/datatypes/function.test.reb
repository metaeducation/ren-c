; datatypes/function.r
(action? does ["OK"])
(not action? 1)
(action! = type of does ["OK"])
; minimum
(action? does [])
; literal form
(action? first [#[action! [[] []]]])
; return-less return value tests
(
    f: does []
    void? f
)
(
    f: does [:abs]
    :abs = f
)
(
    a-value: #{}
    f: does [a-value]
    same? a-value f
)
(
    a-value: charset ""
    f: does [a-value]
    same? a-value f
)
(
    a-value: []
    f: does [a-value]
    same? a-value f
)
(
    a-value: blank!
    f: does [a-value]
    same? a-value f
)
(
    f: does [1/Jan/0000]
    1/Jan/0000 = f
)
(
    f: does [0.0]
    0.0 == f
)
(
    f: does [1.0]
    1.0 == f
)
(
    a-value: me@here.com
    f: does [a-value]
    same? a-value f
)
(
    f: does [trap [1 / 0]]
    error? f
)
(
    a-value: %""
    f: does [a-value]
    same? a-value f
)
(
    a-value: does []
    f: does [:a-value]
    same? :a-value f
)
(
    a-value: first [:a]
    f: does [:a-value]
    (same? :a-value f) and (:a-value == f)
)
(
    f: does [#"^M"]
    #"^M" == f
)
(
    a-value: make image! 0x0
    f: does [a-value]
    same? a-value f
)
(
    f: does [0]
    0 == f
)
(
    f: does [1]
    1 == f
)
(
    f: does [#a]
    #a == f
)
(
    a-value: first ['a/b]
    f: does [:a-value]
    :a-value == f
)
(
    a-value: first ['a]
    f: does [:a-value]
    :a-value == f
)
(
    f: does [true]
    true = f
)
(
    f: does [false]
    false = f
)
(
    f: does [$1]
    $1 == f
)
(
    f: does [:append]
    same? :append f
)
(
    f: does [_]
    blank? f
)
(
    a-value: make object! []
    f: does [:a-value]
    same? :a-value f
)
(
    a-value: first [()]
    f: does [:a-value]
    same? :a-value f
)
(
    f: does [get '+]
    same? get '+ f
)
(
    f: does [0x0]
    0x0 == f
)
(
    a-value: 'a/b
    f: does [:a-value]
    :a-value == f
)
(
    a-value: make port! http://
    f: does [:a-value]
    port? f
)
(
    f: does [/a]
    /a == f
)
(
    a-value: first [a/b:]
    f: does [:a-value]
    :a-value == f
)
(
    a-value: first [a:]
    f: does [:a-value]
    :a-value == all [:a-value]
)
(
    a-value: ""
    f: does [:a-value]
    same? :a-value f
)
(
    a-value: make tag! ""
    f: does [:a-value]
    same? :a-value f
)
(
    f: does [0:00]
    0:00 == f
)
(
    f: does [0.0.0]
    0.0.0 == f
)
(
    f: does [()]
    void? f
)
(
    f: does ['a]
    'a == f
)
; two-function return tests
(
    g: func [f [action!]] [f [return 1] 2]
    1 = g :do
)
; BREAK out of a function
(
    null? loop 1 [
        f: does [break]
        f
        2
    ]
)
; THROW out of a function
(
    1 = catch [
        f: does [throw 1]
        f
        2
    ]
)
; "error out" of a function
(
    error? trap [
        f: does [1 / 0 2]
        f
        2
    ]
)
; BREAK out leaves a "running" function in a "clean" state
(
    1 = loop 1 [
        f: func [x] [
            either x = 1 [
                loop 1 [f 2]
                x
            ] [break]
        ]
        f 1
    ]
)
; THROW out leaves a "running" function in a "clean" state
(
    did all [
        null? catch [
            f: func [x] [
                either x = 1 [
                    catch [f 2]
                    x
                ] [throw 1]
            ]
            result: f 1
        ]
        result = 1
    ]
)

; "error out" leaves a "running" function in a "clean" state
(
    f: func [x] [
        either x = 1 [
            error? trap [f 2]
            x = 1
        ] [1 / 0]
    ]
    f 1
)

; Argument passing of "get arguments" ("get-args")
[
    (
        getf: func [:x] [:x]
        true
    )

    (10 == getf 10)
    ('a == getf a)
    (lit 'a == getf 'a)
    (lit :a == getf :a)
    (lit a: == getf a:)
    (lit (10 + 20) == getf (10 + 20))
    (
        o: context [f: 10]
        lit :o/f == getf :o/f
    )
]

; Argument passing of "literal arguments" ("lit-args")
[
    (
        litf: func ['x] [:x]
        true
    )

    (10 == litf 10)
    ('a == litf a)
    (lit 'a == litf 'a)
    (a: 10 | 10 == litf :a)
    (lit a: == litf a:)
    (30 == litf :(10 + 20))
    (
        o: context [f: 10]
        10 == litf :o/f
    )
]

; basic test for recursive action! invocation
(
    i: 0
    countdown: func [n] [if n > 0 [i: i + 1 | countdown n - 1]]
    countdown 10
    i = 10
)

; In Ren-C's specific binding, a function-local word that escapes the
; function's extent cannot be used when re-entering the same function later
(
    f: func [code value] [either blank? code ['value] [do code]]
    f-value: f blank blank
    error? trap [f compose [2 * (f-value)] 21]  ; re-entering same function
)
(
    f: func [code value] [either blank? code ['value] [do code]]
    g: func [code value] [either blank? code ['value] [do code]]
    f-value: f blank blank
    error? trap [g compose [2 * (f-value)] 21]  ; re-entering different function
)
[#19 ; but duplicate specializations currently not legal in Ren-C
    (
    f: func [/r [integer!]] [x]
    error? trap [2 == f/r/r 1 2]
    )
]
[#27
    (error? trap [(type of) 1])
]
; inline function test
[#1659 (
    f: does :(reduce [does [true]])
    f
)]

; Second time f is called, `a` has been cleared so `a [d]` doesn't recapture
; the local, and `c` holds the `[d]` from the first call.  This succeeds in
; R3-Alpha for a different reason than it succeeds in Ren-C; Ren-C has
; closure semantics for functions so the c: [d] where d is 1 survives.
; R3-Alpha recycles variables based on stack searching (non-specific binding).
(
    a: func [b] [
        a: _  comment "erases a so only first call saves c"
        c: b
    ]
    f: func [d] [
        a [d]
        do c
    ]
    did all [
        1 = f 1
        1 = f 2
    ]
)
[#1528
    (action? func [self] [])
]
[#1756
    (reeval does [reduce reduce [:self] true])
]
[#2025 (
    ; ensure x and y are unset from previous tests, as the test here
    ; is trying to cause an error...
    unset 'x
    unset 'y

    body: [x + y]
    f: make action! reduce [[x] body]
    g: make action! reduce [[y] body]
    error? trap [f 1]
)]
[#2044 (
    o: make object! [f: func [x] ['x]]
    p: make o []
    not same? o/f 1 p/f 1
)]

(
    o1: make object! [x: {x} o2: make object! [y: {y}]]
    outer: {outer}
    n: 20

    f: function [
        /count [integer!]
        <in> o1 o1/o2
        <with> outer
        <static> static (10 + n)
    ][
        count: default [2]
        data: reduce [count x y outer static]
        return case [
            count = 0 [reduce [data]]
            true [
               append/only (f/count count - 1) data
            ]
        ]
    ]

    f = [
        [0 "x" "y" "outer" 30]
        [1 "x" "y" "outer" 30]
        [2 "x" "y" "outer" 30]
    ]
)

; Duplicate arguments or refinements.
(
    error? trap [func [a b a] []]
)
(
    error? trap [function [a b a] []]
)
(
    error? trap [func [/test /test] []]
)
(
    error? trap [function [/test /test] []]
)

; /LOCAL is an ordinary refinement in Ren-C
(
    a-value: func [/local [integer!]] [local]
    1 == a-value/local 1
)

[#539 https://github.com/metaeducation/ren-c/issues/755 (
    f: func [return: <void>] [
        use [x] [return]
        42
    ]
    void? f
)]
