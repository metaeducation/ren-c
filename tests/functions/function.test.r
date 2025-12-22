; datatypes/function.r
(action? does ["OK"])
(not frame? 1)
(frame! = type of unrun does ["OK"])
; minimum
(frame? unrun does [])

; !!! literal form no longer supported
;
~???~ !! (load "&[action! [[] []]]")

; return-less return value tests
(
    f: does [null]
    null? f
)
(
    f: does [abs/]
    abs/ = f
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
    a-value: integer!
    f: does [a-value]
    same? a-value f
)
(
    f: does [1/Jan/0000]
    1/Jan/0000 = f
)
(
    f: does [0.0]
    0.0 = f
)
(
    f: does [1.0]
    1.0 = f
)
(
    a-value: me@here.com
    f: does [a-value]
    same? a-value f
)
(
    f: does [rescue [1 / 0]]
    warning? f
)
(
    a-value: %""
    f: does [a-value]
    same? a-value f
)
(
    a-value: does []
    f: does [a-value/]
    same? a-value/ f
)
(
    a-value: first [:a]
    f: does [a-value]
    (same? a-value f) and (a-value = f)
)
(
    f: does [#"^M"]
    #"^M" = f
)
(
    f: does [0]
    0 = f
)
(
    f: does [1]
    1 = f
)
(
    f: does [#a]
    #a = f
)
(
    a-value: first ['a/b]
    f: does [a-value]
    a-value = f
)
(
    a-value: first ['a]
    f: does [a-value]
    a-value = f
)
(
    f: does ['true]
    'true = f
)
(
    f: does ['false]
    'false = f
)
(
    f: does [$1]
    $1 = f
)
(
    f: does [append/]
    same? append/ f
)
(
    f: does [_]
    space? f
)
(
    a-value: make object! []
    f: does [a-value]
    same? a-value f
)
(
    a-value: first [()]
    f: does [a-value]
    same? a-value f
)
(
    f: does [+/]
    same? +/ f
)
(
    f: does [0x0]
    0x0 = f
)
(
    a-value: 'a/b
    f: does [a-value]
    a-value = f
)
(
    a-value: make port! http://
    f: does [a-value]
    port? f
)
(
    f: does ['/a]
    '/a = f
)
(
    a-value: first [a.b:]
    f: does [a-value]
    a-value = f
)
(
    a-value: first [a:]
    f: does [a-value]
    a-value = all [a-value]
)
(
    a-value: ""
    f: does [a-value]
    same? a-value f
)
(
    a-value: to tag! ""
    f: does [a-value]
    same? a-value f
)
(
    f: does [0:00]
    0:00 = f
)
(
    f: does [0.0.0]
    0.0.0 = f
)
(
    f: does [()]
    (lift ^ghost) = lift f
)
(
    f: does ['a]
    'a = f
)
; two-function return tests
(
    g: func [return: [integer!] f [action!]] [f [return 1] 2]
    1 = g eval/
)
; BREAK out of a function
(
    null? repeat 1 [
        let f: does [break]
        f
        2
    ]
)
; THROW out of a function
(
    1 = catch [
        let f: does [throw 1]
        f
        2
    ]
)

~zero-divide~ !! (
    warning? rescue [
        let f: does [1 / 0 2]  ; "error out" of a function
        f
        2
    ]
)

; The BREAK designates breaking the outer repeat (definitional BREAK)
(
    null = repeat 1 wrap [
        f: lambda [x] [
            either x = 1 [
                repeat 1 [f 2]
                x
            ] [break]
        ]
        f 1
    ]
)

(
    result: <before>
    all [
        2 = catch wrap [  ; outer catch
            f: lambda [x] [
                either x = 1 [
                    catch [f 2]  ; inner catch--no throws in block
                    x
                ] [throw 2]  ; definitional throw, only matches outer catch
            ]
            result: f 1  ; never returns due ot outer catch
        ]
        result = <before>
    ]
)

; "error out" leaves a "running" function in a "clean" state
(
    f: lambda [x] [
        either x = 1 [
            warning? rescue [f 2]
            x = 1
        ] [1 / 0]
    ]
    f 1
)

; Argument passing of "hard literal arguments"
[
    (
        hard: func ['x] [return x]
        ok
    )

    (10 = hard 10)
    ('a = hard a)
    (the 'a = hard 'a)
    (the :a = hard :a)
    (the a: = hard a:)
    (the (10 + 20) = hard (10 + 20))
    (
        o: context [f: 10]
        (the :o.f) = hard :o.f
    )
]

; Argument passing of "escapable (soft) literal arguments"
[
    (
        got: null

        soft: func [@(x)] [got: ^x, return 1000]
        Lsoft: infix soft/

        test: lambda [expr [block!]] [
            got: '~junk~
            compose [(eval expr), (^got)]
        ]
        ok
    )

    ([1000, 1] = test [soft 1])
    ([1000, a] = test [soft a])
    ([1000, 'a] = test [soft 'a])
    ([1000, 304] = test [soft (300 + 4)])
    ([1000, o.f] = test [soft o.f])
    (
        o: context [f: 304]
        [1000, 304] = test [soft (o.f)]
    )

    (
        +Q: infix lambda ['x [<end> integer!] y] [if x [x + y] else [<null>]]
        [1000, 30] = test [soft 10 +Q 20]
    )
    (
        +Q: infix lambda ['x y] [x + y]
        [1000, 30] = test [soft 10 +Q 20]
    )

    ([1001, 2] = test [1 + 2 Lsoft])
    ([1001, <hi>] = test [1 + (first [<hi>]) Lsoft])
]

; basic test for recursive action invocation
(
    i: 0
    countdown: lambda [n] [if n > 0 [i: i + 1, countdown n - 1]]
    countdown 10
    i = 10
)

; In Ren-C's specific binding, a function-local word that escapes the
; function's extent cannot be used when re-entering the same function later
;
; !!! Review: Fix these tests.

~expect-arg~ !! (
    f: func [code value] [return either space? code [$value] [eval code]]
    f-value: f space space
    f compose [2 * (f-value)] 21  ; re-entering same function
)
~expect-arg~ !! (
    f: func [code value] [return either space? code [$value] [eval code]]
    g: func [code value] [return either space? code [$value] [eval code]]
    f-value: f space space
    g compose [2 * (f-value)] 21  ; re-entering different function
)

[#19
    ~bad-parameter~ !! (
        f: func [:r [integer!]] [return x]
        2 = f:r:r 1 2  ; Review: could be a syntax for variadic refinements?
    )
]

[#27
    ~not-bound~ !! (
        warning? rescue [(kind of) 1]
    )
]

; inline function test
[#1659 (
    f: does (reduce [unrun does [okay]])
    f
)]

; Second time f is called, `a` has been cleared so `a [d]` doesn't recapture
; the local, and `c` holds the `[d]` from the first call.  This succeeds in
; R3-Alpha for a different reason than it succeeds in Ren-C; Ren-C has
; closure semantics for functions so the c: [d] where d is 1 survives.
; R3-Alpha recycles variables based on stack searching (non-specific binding).
(
    c: ~
    a: lambda [b] [
        a: null  ; erases a so only first call saves c
        c: b
    ]
    f: lambda [d] [
        a [d]
        eval c
    ]
    all [
        1 = f 1
        1 = f 2
    ]
)
[#2025
    ~not-bound~ !! (
        assert [undefined? $x, undefined? $y]

        body: [return x + y]
        f: func [x] body
        g: func [y] body
        (f 1)
    )
]

[#2044 (
    o: make object! [f: func [x] [return $x]]
    p: make o []
    not same? o/f 1 p/f 1
)]

(
    o1: make object! [x: "x" o2: make object! [y: "y"]]
    outer: "outer"
    n: 20

    f: func [
        :count [integer!]
    ]
    bind o1 bind o2 bind {
        static: 10 + n
    }[
        count: default [2]
        let data: reduce [count x y outer static]
        return case [
            count = 0 [reduce [data]]
            <default> [
               append (f:count count - 1) data
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
[
    ~dup-vars~ !! (func [a b a] [return 0])
    ~dup-vars~ !! (lambda [a b a] [return 0])
    ~dup-vars~ !! (func [:test :test] [return 0])
    ~dup-vars~ !! (lambda [:test :test] [return 0])
]

; :LOCAL is an ordinary refinement in Ren-C
(
    a-value: func [:local [integer!]] [return local]
    1 = a-value:local 1
)

[#539 https://github.com/metaeducation/ren-c/issues/755 (
    f: func [return: ~] [
        use [x] [return ~]
        42
    ]
    (lift ^tripwire) = lift f
)]

(
    foo: lambda [^arg [null? void! <end> void? integer!]] [
        either unset? $arg [<unset>] [lift ^arg]
    ]
    all [
        (the '1020) = (foo 1020)
        '~,~ = (foo comment "HI")
        (lift null) = (foo any [1 > 2, 3 > 4])
        <unset> = (foo)
    ]
)

; Test that undefined types or predicates cause an error and don't crash
[
    ~bad-value~ !! (
        to-the-limit: lambda [everybody [integer! fhqwhgads?]] []
    )
]

; Trash value returned has label of function
(
    some-name: func [return: ~] [return ~]
    #some-name = unanti some-name
)
