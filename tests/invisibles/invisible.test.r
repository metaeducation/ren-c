; COMMENT is fully invisible.


; ONCE-BAR was an experiment created to see if it could be done, and was
; thought about putting in the box.  Notationally it was || to correspond as
; a stronger version of |.  Not only was it not used, but since COMMA! has
; overtaken the | it no longer makes sense.
;
; Keeping as a test of the variadic feature it exercised.
[
    (
        weird: [|1|]
        all [
            word? first weird
            "|1|" = as text! first weird
            "[|1|]" = mold weird
        ]
    )

    (|1|: lambda [
        "Barrier that's willing to only run one expression after it"

        right [any-stable? <variadic>]
        'lookahead [element? <variadic>]
        <local> look
    ][
        take right  ; returned value

        elide any [
            tail? right,
            '|1| = look: take lookahead  ; hack...recognize selfs
        ] else [
            panic:blame [
                "|1| expected single expression, found residual of" :look
            ] $right
        ]
    ]
    ok)

    (7 = (1 + 2 |1| 3 + 4))
    ~???~ !! (1 + 2 |1| 3 + 4 5 + 6)
]

(
    void? eval [|||]
)
(
    3 = eval [1 + 2 ||| 10 + 20, 100 + 200]
)

; Test expression barrier invisibility

(
    3 = (1 + 2,)  ; COMMA! barrier
)(
    3 = (1 + 2 ||)  ; usermode expression barrier
)

; Non-variadic
[
    (
        left-normal: infix /right-normal: (
            func [return: [null? word!] x [word!]] [return x]
        )
        left-normal*: infix /right-normal*: (
            func [return: [null? word!] x [word! <end>]] [return x]
        )

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft: (
            func [return: [null? word!] @(x) [word!]] [return x]
        )
        left-soft*: infix /right-soft*: (
            func [return: [null? word!] @(x) [word! <end>]] [return x]
        )

        left-hard: infix /right-hard: (
            func [return: [null? word!] 'x [word!]] [return x]
        )
        left-hard*: infix /right-hard*: (
            func [return: [null? word!] 'x [word! <end>]] [return x]
        )

        ok
    )

    ~no-arg~ !! (right-normal ||)
    (null? eval [right-normal* ||])
    (null? eval [right-normal*])

    ~no-arg~ !! (|| left-normal)
    (null? eval [|| left-normal*])
    (null? eval [left-normal*])

    ~no-arg~ !! (|| left-defer)
    (null? eval [|| left-defer*])
    (null? eval [left-defer*])

    ('|| = eval [right-soft ||])
    ('|| = eval [right-soft* ||])
    (null? eval [right-soft*])

    ~no-arg~ !! (eval [|| left-soft])
    (null? eval [|| left-soft*])
    (null? eval [left-soft*])

    ('|| = eval [right-hard ||])
    ('|| = eval [right-hard* ||])
    (null? eval [right-hard*])

    ~no-arg~ !! (eval [|| left-hard])
    (null? eval [|| left-hard*])
    (null? eval [left-hard*])
]


; Variadic
[
    (
        left-normal: infix /right-normal:
            func [return: [null? word!] x [word! <variadic>]] [
                return take x
            ]
        left-normal*: infix /right-normal*:
            func [return: [null? word!] x [word! <variadic> <end>]] [
                return try take x
            ]

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft:
            func [return: [null? word!] @(x) [word! <variadic>]] [
                return take x
            ]
        left-soft*: infix /right-soft*:
            func [return: [null? word!] @(x) [word! <variadic> <end>]] [
                return try take x
            ]

        left-hard: infix /right-hard:
            func [return: [null? word!] 'x [word! <variadic>]] [
                return take x
            ]
        left-hard*: infix /right-hard*:
            func [return: [null? word!] 'x [word! <variadic> <end>]] [
                return try take x
            ]

        ok
    )

    ; !!! A previous distinction between TAKE and TAKE* made errors on cases of
    ; trying to TAKE from a non-endable parameter.  The definition has gotten
    ; fuzzy:
    ; https://github.com/metaeducation/ren-c/issues/1057
    ;
    ~nothing-to-take~ !! (eval [right-normal ||])
    ~nothing-to-take~ !! (eval [|| left-normal])

    (null? eval [right-normal* ||])
    (null? eval [right-normal*])

    (null? eval [|| left-normal*])
    (null? eval [left-normal*])

    ~nothing-to-take~ !! (eval [|| left-defer])
    (null? eval [|| left-defer*])
    (null? eval [left-defer*])

    ('|| = eval [right-soft ||])
    ('|| = eval [right-soft* ||])
    (null? eval [right-soft*])

    ~nothing-to-take~ !! (eval [|| left-soft])
    (null? eval [|| left-soft*])
    (null? eval [left-soft*])

    ~nothing-to-take~ !! (eval [right-hard])
    ('|| = eval [right-hard ||])
    ('|| = eval [right-hard* ||])
    (null? eval [right-hard*])

    ~nothing-to-take~ !! (eval [|| left-hard])
    (null? eval [|| left-hard*])
    (null? eval [left-hard*])
]

~no-value~ !! (
    x: <overwritten>
    (<kept> x: ())
)
~no-value~ !! (
    x: <overwritten>
    (<kept> x: comment "hi")
)
~need-non-end~ !! (
    x: <overwritten>
    (<kept> x:,)
)

~no-value~ !! (
    obj: make object! [x: <overwritten>]
    (<kept> obj.x: comment "hi")
)
~no-value~ !! (
    obj: make object! [x: <overwritten>]
    (<kept> obj.x: ())
)
~need-non-end~ !! (
    obj: make object! [x: <overwritten>]
    (<kept> obj.x:,)
)

('~[]~ = lift (if ok [] else [<else>]))
('~[]~ = lift (if ok [comment <true-branch>] else [<else>]))

(304 = (1000 + 20 (** foo <baz> (bar)) 300 + 4))
(304 = (1000 + 20 ** (
    foo <baz> (bar)
) 300 + 4))


; GROUP!s "vaporize" if they are empty or invisible, but can't be used as
; inputs to infix.
;
; https://forum.rebol.info/t/permissive-group-invisibility/1153
;
~no-value~ !! (
    () 1 + () 2 = () 3
)
~no-value~ !! (
    (comment "one") 1 + (comment "two") 2 = (comment "three") 3
)

; "Opportunistic Invisibility" means that functions can treat invisibility as
; a return type, decided on after they've already started running.
[
    (vanish-if-odd: func [return: [ghost! integer!] x] [
        if even? x [return x]
        return ~,~
    ] ok)

    (2 = (<test> vanish-if-odd 2))
    (<test> = (<test> vanish-if-odd 1))

    (vanish-if-even: func [return: [ghost! integer!] y] [
        return unlift ^(vanish-if-odd y + 1)
    ] ok)

    (<test> = (<test> vanish-if-even 2))
    (2 = (<test> vanish-if-even 1))
]


; Invisibility is a checked return type, if you use a type spec...but allowed
; by default if not.
[
    (
        no-spec: func [x] [return ~,~]
        <test> = (<test> no-spec 10)
    )
    ~bad-return-type~ !! (
        int-spec: func [return: [integer!] x] [return ~,~]
        int-spec 10
    )
    (
        invis-spec: func [return: [~,~ integer!] x] [
            return ~,~
        ]
        <test> = (<test> invis-spec 10)
    )
]

((lift ^void) = lift ^void)

(
    num-runs: 0

    add-period: func [x [<opt-out> text!]] [
        num-runs: me + 1
        return append x "."
    ]

    all [
        "Hello World." = add-period "Hello World"
        num-runs = 1
        null = add-period ^void  ; shouldn't run ADD-PERIOD body
        num-runs = 1
    ]
)
