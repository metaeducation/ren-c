; %virtual-bind.test.reb
;
; Virtual binding is a mechanism for creating a view of a block where its
; binding is seen differetly, without disrupting other views of that block.
; It is exposed via the IN and USE constructs, and is utilized by FOR-EACH
; and MAKE OBJECT!
;

; Basic example of virtual binding not disrupting the bindings of a block.
(
    obj1000: make object! [x: 1000]
    block: [x + 20]
    bind block obj1000

    obj284: make object! [x: 284]
    did all [
        1020 = do block
        304 = do in obj284 block
        1020 = do block
    ]
)

; One of the first trip-ups for virtual binding was Brett's PARSING-AT,
; which exposed an issue with virtual binding and PARSE (which was applying
; specifiers twice in cases of fetched words).  This is isolated from that
(
    make-rule: func [/make-rule] [  ; refinement helps recognize in C Probe()
        use [rule position][
            rule: compose/deep [
                [[position: here, "a"]]
            ]
            use [x] compose/deep [
                [(as group! rule) rule]
            ]
        ]
    ]
    did all [
        r: make-rule
        "a" = do first first first r  ; sanity check with plain DO
        parse "a" r  ; this was where the problem was
    ]
)

; Compounding specifiers is tricky, and many of the situations only arise
; when you return still-relative material (e.g. from nested blocks in a
; function body) that has only been derelativized at the topmost level.
; Using GROUP!s is a good way to catch this since they're easy to evaluate
; in a nested faction.
[
    (
        add1020: func [x] [use [y] [y: 1020, '(((x + y)))]]
        add1324: func [x] [
            use [z] compose/deep <*> [
                z: 304
                '(((z + (<*> add1020 x))))
            ]
        ]
        add2020: func [x] [
            use [zz] compose/deep <*> [
                zz: 696
                '(((zz + (<*> add1324 x))))
            ]
        ]

        true
    )

    (1324 = do add1020 304)
    (2020 = do add1324 696)
    (2021 = do add2020 1)
]

[
    (
        group: append '() use [x y] [x: 10, y: 20, '((x + y))]
        group = '(((x + y)))
    )

    ; Basic robustness
    ;
    (30 = do group)
    (30 = do compose [(group)])
    (30 = do compose [(group)])
    (30 = do compose/deep [do [(group)]])
    (30 = reeval does [do compose [(group)]])

    ; Unrelated USE should not interfere
    ;
    (30 = use [z] compose [(group)])
    (30 = use [z] compose/deep [do [(group)]])

    ; Related USE should override
    ;
    (110 = use [y] compose [y: 100, (group)])
    (110 = use [y] compose/deep [y: 100, do [(group)]])

    ; Chaining will affect any values that were visible at the time of the
    ; USE (think of it the way you would as if the BIND were run mutably).
    ; In the first case, the inner use sees the composed group's x and y,
    ; but the compose is run after the outer use, so the x override is unseen.
    ; Moving the compose so it happens before the use [x] runs will mean the
    ; x gets overridden as well.
    ;
    (110 = use [x] [x: 1000, use [y] compose [y: 100, (group)]])
    (1100 = use [x] compose/deep [x: 1000, use [y] [y: 100, do [(group)]]])
]


; This was a little test made to compare speed with R3-Alpha, keeping it.
(
    data: array/initial 20 1
    sum: 0
    for-each x data [
        code: copy []
        for-each y data [
            append code compose [sum: sum + do [(x) + (y) + z]]
        ]
        for-each z data code
    ]
    sum = 24000
)


; Virtual Binding gives back a CONST value, because it can't assure you that
; mutable bindings would have an effect.  You can second-guess it.
; https://forum.rebol.info/t/765/2
[
    (
        e: trap [bind use [x] [x: 10, [x + 1]] make object! [x: 20]]
        e/id = 'const-value
    )

    ; It tried to warn you that the X binding wouldn't be updated... but
    ; using MUTABLE overrides the warning.
    ;
    (11 = do bind mutable use [x] [x: 10, [x + 1]] make object! [x: 20])

    ; Quoted values elude the CONST inheritance (this is a general mechanism
    ; that is purposeful, and used heavily by the API).  The more cautious
    ; approach is not to use quotes as part of inline evaluations.
    ;
    ; https://forum.rebol.info/t/1062/4
    ;
    (11 = do bind use [x] [x: 10, '(x + 1)] make object! [x: 20])
    (
        e: trap [bind use [x] [x: 10, just (x + 1)] make object! [x: 20]]
        e/id = 'const-value
    )
]

; Test virtual binding chain reuse scenario.
;
; !!! Right now the only way to make sure it's actually reusing the same
; chain is to set a breakpoint in the debugger.  There should probably be
; some way to reflect this--at least in debug builds--so you can analyze
; the virtual bind patch information.
(
    x: 100
    y: 200
    plus-global: [x + y]
    minus-global: [x - y]
    alpha: make object! compose/only [  ; virtual binds body to obj
        x: 10
        y: 20
        plus: (plus-global)
        minus: (minus-global)
    ]
    beta: make object! compose/only [  ; virtual binds body to obj
        x: 1000
        y: 2000
        plus: (plus-global)
        minus: (minus-global)
    ]
    [11 1001 999 9 30 -10 3000 -1000 300 -100] = collect [
        for-each y [1] compose/only [
            keep do (alpha/plus)  ; needs chain y -> alpha
            keep do (beta/plus)  ; needs chain y -> beta
            keep do (beta/minus)  ; also needs chain y -> beta
            keep do (alpha/minus)  ; back to needing chain y -> alpha
            keep do alpha/plus
            keep do alpha/minus
            keep do beta/plus
            keep do beta/minus
            keep do plus-global
            keep do minus-global
        ]
    ]
)
