; functions/control/remove-each.r
(
    remove-each i s: [1 2] [true]
    empty? s
)
(
    remove-each i s: [1 2] [false]
    [1 2] = s
)

; BLOCK!
(
    block: copy [1 2 3 4]
    remove-each i block [
        all [i > 1 | i < 4]
    ]
    block = [1 4]
)
(
    block: copy [1 2 3 4]
    remove-each i block [
        if i = 3 [break]
        true
    ]
    block = [3 4]
)
(
    block: copy [1 2 3 4]
    remove-each i block [
        if i = 3 [break/with true]
        false
    ]
    block = [1 2 4]
)
(
    block: copy [1 2 3 4]
    remove-each i block [
        if i = 3 [continue/with true]
        if i = 4 [true] else [false]
    ]
    block = [1 2]
)
(
    block: copy [1 2 3 4]
    trap [
        remove-each i block [
            if i = 3 [fail "midstream failure"]
            true
        ]
    ]
    block = [3 4]
)
(
    b-was-void: false

    block: copy [1 2 3 4 5]
    remove-each [a b] block [
        if a = 5 [
            b-was-void: void? :b
        ]
    ]
    b-was-void
)

; STRING!
(
    string: copy "1234"
    remove-each i string [
        any [i = #"2" | i = #"3"]
    ]
    string = "14"
)
(
    string: copy "1234"
    remove-each i string [
        if i = #"3" [break]
        true
    ]
    string = "34"
)
(
    string: copy "1234"
    trap [
        remove-each i string [
            if i = #"3" [fail "midstream failure"]
            true
        ]
    ]
    string = "34"
)
(
    b-was-void: false

    string: copy "12345"
    remove-each [a b] string [
        if a = #"5" [
            b-was-void: void? :b
        ]
    ]
    b-was-void
)

; BINARY!
(
    binary: copy #{01020304}
    remove-each i binary [
        any [i = 2 | i = 3]
    ]
    binary = #{0104}
)
(
    binary: copy #{01020304}
    remove-each i binary [
        if i = 3 [break]
        true
    ]
    binary = #{0304}
)
(
    binary: copy #{01020304}
    trap [
        remove-each i binary [
            if i = 3 [fail "midstream failure"]
            true
        ]
    ]
    binary = #{0304}
)
(
    b-was-void: false

    binary: copy #{0102030405}
    remove-each [a b] binary [
        if a = 5 [
            b-was-void: void? :b
        ]
    ]
    b-was-void
)
