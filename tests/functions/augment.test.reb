; AUGMENT facility for making a variant of a function that acts just the
; same, but has more parameters.

(
    foo: lambda [x] [x]
    bar: augment :foo [y]
    all [
        [x y] = parameters of :bar
        10 = bar 10 20
    ]
)

~dup-vars~ !! (
    augment (func [x] [return x]) [x]
)


; Tests with ADAPT
(
    sum: adapt augment (func [x] [return x]) [y] [
        x: x + y
    ]
    1020 = sum 1000 20
)(
    mix: adapt augment (x -> [x]) [y /sub] [
        x: reeval (either sub [:subtract] [:add]) x y
    ]
    all [
        1020 = mix 1000 20
        980 = mix/sub 1000 20
    ]
)


; Tests with ENCLOSE
[
    (switch-d: enclose (augment :switch [
        /default "Default case if no others are found"
            [block!]
    ]) lambda [f [frame!]] [
        let def: f.default
        eval f else (try def)
    ]
    true)

    (1020 = switch-d 'b ['b [1000 + 20]])
    (1020 = switch-d/default 'b ['b [1000 + 20]] [300 + 4])
    (304 = switch-d/default 'j ['b [1000 + 20]] [300 + 4])
]

; Augmenting a specialized function
(
    two-a-plus-three-b: lambda [a [integer!] /b [integer!]] [
        (2 * a) + either b [3 * b] [0]
    ]
    two-a-plus-six: specialize :two-a-plus-three-b [b: 2]

    two-a-plus-six-plus-four-c: enclose augment :two-a-plus-six [
        /c [integer!]
    ] lambda [f [frame!]] [
        let old-c: f.c
        let x: do f
        if old-c [
            x + (4 * old-c)
        ] else [
            x
        ]
    ]

    all [
        10 = two-a-plus-six-plus-four-c 2
        50 = two-a-plus-six-plus-four-c/c 2 10
    ]
)

; Check to see that AUGMENT of the help expands it.
[(
    all [
        orig: func ["description" a "a" /b "b"] [return <unused>]
        aug: augment :orig [c "c" /d "d"]
        if m: adjunct-of :aug [
            m.description = null  ; description not inherited ATM
        ]
        (select :aug 'a).text = "a"
        (select :aug 'b).text = "b"
        (select :aug 'c).text = "c"
        (select :aug 'd).text = "d"
    ]
)]
