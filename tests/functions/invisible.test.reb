; COMMENT is fully invisible.
;
; https://trello.com/c/dWQnsspG

(1 = eval [comment "a" 1])
(1 = eval [1 comment "a"])
(void? comment "a")
(void? (comment "a"))

('~,~ = (meta comment "a"))
((quote '~,~) = meta (meta comment "a"))

('~,~ = meta eval:undecayed [comment "a"])
((quote '~,~) = meta (meta eval:undecayed [comment "a"]))

; !!! At one time, comment mechanics allowed comments to be infix such that
; they ran as part of the previous evaluation.  This is no longer the case,
; as invisible mechanics no longer permit interstitials--which helps make
; the evaluator more sane, without losing the primary advantages of invisibles.
;
; https://forum.rebol.info/t/1582

~no-value~ !! (
    [pos val]: evaluate:step [
        1 + comment "a" comment "b" 2 * 3 fail "too far"
    ]
)
(
    [pos val]: evaluate:step [
        1 comment "a" + comment "b" 2 * 3 fail "too far"
    ]
    all [
        val = 1
        pos = [comment "a" + comment "b" 2 * 3 fail "too far"]
    ]
)
(
    [pos val]: evaluate:step [
        1 comment "a" comment "b" + 2 * 3 fail "too far"
    ]
    all [
        val = 1
        pos = [comment "a" comment "b" + 2 * 3 fail "too far"]
    ]
)

(
    1 = eval [elide "a" 1]
)
(
    1 = eval [1 elide "a"]
)
(
    '~,~ = ^ eval:undecayed [elide "a"]
)
(ghost? elide "a")
('~,~ = ^ elide "a")


~no-value~ !! (
    evaluate evaluate [1 elide "a" + elide "b" 2 * 3 fail "too far"]
)
(
    code: [1 elide "a" elide "b" + 2 * 3 fail "too far"]
    pos: evaluate:step code
    pos: evaluate:step pos
    pos = [elide "b" + 2 * 3 fail "too far"]
)
(
    [pos val]: evaluate:step [
        1 + 2 * 3 elide "a" elide "b" fail "too far"
    ]
    all [
        val = 9
        pos = [elide "a" elide "b" fail "too far"]
    ]
)


(
    y: ~
    x: ~
    x: 1 + 2 * 3
    elide (y: :x)

    all [x = 9, y = 9]
)
~no-value~ !! (
    y: ~
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
        all [
            word? first weird
            "|1|" = as text! first weird
            "[|1|]" = mold weird
        ]
    )

    (|1|: lambda [
        "Barrier that's willing to only run one expression after it"

        right [any-value? <variadic>]
        'lookahead [element? <variadic>]
        <local> look
    ][
        take right  ; returned value

        elide any [
            tail? right,
            '|1| = look: take lookahead  ; hack...recognize selfs
        ] else [
            fail:blame [
                "|1| expected single expression, found residual of" :look
            ] $right
        ]
    ]
    ok)

    (7 = (1 + 2 |1| 3 + 4))
    ~???~ !! (1 + 2 |1| 3 + 4 5 + 6)
]

(
    void? eval:undecayed [|||]
)
(
    3 = eval [1 + 2 ||| 10 + 20, 100 + 200]
)

; !!! There used to be some concept that these void-returning things could
; appear like an "end" to functions.  But rules for reification have changed,
; in that there are no "pure invisibles".  So saying that it's an <end> is
; questionable.  Review when there's enough time in priorities to think on it.
;
;     (unraised? trap [reeval (lambda [x [<end>]] []) ||| 1 2 3])
;     (error? trap [reeval (lambda [x [~null~]] []) ||| 1 2 3])

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
        left-normal: infix /right-normal: (
            func [return: [~null~ word!] x [word!]] [return x]
        )
        left-normal*: infix /right-normal*: (
            func [return: [~null~ word!] x [word! <end>]] [return x]
        )

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft: (
            func [return: [~null~ word!] @(x) [word!]] [return x]
        )
        left-soft*: infix /right-soft*: (
            func [return: [~null~ word!] @(x) [word! <end>]] [return x]
        )

        left-hard: infix /right-hard: (
            func [return: [~null~ word!] 'x [word!]] [return x]
        )
        left-hard*: infix /right-hard*: (
            func [return: [~null~ word!] 'x [word! <end>]] [return x]
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
            func [return: [~null~ word!] x [word! <variadic>]] [
                return take x
            ]
        left-normal*: infix /right-normal*:
            func [return: [~null~ word!] x [word! <variadic> <end>]] [
                return try take x
            ]

        left-defer: infix:defer left-normal/
        left-defer*: infix:defer left-normal/

        left-soft: infix /right-soft:
            func [return: [~null~ word!] @(x) [word! <variadic>]] [
                return take x
            ]
        left-soft*: infix /right-soft*:
            func [return: [~null~ word!] @(x) [word! <variadic> <end>]] [
                return try take x
            ]

        left-hard: infix /right-hard:
            func [return: [~null~ word!] 'x [word! <variadic>]] [
                return take x
            ]
        left-hard*: infix /right-hard*:
            func [return: [~null~ word!] 'x [word! <variadic> <end>]] [
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

('~[]~ = meta (if ok [] else [<else>]))
('~[]~ = meta (if ok [comment <true-branch>] else [<else>]))

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
    all [
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
        return unmeta ^(vanish-if-odd y + 1)
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

((meta void) = ^ void)

~no-value~ !! (
    1 + 2 (comment "stale") + 3
)

(
    num-runs: 0

    add-period: func [x [<maybe> text!]] [
        num-runs: me + 1
        return append x "."
    ]

    all [
        "Hello World." = add-period "Hello World"
        num-runs = 1
        null = add-period void  ; shouldn't run ADD-PERIOD body
        num-runs = 1
    ]
)
