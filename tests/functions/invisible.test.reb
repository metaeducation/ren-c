; COMMENT is fully invisible.
;
; https://trello.com/c/dWQnsspG

(
    1 = do [comment "a" 1]
)
(
    1 = do [1 comment "a"]
)
(
    ~empty~ = do [comment "a"]
)

(
    val: <overwritten>
    pos: evaluate/result [
        1 + comment "a" comment "b" 2 * 3 fail "too far"
    ] 'val
    did all [
        val = 9
        pos = [fail "too far"]
    ]
)
(
    val: <overwritten>
    pos: evaluate/result [
        1 comment "a" + comment "b" 2 * 3 fail "too far"
    ] 'val
    did all [
        val = 9
        pos = [fail "too far"]
    ]
)
(
    val: <overwritten>
    pos: evaluate/result [
        1 comment "a" comment "b" + 2 * 3 fail "too far"
    ] 'val
    did all [
        val = 9
        pos = [fail "too far"]
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
    ~empty~ = do [elide "a"]
)

(
    e: trap [
        evaluate evaluate [1 elide "a" + elide "b" 2 * 3 fail "too far"]
    ]
    e/id = 'expect-arg
)
(
    pos: evaluate evaluate [1 elide "a" elide "b" + 2 * 3 fail "too far"]
    pos = lit '[elide "b" + 2 * 3 fail "too far"]
)
(
    pos: evaluate [
        1 + 2 * 3 elide "a" elide "b" fail "too far"
    ] 'val
    did all [
        val = 9
        pos = [elide "a" elide "b" fail "too far"]
    ]
)


(
    unset 'x
    x: 1 + 2 * 3
    elide (y: :x)

    did all [x = 9, y = 9]
)
(
    unset 'x
    x: 1 + elide (y: 10) 2 * 3
    did all [
        x = 9
        y = 10
    ]
)

(
    unset 'x
    unset 'y
    unset 'z

    x: 10
    y: 1 comment [+ 2
    z: 30] + 7

    did all [
        x = 10
        y = 8
        not set? 'z
    ]
)

; ONCE-BAR was an experiment created to see if it could be done, and was
; thought about putting in the box.  Notationally it was || to correspond as
; a stronger version of |.  Not only was it not used, but since COMMA! has
; overtaken the | it no longer makes sense.
;
; Keeping as a test of the variadic feature it exercised.
[
    (|1|: func [
        {Barrier that's willing to only run one expression after it}

        return: [<opt> any-value!]
        right [<opt> <end> any-value! <variadic>]
        :lookahead [any-value! <variadic>]
        look:
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
    (error? trap [1 + 2 |1| 3 + 4 5 + 6])
]

(
    ~empty~ = do [|||]
)
(
    3 = do [1 + 2 ||| 10 + 20, 100 + 200]
)
(
    ok? trap [reeval (func [x [<end>]] []) ||| 1 2 3]
)
(
    error? trap [reeval (func [x [<opt>]] []) ||| 1 2 3]
)

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
            func [return: [<opt> word!] x [word!]] [:x]
        left-normal*: enfixed right-normal*:
            func [return: [<opt> word!] x [word! <end>]] [:x]

        left-defer: enfixed tweak (copy :left-normal) 'defer on
        left-defer*: enfixed tweak (copy :left-normal*) 'defer on

        left-soft: enfixed right-soft:
            func [return: [<opt> word!] 'x [word!]] [:x]
        left-soft*: enfixed right-soft*:
            func [return: [<opt> word!] 'x [word! <end>]] [:x]

        left-hard: enfixed right-hard:
            func [return: [<opt> word!] :x [word!]] [:x]
        left-hard*: enfixed right-hard*:
            func [return: [<opt> word!] :x [word! <end>]] [:x]

        true
    )

    ('no-arg = (trap [right-normal ||])/id)
    (null? do [right-normal* ||])
    (null? do [right-normal*])

    ('no-arg = (trap [|| left-normal])/id)
    (null? do [|| left-normal*])
    (null? do [left-normal*])

    ('no-arg = (trap [|| left-defer])/id)
    (null? do [|| left-defer*])
    (null? do [left-defer*])

    ('|| = do [right-soft ||])
    ('|| = do [right-soft* ||])
    (null? do [right-soft*])

    ; !!! This was legal at one point, but the special treatment of left
    ; quotes when there is nothing to their right means you now get errors.
    ; It's not clear what the best behavior is, so punting for now.
    ;
    ('literal-left-path = (trap [<bug> 'left-soft = do [|| left-soft]])/id)
    ('literal-left-path = (trap [<bug> 'left-soft* = do [|| left-soft*]])/id)
    (null? do [left-soft*])

    ('|| = do [right-hard ||])
    ('|| = do [right-hard* ||])
    (null? do [right-hard*])

    ; !!! See notes above.
    ;
    ('literal-left-path = (trap [<bug> 'left-hard = do [|| left-hard]])/id)
    ('literal-left-path = (trap [<bug> 'left-hard* = do [|| left-hard*]])/id)
    (null? do [left-hard*])
]


; Variadic
[
    (
        left-normal: enfixed right-normal:
            func [return: [<opt> word!] x [word! <variadic>]] [take x]
        left-normal*: enfixed right-normal*:
            func [return: [<opt> word!] x [word! <variadic> <end>]] [take x]

        left-defer: enfixed tweak (copy :left-normal) 'defer on
        left-defer*: enfixed tweak (copy :left-normal*) 'defer on

        left-soft: enfixed right-soft:
            func [return: [<opt> word!] 'x [word! <variadic>]] [take x]
        left-soft*: enfixed right-soft*:
            func [return: [<opt> word!] 'x [word! <variadic> <end>]] [take x]

        left-hard: enfixed right-hard:
            func [return: [<opt> word!] :x [word! <variadic>]] [take x]
        left-hard*: enfixed right-hard*:
            func [return: [<opt> word!] :x [word! <variadic> <end>]] [take x]

        true
    )

; !!! A previous distinction between TAKE and TAKE* made errors on cases of
; trying to TAKE from a non-endable parameter.  The definition has gotten
; fuzzy:
; https://github.com/metaeducation/ren-c/issues/1057
;
;    (error? trap [right-normal ||])
;    (error? trap [|| left-normal])

    (null? do [right-normal* ||])
    (null? do [right-normal*])

    (null? do [|| left-normal*])
    (null? do [left-normal*])

    (null? trap [|| left-defer])  ; !!! Should likely be an error, as above
    (null? do [|| left-defer*])
    (null? do [left-defer*])

    ('|| = do [right-soft ||])
    ('|| = do [right-soft* ||])
    (null? do [right-soft*])

    ; !!! This was legal at one point, but the special treatment of left
    ; quotes when there is nothing to their right means you now get errors.
    ; It's not clear what the best behavior is, so punting for now.
    ;
    ('literal-left-path = (trap [<bug> 'left-soft = do [|| left-soft]])/id)
    ('literal-left-path = (trap [<bug> 'left-soft* = do [|| left-soft*]])/id)
    (null? do [left-soft*])

    ('|| = do [right-hard ||])
    ('|| = do [right-hard* ||])
    (null? do [right-hard*])

    ; !!! See notes above.
    ;
    ('literal-left-path = (trap [<bug> 'left-hard = do [|| left-hard]])/id)
    ('literal-left-path = (trap [<bug> 'left-hard* = do [|| left-hard*]])/id)
    (null? do [left-hard*])
]

; GROUP!s with no content act as invisible
(
    x: <unchanged>
    did all [
        'need-non-end = (trap [<discarded> x: ()])/id
        x = <unchanged>
    ]
)(
    x: <unchanged>
    did all [
        'need-non-end = (trap [<discarded> x: comment "hi"])/id
        x = <unchanged>
    ]
)(
    obj: make object! [x: <unchanged>]
    did all [
        'need-non-end = (trap [<discarded> obj/x: comment "hi"])/id
        obj/x = <unchanged>
    ]
)(
    obj: make object! [x: <unchanged>]
    did all [
        'need-non-end = (trap [<discarded> obj/x: ()])/id
        obj/x = <unchanged>
    ]
)

(void? (if true [] else [<else>]))
(void? (if true [comment <true-branch>] else [<else>]))

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
(integer? reeval lit (comment "this group vaporizes") 1020)
(<before> = (<before> reeval :comment "erase me"))
(
    x: <before>
    equal? true reeval :elide x: <after>  ; picks up the `x = <after>`
    x = <after>
)


; !!! Tests of invisibles interacting with functions should be in the file
; where those functions are defined, when test file structure gets improved.
;
(null? spaced [])
(null? spaced [comment "hi"])
(null? spaced [()])


; GROUP!s are able to "vaporize" if they are empty or invisible
; https://forum.rebol.info/t/permissive-group-invisibility/1153
;
(() 1 + () 2 = () 3)
((comment "one") 1 + (comment "two") 2 = (comment "three") 3)


; "Opportunistic Invisibility" means that functions can treat invisibility as
; a return type, decided on after they've already started running.  This means
; using the @(...) form of RETURN, which can also be used for chaining.
[
    (vanish-if-odd: func [return: [<invisible> integer!] x] [
        if even? x [return x]
        return @()
    ] true)

    (2 = (<test> vanish-if-odd 2))
    (<test> = (<test> vanish-if-odd 1))

    (vanish-if-even: func [return: [<invisible> integer!] y] [
       return @(vanish-if-odd y + 1)
    ] true)

    (<test> = (<test> vanish-if-even 2))
    (2 = (<test> vanish-if-even 1))
]


; Invisibility is a checked return type, if you use a type spec...but allowed
; by default if not.
[
    (
        no-spec: func [x] [return @()]
        <test> = (<test> no-spec 10)
    )
    (
        int-spec: func [return: [integer!] x] [return @()]
        e: trap [int-spec 10]
        e/id = 'bad-invisible
    )
    (
        invis-spec: func [return: [<invisible> integer!] x] [return @()]
        <test> = (<test> invis-spec 10)
    )
]
