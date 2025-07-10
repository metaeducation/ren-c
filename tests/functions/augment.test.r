; AUGMENT facility for making a variant of a function that acts just the
; same, but has more parameters.

(
    foo: lambda [x] [x]
    bar: augment foo/ [y]
    all [
        [x y] = decorated-words-of bar/
        10 = bar 10 20
    ]
)

~dup-vars~ !! (
    augment (func [x] [return x]) [x]
)


; Tests with ADAPT
(
    /sum: adapt augment (func [x] [return x]) [y] [
        x: x + y
    ]
    1020 = sum 1000 20
)(
    /mix: adapt augment (x -> [x]) [y :sub] [
        x: reeval (either sub [:subtract] [:add]) x y
    ]
    all [
        1020 = mix 1000 20
        980 = mix:sub 1000 20
    ]
)


; Tests with ENCLOSE
[
    (/switch-d: enclose (augment switch/ [
        :default "Default case if no others are found"
            [block!]
    ]) lambda [f [frame!]] [
        eval f else (opt f.default)
    ]
    ok)

    (1020 = switch-d 'b ['b [1000 + 20]])
    (1020 = switch-d:default 'b ['b [1000 + 20]] [300 + 4])
    (304 = switch-d:default 'j ['b [1000 + 20]] [300 + 4])
]

; Augmenting a specialized function
(
    two-a-plus-three-b: lambda [a [integer!] :b [integer!]] [
        (2 * a) + either b [3 * b] [0]
    ]
    two-a-plus-six: specialize two-a-plus-three-b/ [b: 2]

    two-a-plus-six-plus-four-c: enclose augment two-a-plus-six/ [
        :c [integer!]
    ] lambda [f [frame!]] [
        let x: eval f
        if f.c [
            x + (4 * f.c)
        ] else [
            x
        ]
    ]

    all [
        10 = two-a-plus-six-plus-four-c 2
        50 = two-a-plus-six-plus-four-c:c 2 10
    ]
)

; Check to see that AUGMENT of the help expands it.
[(
    all wrap [
        orig: func ["description" a "a" :b "b"] [return <unused>]
        aug: lift:lite augment orig/ [c "c" :d "d"]
        if m: adjunct-of aug [
            m.description = null  ; description not inherited ATM
        ]
        aug.a.text = "a"
        aug.b.text = "b"
        aug.c.text = "c"
        aug.d.text = "d"
    ]
)]
