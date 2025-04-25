; functions/control/remove-each.r
(
    remove-each i s: [1 2] [okay]
    empty? s
)
(
    remove-each i s: [1 2] [null]
    [1 2] = s
)

; BLOCK!
(
    block: copy [1 2 3 4]
    remove-each i block [
        all [i > 1  i < 4]
    ]
    block = [1 4]
)

; !!! REMOVE-EACH currently has no effect on BREAK.  This is because as part
; of the loop protocol, it returns NULL and thus cannot return the number
; of removed elements.
(
    block: copy [1 2 3 4]
    remove-each i block [
        if i = 3 [break]
        okay
    ]
    block = [1 2 3 4]
)
(
    block: copy [1 2 3 4]
    returned-null: null
    remove-each i block [
        if i = 3 [break]
        i = 2
    ] else [
        returned-null: okay
    ]
    all [
        block = [1 2 3 4]
        returned-null = okay
    ]
)
(
    block: copy [1 2 3 4]
    remove-each i block [
        if i = 3 [continue/with okay]
        degrade (if i = 4 ['~okay~] else ['~null~])
    ]
    block = [1 2]
)
(
    block: copy [1 2 3 4]
    sys/util/rescue [
        remove-each i block [
            if i = 3 [fail "midstream failure"]
            okay
        ]
    ]
    block = [3 4]
)
(
    b-was-null: null

    block: copy [1 2 3 4 5]
    remove-each [a b] block [
        if a = 5 [
            b-was-null: null? :b
        ]
    ]
    b-was-null
)

; STRING!
(
    string: copy "1234"
    remove-each i string [
        any [i = #"2"  i = #"3"]
    ]
    string = "14"
)
(
    string: copy "1234"
    returned-null: null
    remove-each i string [
        if i = #"3" [break]
        okay
    ] else [
        returned-null: okay
    ]
    all [
        string = "1234" comment {not changed if BREAK}
        returned-null = okay
    ]
)
(
    string: copy "1234"
    sys/util/rescue [
        remove-each i string [
            if i = #"3" [fail "midstream failure"]
            okay
        ]
    ]
    string = "34"
)
(
    b-was-null: null

    string: copy "12345"
    remove-each [a b] string [
        if a = #"5" [
            b-was-null: null? :b
        ]
    ]
    b-was-null
)

; BINARY!
(
    binary: copy #{01020304}
    remove-each i binary [
        any [i = 2  i = 3]
    ]
    binary = #{0104}
)
(
    binary: copy #{01020304}
    returned-null: null
    remove-each i binary [
        if i = 3 [break]
        okay
    ] else [
        returned-null: okay
    ]
    all [
        binary = #{01020304} comment {Not changed with BREAK}
        returned-null = okay
    ]
)
(
    binary: copy #{01020304}
    sys/util/rescue [
        remove-each i binary [
            if i = 3 [fail "midstream failure"]
            okay
        ]
    ]
    binary = #{0304}
)
(
    b-was-null: null

    binary: copy #{0102030405}
    remove-each [a b] binary [
        if a = 5 [
            b-was-null: null? :b
        ]
    ]
    b-was-null
)
