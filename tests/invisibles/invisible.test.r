; %invisible.test.r


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
