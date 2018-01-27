; COMMENT is *mostly* invisible, but interrupts evaluator order for simplicity
; https://trello.com/c/dWQnsspG

[
    1 = do [comment "a" 1]
][
    1 = do [1 comment "a"]
][
    () = do [comment "a"]
]

[
    pos: _
    do/next [1 + comment "a" comment "b" 2 * 3 fail "didn't stop"] 'pos
    pos = [fail "didn't stop"]
][
    pos: _
    val: do/next [1 comment "a" + comment "b" 2 * 3 fail "didn't stop"] 'pos
    did all [
        val = 1
        pos = [+ comment "b" 2 * 3 fail "didn't stop"]
    ]
][
    pos: _
    val: do/next [1 comment "a" comment "b" + 2 * 3 fail "didn't stop"] 'pos
    did all [
        val = 1
        pos = [+ 2 * 3 fail "didn't stop"] 'pos
    ]
]

; ELIDE is fully invisible, but slaved to the evaluator order
; https://trello.com/c/snnG8xwW

[
    1 = do [elide "a" 1]
][
    1 = do [1 elide "a"]
][
    () = do [elide "a"]
]

[
    pos: _
    do/next [1 + comment "a" comment "b" 2 * 3 fail "didn't stop"] 'pos
    pos = [fail "didn't stop"]
][
    pos: _
    do/next [1 elide "a" + elide "b" 2 * 3 fail "didn't stop"] 'pos
    pos = [fail "didn't stop"]
][
    pos: _
    do/next [1 elide "a" elide "b" + 2 * 3 fail "didn't stop"] 'pos
    pos = [fail "didn't stop"]
]

[
    unset 'x
    x: 1 + 2 * 3 elide (y: :x)
    did all [
        x = 9
        not set? 'y
    ]
][
    unset 'x
    x: 1 + elide (y: 10) 2 * 3
    did all [
        x = 9
        y = 10
    ]
]

[
    unset 'x
    unset 'y
    unset 'z

    x: 10
    y: 1 elide [+ 2
    z: 30] + 7

    did all [
        x = 10
        y = 8
        not set? 'z
    ]
]
