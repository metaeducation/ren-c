; COMMENT is fully invisible.
;
; https://trello.com/c/dWQnsspG

(1 = do [comment "a" 1])
(1 = do [1 comment "a"])
(nihil? comment "a")
(nihil? (comment "a"))

(nihil' = (meta comment "a"))
((quote nihil') = ^(^ comment "a"))

(nihil' = meta eval [comment "a"])
((quote nihil') = ^(^ eval [comment "a"]))

; !!! At one time, comment mechanics allowed comments to be enfix such that
; they ran as part of the previous evaluation.  This is no longer the case,
; as invisible mechanics no longer permit interstitials--which helps make
; the evaluator more sane, without losing the primary advantages of invisibles.
;
; https://forum.rebol.info/t/1582

~no-value~ !! (
    pos: ~
    val: evaluate/next [
        1 + comment "a" comment "b" 2 * 3 fail "too far"
    ] 'pos
)
(
    pos: ~
    val: evaluate/next [
        1 comment "a" + comment "b" 2 * 3 fail "too far"
    ] 'pos
    did all [
        val = 1
        pos = [comment "a" + comment "b" 2 * 3 fail "too far"]
    ]
)
(
    pos: ~
    val: evaluate/next [
        1 comment "a" comment "b" + 2 * 3 fail "too far"
    ] 'pos
    did all [
        val = 1
        pos = [comment "a" comment "b" + 2 * 3 fail "too far"]
    ]
)

; ELIDE is not fully invisible, but trades this off to be able to run its
; code "in turn", instead of being slaved to eager enfix evaluation order.
;
; https://trello.com/c/snnG8xwW

(
    1 = do [elide "a" 1]
)
(
    1 = do [1 elide "a"]
)
(
    void' = ^ do [elide "a"]
)
(
    nihil' = ^ eval [elide "a"]
)
(nihil? elide "a")
(nihil' = ^ elide "a")


~no-value~ !! (
    evaluate evaluate [1 elide "a" + elide "b" 2 * 3 fail "too far"]
)
(
    code: [1 elide "a" elide "b" + 2 * 3 fail "too far"]
    evaluate/next code 'pos
    evaluate/next pos 'pos
    pos = [elide "b" + 2 * 3 fail "too far"]
)
(
    pos: ~
    val: evaluate/next [
        1 + 2 * 3 elide "a" elide "b" fail "too far"
    ] 'pos
    did all [
        val = 9
        pos = [elide "a" elide "b" fail "too far"]
    ]
)


(
    x: ~
    x: 1 + 2 * 3
    elide (y: :x)

    did all [x = 9, y = 9]
)
~no-value~ !! (
    x: ~
    x: 1 + elide (y: 10) 2 * 3  ; non-interstitial, no longer legal
)

; ONCE-BAR was an experiment created to see if it could be done, and was
; thought about putting in the box.  Notationally it was || to correspond as
; a stronger version of |.  Not only was it not used, but since COMMA! has
; overtaken the | it no longer makes sense.
;
; Keeping as a test of the variadic feature it exercised.
[
    (
        weird: [|1|]
        did all [
            word? first weird
            "1" = as text! first weird
            "[|1|]" = mold weird
        ]
    )

    (|1|: lambda [
        {Barrier that's willing to only run one expression after it}

        right [<opt> <end> any-value! <variadic>]
        'lookahead [any-value! <variadic>]
        <local> look
    ][
        take right  ; returned value

        elide any [
            tail? right,
            '|1| = look: take lookahead  ; hack...recognize selfs
        ] else [
            fail 'right [
                "|1| expected single expression, found residual of" :look
            ]
        ]
    ]
    true)

    (7 = (1 + 2 |1| 3 + 4))
    ~???~ !! (1 + 2 |1| 3 + 4 5 + 6)
]

(
    void? do [|||]
)
(
    nihil? eval [|||]
)
(
    3 = do [1 + 2 ||| 10 + 20, 100 + 200]
)

; !!! There used to be some concept that these void-returning things could
; appear like an "end" to functions.  But rules for reification have changed,
; in that there are no "pure invisibles".  So saying that it's an <end> is
; questionable.  Review when there's enough time in priorities to think on it.
;
;     (ok? trap [reeval (lambda [x [<end>]] []) ||| 1 2 3])
;     (error? trap [reeval (lambda [x [<opt>]] []) ||| 1 2 3])

(
    [3 11] = reduce [1 + 2 elide 3 + 4 5 + 6]
)


; Test expression barrier invisibility

(
    3 = (1 + 2,)  ; COMMA! barrier
)(
    3 = (1 + 2 ||)  ; usermode expression barrier
)(
    3 = (1 + 2 comment "invisible")
)

; Non-variadic
[
    (
        left-normal: enfixed right-normal:
            func [return: [<opt> word!] x [word!]] [return x]
        left-normal*: enfixed right-normal*:
            func [return: [<opt> word!] x [word! <end>]] [return x]

        left-defer: enfixed tweak (copy unrun :left-normal) 'defer on
        left-defer*: enfixed tweak (copy unrun :left-normal*) 'defer on

        left-soft: enfixed right-soft:
            func [return: [<opt> word!] 'x [word!]] [return x]
        left-soft*: enfixed right-soft*:
            func [return: [<opt> word!] 'x [word! <end>]] [return x]

        left-hard: enfixed right-hard:
            func [return: [<opt> word!] :x [word!]] [return x]
        left-hard*: enfixed right-hard*:
            func [return: [<opt> word!] :x [word! <end>]] [return x]

        true
    )

    ~no-arg~ !! (right-normal ||)
    (null? do [right-normal* ||])
    (null? do [right-normal*])

    ~no-arg~ !! (|| left-normal)
    (null? do [|| left-normal*])
    (null? do [left-normal*])

    ~no-arg~ !! (|| left-defer)
    (null? do [|| left-defer*])
    (null? do [left-defer*])

    ('|| = do [right-soft ||])
    ('|| = do [right-soft* ||])
    (null? do [right-soft*])

    ; !!! At one point, when left quoting saw a "barrier" to the left, it would
    ; perceive it as a null.  Today's barriers (commas or ||) make nihil, and
    ; we have to distinguish between the case where it expects to see a nihil
    ; vs. when that should act as an <end>.  This is not thought out well.
    ;
    ~expect-arg~ !! (<bug> do [|| left-soft])
    ~expect-arg~ !! (<bug> do [|| left-soft*])
    (null? do [left-soft*])

    ('|| = do [right-hard ||])
    ('|| = do [right-hard* ||])
    (null? do [right-hard*])

    ; !!! See notes above.
    ;
    ~expect-arg~ !! (<bug> do [|| left-hard])
    ~expect-arg~ !! (<bug> do [|| left-hard*])
    (null? do [left-hard*])
]


; Variadic
[
    (
        left-normal: enfixed right-normal:
            func [return: [<opt> word!] x [word! <variadic>]] [
                return take x
            ]
        left-normal*: enfixed right-normal*:
            func [return: [<opt> word!] x [word! <variadic> <end>]] [
                return try take x
            ]

        left-defer: enfixed tweak (copy unrun :left-normal) 'defer on
        left-defer*: enfixed tweak (copy unrun :left-normal*) 'defer on

        left-soft: enfixed right-soft:
            func [return: [<opt> word!] 'x [word! <variadic>]] [
                return take x
            ]
        left-soft*: enfixed right-soft*:
            func [return: [<opt> word!] 'x [word! <variadic> <end>]] [
                return try take x
            ]

        left-hard: enfixed right-hard:
            func [return: [<opt> word!] :x [word! <variadic>]] [
                return take x
            ]
        left-hard*: enfixed right-hard*:
            func [return: [<opt> word!] :x [word! <variadic> <end>]] [
                return try take x
            ]

        true
    )

    ; !!! A previous distinction between TAKE and TAKE* made errors on cases of
    ; trying to TAKE from a non-endable parameter.  The definition has gotten
    ; fuzzy:
    ; https://github.com/metaeducation/ren-c/issues/1057
    ;
    ~nothing-to-take~ !! (do [right-normal ||])
    ~nothing-to-take~ !! (do [|| left-normal])

    (null? do [right-normal* ||])
    (null? do [right-normal*])

    (null? do [|| left-normal*])
    (null? do [left-normal*])

    ~nothing-to-take~ !! (do [|| left-defer])
    (null? do [|| left-defer*])
    (null? do [left-defer*])

    ('|| = do [right-soft ||])
    ('|| = do [right-soft* ||])
    (null? do [right-soft*])

    ; !!! At one point, when left quoting saw a "barrier" to the left, it would
    ; perceive it as a null.  Today's barriers (commas or ||) make nihil, and
    ; we have to distinguish between the case where it expects to see a nihil
    ; vs. when that should act as an <end>.  This is not thought out well.
    ;
    ~expect-arg~ !! (<bug> do [|| left-soft])
    ~expect-arg~ !! (<bug> do [|| left-soft*])
    (null? do [left-soft*])

    ~nothing-to-take~ !! (do [right-hard])
    ('|| = do [right-hard ||])
    ('|| = do [right-hard* ||])
    (null? do [right-hard*])

    ; !!! See notes above.
    ;
    ~expect-arg~ !! (<bug> do [|| left-hard])
    ~expect-arg~ !! (<bug> do [|| left-hard*])
    (null? do [left-hard*])
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

('~[']~ = ^ (if true [] else [<else>]))
('~[']~ = ^(if true [comment <true-branch>] else [<else>]))

(1 = all [1 elide <vaporize>])
(1 = any [1 elide <vaporize>])
([1] = reduce [1 elide <vaporize>])

(304 = (1000 + 20 (** foo <baz> (bar)) 300 + 4))
(304 = (1000 + 20 ** (
    foo <baz> (bar)
) 300 + 4))


; REEVAL has been tuned to be able to act invisibly if the thing being
; reevaluated turns out to be invisible.
;
(integer? (reeval the (comment "this group vaporizes") 1020))

(<before> = (<before> reeval :comment "erase me"))
(
    x: <before>
    did all [
        10 = (
            10 reeval :elide x: <after>
        )
        x = <after>
    ]
)


; !!! Tests of invisibles interacting with functions should be in the file
; where those functions are defined, when test file structure gets improved.
;
(null? spaced [])
(null? spaced [comment "hi"])
(null? spaced [()])


; GROUP!s "vaporize" if they are empty or invisible, but can't be used as
; inputs to enfix.
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
    (vanish-if-odd: func [return: [nihil? integer!] x] [
        if even? x [return x]
        return nihil
    ] true)

    (2 = (<test> vanish-if-odd 2))
    (<test> = (<test> vanish-if-odd 1))

    (vanish-if-even: func [return: [nihil? integer!] y] [
        return unmeta ^(vanish-if-odd y + 1)
    ] true)

    (<test> = (<test> vanish-if-even 2))
    (2 = (<test> vanish-if-even 1))
]


; Invisibility is a checked return type, if you use a type spec...but allowed
; by default if not.
[
    (
        no-spec: func [x] [return nihil]
        <test> = (<test> no-spec 10)
    )
    ~bad-return-type~ !! (
        int-spec: func [return: [integer!] x] [return nihil]
        int-spec 10
    )
    (
        invis-spec: func [return: [nihil? integer!] x] [
            return nihil
        ]
        <test> = (<test> invis-spec 10)
    )
]

(void' = ^ void)

~no-value~ !! (
    1 + 2 (comment "stale") + 3
)

; Functions that took nihil as normal parameters once received them as unset.
; It's not clear that this is an interesting feature, especially in light of
; COMMA!'s new mechanic getting its barrier-ness from returning nihil.
;
;    foo: lambda [x [nihil? integer!]] [if unset? 'x [<unset>] else [x]]
;    did all [
;        <unset> = foo comment "hi"
;        1020 = foo 1000 + 20
;    ]

(
    num-runs: 0

    add-period: func [x [<maybe> text!]] [
        num-runs: me + 1
        return append x "."
    ]

    did all [
        "Hello World." = add-period "Hello World"
        num-runs = 1
        null = add-period void  ; shouldn't run ADD-PERIOD body
        num-runs = 1
    ]
)
