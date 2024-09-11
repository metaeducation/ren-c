; functions/series/ordinals.r

(
    for-each ordinal [
        first second third fourth fifth sixth seventh eighth ninth tenth
    ][
        assert compose [null = try ($ ordinal) []]
        assert compose [($ordinal) [] except e -> [e.id = 'bad-pick]]
    ]
    ok
)

(
    for-each [num ordinal] [
        1 first
        2 second
        3 third
        4 fourth
        5 fifth
        6 sixth
        7 seventh
        8 eighth
        9 ninth
        10 tenth
    ][
        assert compose [
            (num) = ($ ordinal) [1 2 3 4 5 6 7 8 9 10 11]
        ]
    ]
    ok
)
