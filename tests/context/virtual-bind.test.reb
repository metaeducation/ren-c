; %virtual-bind.test.reb
;
; Virtual binding is a mechanism for creating a view of a block where its
; binding is seen differetly, without disrupting other views of that block.
; It is exposed via the IN and USE constructs, and is utilized by FOR-EACH
; and MAKE OBJECT!
;

; Basic example of virtual binding not disrupting the bindings of a block.
(
    x: 1000
    block: '[add x 20]
    obj284: make object! compose [x: 284, add: (^add)]
    all [
        1020 = eval $ block
        304 = eval inside obj284 block
        1020 = eval $ block
    ]
)

; One of the first trip-ups for virtual binding was Brett's PARSING-AT,
; which exposed an issue with virtual binding and PARSE (which was applying
; bindings twice in cases of fetched words).  This is isolated from that
(
    make-rule: func [:make-rule] [  ; refinement helps recognize in C Probe()
        use [rule position][
            rule: compose:deep [
                [[position: <here>, "a"]]
            ]
            return use [x] compose:deep [
                [(as group! rule) rule]
            ]
        ]
    ]
    all [
        let r: make-rule
        did parse3 "a" r  ; this was where the problem was
    ]
)

; Compounding bindingss is tricky, and many of the situations only arise
; when you return still-relative material (e.g. from nested blocks in a
; function body) that has only been derelativized at the topmost level.
; Using GROUP!s is a good way to catch this since they're easy to evaluate
; in a nested faction.
[
    (
        add1020: func [x] [return use [y] [y: 1020, $(((x + y)))]]
        add1324: func [x] [
            return use [z] compose:label:deep [
                z: 304
                $(((z + (<*> add1020 x))))
            ] <*>
        ]
        add2020: func [x] [
            return use [zz] compose:label:deep [
                zz: 696
                $(((zz + (<*> add1324 x))))
            ] <*>
        ]
        ok
    )

    (1324 = eval add1020 304)
    (2020 = eval add1324 696)
    (2021 = eval add2020 1)
]

[
    (
        group: use [x y] [x: 10, y: 20, $(((x + y)))]
        group = '(((x + y)))
    )

    ; Basic robustness
    ;
    (30 = eval group)
    (30 = eval compose [(group)])
    (30 = eval compose:deep [eval [(group)]])
    (30 = reeval unrun does [eval compose [(group)]])

    ; Unrelated USE should not interfere
    ;
    (30 = use [z] [z: ~<whatever>~ eval compose [(group)]])

    ; Related USE shouldn't interfere, either
    ;
    (30 = use [y] [y: 100, eval compose [(group)]])

    ; You have to invasvely overbind to see an effect
    ;
    (110 = use [y] [y: 100, eval compose [(overbind $y group)]])
]


; This was a little test made to compare speed with R3-Alpha, keeping it.
(
    data: array:initial 20 1
    sum: 0
    for-each 'x data wrap [
        code: copy []  ; block captures binding that can see X
        for-each 'y data [  ; block can't see Y w/o overbind, let's COMPOSE it
            append code spread compose:deep [sum: sum + eval [x + (y) + z]]
        ]
        for-each 'z data code  ; FOR-EACH overbinds for Z visibility
    ]
    sum = 24000
)


; Virtual Binding once gave back a CONST value, because it couldn't assure you
; that mutable bindings would have an effect.  That's no longer the case (to
; the extent you should ever mutably bind).
;
; https://forum.rebol.info/t/765/2
[
    (21 = eval bind use [x] [x: 10, [x + 1]] make object! [x: 20])
]

; This was originally a test for "virtual binding chain reuse".  That is a
; currently defunct optimization, and binding rules have changed drastically.
(
    x: 100
    y: 200
    plus-global: '[x + y]  ; unbound--no capture
    minus-global: [x - y]  ; captures x and y
    alpha: make object! compose [  ; virtual binds body to obj
        x: 10
        y: 20
        plus: (plus-global)
        minus: (minus-global)
    ]
    beta: make object! compose [  ; virtual binds body to obj
        x: 1000
        y: 2000
        plus: (plus-global)
        minus: (minus-global)
    ]
    [30 3000 -100 -100 30 -100 3000 -100 101 -100 101 -100] = collect [
        for-each 'y [1] compose [
            keep eval (alpha.plus)
            keep eval (beta.plus)
            keep eval (alpha.minus)
            keep eval (beta.minus)
            keep eval alpha.plus
            keep eval alpha.minus
            keep eval beta.plus
            keep eval beta.minus
            keep eval inside [] plus-global
            keep eval inside [] minus-global
            keep eval inside [] (plus-global)
            keep eval inside [] (minus-global)
        ]
    ]
)
