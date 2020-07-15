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
    void = do [comment "a"]
)

(
    val: <overwritten>
    pos: evaluate/set [1 + comment "a" comment "b" 2 * 3 fail "too far"] 'val
    did all [
        val = 9
        pos = [fail "too far"]
    ]
)
(
    val: <overwritten>
    pos: evaluate/set [1 comment "a" + comment "b" 2 * 3 fail "too far"] 'val
    did all [
        val = 9
        pos = [fail "too far"]
    ]
)
(
    val: <overwritten>
    pos: evaluate/set [1 comment "a" comment "b" + 2 * 3 fail "too far"] 'val
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
    void = do [elide "a"]
)

(
    error? trap [
        evaluate evaluate [1 elide "a" + elide "b" 2 * 3 fail "too far"]
    ]
)
(
    error? trap [
        evaluate evaluate [1 elide "a" elide "b" + 2 * 3 fail "too far"]
    ]
)
(
    pos: evaluate/set [1 + 2 * 3 elide "a" elide "b" fail "too far"] 'val
    did all [
        val = 9
        pos = [elide "a" elide "b" fail "too far"]
    ]
)


(
    x: void
    x: 1 + 2 * 3
    elide (y: :x)

    did all [x = 9 | y = 9]
)
(
    x: void
    x: 1 + elide (y: 10) 2 * 3
    did all [
        x = 9
        y = 10
    ]
)

(
    x: y: z: void

    x: 10
    y: 1 comment [+ 2
    z: 30] + 7

    did all [
        x = 10
        y = 8
        voided? 'z
    ]
)

(
    void = do [end]
)
(
    3 = do [1 + 2 end 10 + 20 | 100 + 200]
)
(
    ok? trap [reeval (func [x [<end>]] []) end 1 2 3]
)
(
    error? trap [reeval (func [x [<opt>]] []) end 1 2 3]
)

(
    [3 11] = reduce [1 + 2 elide 3 + 4 5 + 6]
)


; BAR! is invisible, and acts as an expression barrier

(
    3 = (1 + 2 |)
)(
    3 = (1 + 2 | comment "invisible")
)

; Non-variadic
[
    (
        left-normal: enfix right-normal:
            <- func [return: [<opt> bar!] x [bar!]] [:x]
        left-normal*: enfix right-normal*:
            <- func [return: [<opt> bar!] x [bar! <end>]] [:x]

        left-tight: enfix right-tight:
            <- func [return: [<opt> bar!] #x [bar!]] [:x]
        left-tight*: enfix right-tight*:
            <- func [return: [<opt> bar!] #x [bar! <end>]] [:x]

        left-soft: enfix right-soft:
            <- func [return: [<opt> bar!] 'x [bar!]] [:x]
        left-soft*: enfix right-soft*:
            <- func [return: [<opt> bar!] 'x [bar! <end>]] [:x]

        left-hard: enfix right-hard:
            <- func [return: [<opt> bar!] :x [bar!]] [:x]
        left-hard*: enfix right-hard*:
            <- func [return: [<opt> bar!] :x [bar! <end>]] [:x]

        true
    )

    ('no-arg = (trap [right-normal |])/id)
    (null? do [right-normal* |])
    (null? do [right-normal*])

    ('no-arg = (trap [| left-normal])/id)
    (null? do [| left-normal*])
    (null? do [left-normal*])

    ('no-arg = (trap [right-tight |])/id)
    (null? do [right-tight* |])
    (null? do [right-tight*])

    ('no-arg = (trap [| left-tight])/id)
    (null? do [| left-tight*])
    (null? do [left-tight*])

    ('no-arg = (trap [right-soft |])/id)
    (null? do [right-soft* |])
    (null? do [right-soft*])

    ('no-arg = (trap [| left-soft])/id)
    (null? do [| left-soft*])
    (null? do [left-soft*])

    ('| = do [right-hard |])
    ('| = do [right-hard* |])
    (null? do [right-hard*])

    ('| = do [| left-hard])
    ('| = do [| left-hard*])
    (null? do [left-hard*])
]


; Variadic
[
    (
        left-normal: enfix right-normal:
            <- func [return: [<opt> bar!] x [bar! <...>]] [take x]
        left-normal*: enfix right-normal*:
            <- func [return: [<opt> bar!] x [bar! <...> <end>]] [take x]

        left-tight: enfix right-tight:
            <- func [return: [<opt> bar!] #x [bar! <...>]] [take x]
        left-tight*: enfix right-tight*:
            <- func [return: [<opt> bar!] #x [bar! <...> <end>]] [take x]

        left-soft: enfix right-soft:
            <- func [return: [<opt> bar!] 'x [bar! <...>]] [take x]
        left-soft*: enfix right-soft*:
            <- func [return: [<opt> bar!] 'x [bar! <...> <end>]] [take x]

        left-hard: enfix right-hard:
            <- func [return: [<opt> bar!] :x [bar! <...>]] [take x]
        left-hard*: enfix right-hard*:
            <- func [return: [<opt> bar!] :x [bar! <...> <end>]] [take x]

        true
    )

    (null? trap [right-normal |])
    (null? do [right-normal* |])
    (null? do [right-normal*])

    (null? trap [| left-normal])
    (null? do [| left-normal*])
    (null? do [left-normal*])

    (null? trap [right-tight |])
    (null? do [right-tight* |])
    (null? do [right-tight*])

    (null? trap [| left-tight])
    (null? do [| left-tight*])
    (null? do [left-tight*])

    (null? trap [right-soft |])
    (null? do [right-soft* |])
    (null? do [right-soft*])

    (null? trap [| left-soft])
    (null? do [| left-soft*])
    (null? do [left-soft*])

    ('| = do [right-hard |])
    ('| = do [right-hard* |])
    (null? do [right-hard*])

    ('| = do [| left-hard])
    ('| = do [| left-hard*])
    (null? do [left-hard*])
]

; GROUP!s with no content act as invisible
(
    x: <unchanged>
    did all [
        'no-arg = (trap [<discarded> set 'x ()])/id
        x = <unchanged>
    ]
)(
    x: <unchanged>
    did all [
        'no-arg = (trap [<discarded> set 'x comment "hi"])/id
        x = <unchanged>
    ]
)(
    obj: make object! [x: <unchanged>]
    did all [
        'no-arg = (trap [<discarded> set 'obj/x comment "hi"])/id
        obj/x = <unchanged>
    ]
)(
    obj: make object! [x: <unchanged>]
    did all [
        'no-arg = (trap [<discarded> set 'obj/x ()])/id
        obj/x = <unchanged>
    ]
)

(void? (if true [] else [<else>]))
(void? (if true [comment <true-branch>] else [<else>]))

(1 = all [1 elide <invisible>])
(1 = any [1 elide <invisible>])
([1] = reduce [1 elide <invisible>])

(304 = (1000 + 20 (** foo <baz> (bar)) 300 + 4))
(304 = (1000 + 20 ** (
    foo <baz> (bar)
) 300 + 4))


; It's likely more useful for EVAL to give VOID! than error if asked to
; evaluate something that turns out to be invisible.
;
(void? reeval quote (comment "void is better than failing here"))
(
    x: <before>
    did all [
        void? reeval :elide x: <after>
        x = <after>
    ]
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
