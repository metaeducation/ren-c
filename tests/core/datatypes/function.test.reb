; datatypes/function.r
[function? does ["OK"]]
[not function? 1]
[function! = type-of does ["OK"]]
; minimum
[function? does []]
; literal form
[function? first [#[function! [[] []]]]]
; return-less return value tests
[
    f: does []
    void? f
]
[
    f: does [:abs]
    :abs = f
]
[
    a-value: #{}
    f: does [a-value]
    same? a-value f
]
[
    a-value: charset ""
    f: does [a-value]
    same? a-value f
]
[
    a-value: []
    f: does [a-value]
    same? a-value f
]
[
    a-value: blank!
    f: does [a-value]
    same? a-value f
]
[
    f: does [1/Jan/0000]
    1/Jan/0000 = f
]
[
    f: does [0.0]
    0.0 == f
]
[
    f: does [1.0]
    1.0 == f
]
[
    a-value: me@here.com
    f: does [a-value]
    same? a-value f
]
[
    f: does [try [1 / 0]]
    error? f
]
[
    a-value: %""
    f: does [a-value]
    same? a-value f
]
[
    a-value: does []
    f: does [:a-value]
    same? :a-value f
]
[
    a-value: first [:a]
    f: does [:a-value]
    (same? :a-value f) and* (:a-value == f)
]
[
    f: does [#"^@"]
    #"^@" == f
]
[
    a-value: make image! 0x0
    f: does [a-value]
    same? a-value f
]
[
    f: does [0]
    0 == f
]
[
    f: does [1]
    1 == f
]
[
    f: does [#a]
    #a == f
]
[
    a-value: first ['a/b]
    f: does [:a-value]
    :a-value == f
]
[
    a-value: first ['a]
    f: does [:a-value]
    :a-value == f
]
[
    f: does [true]
    true = f
]
[
    f: does [false]
    false = f
]
[
    f: does [$1]
    $1 == f
]
[
    f: does [:type-of]
    same? :type-of f
]
[
    f: does [_]
    blank? f
]
[
    a-value: make object! []
    f: does [:a-value]
    same? :a-value f
]
[
    a-value: first [()]
    f: does [:a-value]
    same? :a-value f
]
[
    f: does [get '+]
    same? get '+ f
]
[
    f: does [0x0]
    0x0 == f
]
[
    a-value: 'a/b
    f: does [:a-value]
    :a-value == f
]
[
    a-value: make port! http://
    f: does [:a-value]
    port? f
]
[
    f: does [/a]
    /a == f
]
[
    a-value: first [a/b:]
    f: does [:a-value]
    :a-value == f
]
[
    a-value: first [a:]
    f: does [:a-value]
    :a-value == all [:a-value]
]
[
    a-value: ""
    f: does [:a-value]
    same? :a-value f
]
[
    a-value: make tag! ""
    f: does [:a-value]
    same? :a-value f
]
[
    f: does [0:00]
    0:00 == f
]
[
    f: does [0.0.0]
    0.0.0 == f
]
[
    f: does [()]
    void? f
]
[
    f: does ['a]
    'a == f
]
; two-function return tests
[
    g: func [f [function!]] [f [return 1] 2]
    1 = g :do
]
; BREAK out of a function
[
    1 = loop 1 [
        f: does [break/return 1]
        f
        2
    ]
]
; THROW out of a function
[
    1 = catch [
        f: does [throw 1]
        f
        2
    ]
]
; "error out" of a function
[
    error? try [
        f: does [1 / 0 2]
        f
        2
    ]
]
; BREAK out leaves a "running" function in a "clean" state
[
    1 = loop 1 [
        f: func [x] [
            either x = 1 [
                loop 1 [f 2]
                x
            ] [break/return 1]
        ]
        f 1
    ]
]
; THROW out leaves a "running" function in a "clean" state
[
    1 = catch [
        f: func [x] [
            either x = 1 [
                catch [f 2]
                x
            ] [throw 1]
        ]
        f 1
    ]
]
; "error out" leaves a "running" function in a "clean" state
[
    f: func [x] [
        either x = 1 [
            error? try [f 2]
            x = 1
        ] [1 / 0]
    ]
    f 1
]
; Argument passing of "get arguments" ("get-args")
[gf: func [:x] [:x] 10 == gf 10]
[gf: func [:x] [:x] 'a == gf a]
[gf: func [:x] [:x] (quote 'a) == gf 'a]
[gf: func [:x] [:x] (quote :a) == gf :a]
[gf: func [:x] [:x] (quote a:) == gf a:]
[gf: func [:x] [:x] (quote (10 + 20)) == gf (10 + 20)]
[gf: func [:x] [:x] o: context [f: 10] (quote :o/f) == gf :o/f]
; Argument passing of "literal arguments" ("lit-args")
[lf: func ['x] [:x] 10 == lf 10]
[lf: func ['x] [:x] 'a == lf a]
[lf: func ['x] [:x] (quote 'a) == lf 'a]
[lf: func ['x] [:x] a: 10 10 == lf :a]
[lf: func ['x] [:x] (quote a:) == lf a:]
[lf: func ['x] [:x] 30 == lf (10 + 20)]
[lf: func ['x] [:x] o: context [f: 10] 10 == lf :o/f]
; basic test for recursive function! invocation
[i: 0 countdown: func [n] [if n > 0 [++ i countdown n - 1]] countdown 10 i = 10]

; In Ren-C's specific binding, a function-local word that escapes the
; function's extent cannot be used when re-entering the same function later
[
    f: func [code value] [either blank? code ['value] [do code]]
    f-value: f blank blank
    error? try [f compose [2 * (f-value)] 21]  ; re-entering same function
]
[
    f: func [code value] [either blank? code ['value] [do code]]
    g: func [code value] [either blank? code ['value] [do code]]
    f-value: f blank blank
    error? try [g compose [2 * (f-value)] 21]  ; re-entering different function
]
; bug#19 - but duplicate specializations currently not legal in Ren-C
[
    f: func [/r x] [x]
    error? trap [2 == f/r/r 1 2]
]
; bug#27
[error? try [(type-of) 1]]
; bug#1659
; inline function test
[
    f: does reduce [does [true]]
    f
]
; no-rebind test--succeeds in R3-Alpha but fails in Ren-C.  Second time f is
; called, `a` has been cleared so `a [d]` doesn't recapture the local, and
; `c` holds the `[d]` from the first call.
[
    a: func [b] [a: _ c: b]
    f: func [d] [a [d] do c]
    all? [
        1 = f 1
        error? try [2 = f 2]
    ]
]
; bug#1528
[function? func [self] []]
; bug#1756
[eval does [reduce reduce [:self] true]]
; bug#2025
[
    ; ensure x and y are unset from previous tests, as the test here
    ; is trying to cause an error...
    unset 'x
    unset 'y

    body: [x + y]
    f: make function! reduce [[x] body]
    g: make function! reduce [[y] body]
    error? try [f 1]
]
; bug#2044
[
    o: make object! [f: func [x] ['x]]
    p: make o []
    not same? o/f 1 p/f 1
]
