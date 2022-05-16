# %let.test.reb
#
# LET is a construct which dynamically creates a variable binding and injects
# it into the stream following it.  This makes it a nicer syntax than USE as
# it does not require introducing a new indentation/block level.  However,
# like USE it does have the effect of doing an allocation each time, which
# means it can create a lot of load for the GC, especially in loops:
#
#     count-up x 1000000 [
#         let y: x + 1  ; allocates y each time, hence a million allocs
#


; Because things are currently bound in the user context by default, it can
; be hard to test whether it is adding new bindings or not.
[
    (
       x: <in-user-context>
       did all [
           1020 = do compose [let (unbind 'x:) 20, 1000 + (unbind 'x)]
           x = <in-user-context>
       ]
    )
    (
       x: <in-user-context>
       did all [
           1020 = do compose [let x: 20, 1000 + x]
           x = <in-user-context>
       ]
    )
]

; LET X: 10 form declares but doesn't initialize, and returns
[(
        x: <in-user-context>
        [<in-user-context> 10 10] = reduce [x, let x: 10, get 'x]
)
(
        x: <in-user-context>
        [<in-user-context> x 10 10] = reduce [x, let x, x: 10, get 'x]
)]

; If a LET receives a BLOCK!, then anything that is quoted will be dequoted
; and slipped into the stream to be handled normally.
[(
    saved: _
    leftq: enfixed func ['x] [saved: x]
    let [a 'b ''(c)]: leftq
    saved = just [a b '(c)]:
)(
    leftq: enfixed func ['x] [saved: x]
    saved: let [a 'b ''(c)]
    saved = [a b '(c)]
)]

; The quoting property of LET is used to subvert a LET binding during a
; multiple-return value scenario, allowing you to mix and match variables
; which are getting new bindings with existing bindings.
(
    value: <value>
    pos: <pos>
    result: do [
        let [value 'pos]: transcode "[first item] #residue"
        reduce [value pos]
    ]
    did all [
        result = [[first item] " #residue"]
        pos = " #residue"
        value = <value>
    ]
)

; GROUP!s can either be evaluated on behalf of the LET, or if escaped they
; will be evaluated on behalf of the multi-return.  (At the moment, multi
; return does not support GROUP!s)
(
    value: <value>
    pos: <pos>
    word: 'value
    result: do [
        let [(word) 'pos]: transcode "[first item] #residue"
        reduce [value pos]
    ]
    did all [
        result = [[first item] " #residue"]
        pos = " #residue"
        value = <value>
    ]
)

; Evaluation steps need to be able to carry forward the aggregate specifier
; on their return results, otherwise the LET would be forgotten each time
; you make a step.
;
; !!! This leads to some bad properties if you try to seek around in the
; block you get back, e.g. if you run it again or try to do lookups of the
; words, you'll get out of sync stuff:
;
; https://forum.rebol.info/t/1496
(
    x: <user>
    output: '~unset~
    block: evaluate evaluate evaluate [let x: 10 output: x]
    did all [
        block = []
        output = 10
        x = <user>
    ]
)

; ADD-LET-BINDING is a conceptual step for making your own LET-like thing.
(
    maker: func [name] [
        frame: binding of 'return
        add-let-binding frame (to word! unspaced [name 1]) <one>
        add-let-binding frame (to word! unspaced [name 2]) <two>
    ]
    maker "demo"
    did all [
        demo1 = <one>
        demo2 = <two>
    ]
)

(
    bar: func [] [
        let x: 10
        let y: [x + z]

        let foo: func [] compose [let z: 20, ((y))]
        foo
    ]
    bar = 30
)

(
    bar: func [] [
        let x: 10
        let y: [x + z]

        let foo: func [] compose [let z: 20, (y)]
        func [] compose collect [keep [let z: 2000], keep y, keep [do (y)]]
    ]
    baz: bar
    baz = 2010
)

; we want to create a situation where two LET based chains of patches need
; to be merged.  Such merging only is necessary when a specifier is being
; derived, e.g. the meeting of two blocks with LET chains in their binding.
(
    block1: do [let x: 10, [x + y]]
    block2: do compose/deep [let y: 20, [(block1)]]
    30 = do first block2
)

; Slightly more complex version...use functions
(
    block1: reeval func [] [let x: 10, [x + y]]
    block2: reeval func [] compose/deep [let y: 20, [(block1)]]
    30 = do first block2
)

; REEVAL presents a different case to the "wave of binding" a LET introduces
; to the evaluation.  The execution of the GROUP! needs to be able to discern
; if it was part of the input feed or not...e.g. REEVAL needs be different.
[
    (
        bar: func [b] [
            let n: 10
            reeval b.1  ; should not apply LET of N to fetched result
        ]

        foo: func [n] [
           bar [(n)]
        ]

        1 = foo 1
    )
    (
        bar: func [b] [
            do compose [
                let n: 10
                reeval (b.1)  ; updated LET of N should apply (LET "sees" (n))
            ]
        ]

        foo: func [n] [
           bar [(n)]
        ]

        10 = foo 1
    )

    ; Same goes for WORD!, SET-WORD!, etc.
    (
        x: 10
        y: 'x
        10 = do [let x: 20, reeval y]
    )
    (
        x: 10
        y: 'x
        20 = do compose [let x: 20, reeval '(y)]
    )
    (
        x: 10
        20 = do compose [let x: 20, reeval 'x]  ; sanity check
    )
]

; LET is also a PARSE keyword.  It means the variable will only be visible
; inside the rules and subrules
(
    x: <before>
    did all [
        #a = catch [
            parse3 "a" [let x: skip (throw x)]
        ]
        x = <before>
    ]
)

; LET in parse with a non-set works like LET usually does; declares but leaves
; as unset.
(
    x: <before>
    did all [
        10 = catch [
            parse3 "a" [let x (x: 10) "a" (throw x)]
        ]
        x = <before>
    ]
)

; BLANK! and ISSUE! should be legal in LET.
[(
    var: #  ; v-- second result is discarded, but request did partial transcode
    'abc = let [# (var)]: transcode "abc def"
)(
    var: _  ; opting out of second result, hence full transcode
    [abc def] = let [# (var)]: transcode "abc def"
)]
