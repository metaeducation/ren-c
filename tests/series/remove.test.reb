; functions/series/remove.r
[
    ([] = remove [])
    ([] = head of remove [1])
    ([2] = head of remove [1 2])
]

[
    ("" = remove "")
    ("" = head of remove "1")
    ("2" = head of remove "12")
]

[
    (#{} = remove #{})
    (#{} = head of remove #{01})
    (#{02} = head of remove #{0102})
]

; bitset
(
    a-bitset: charset "a"
    remove/part a-bitset "a"
    null? find a-bitset #"a"
)
(
    a-bitset: charset "a"
    remove/part a-bitset as integer! #"a"
    null? find a-bitset #"a"
)

[
    (1 = take #{010203})
    (#{01} = take/part #{010203} 1)  ; always a series
    (3 = take/last #{010203})
    (#{0102} = take/part #{010203} 2)
    (#{0203} = take/part next #{010203} 100)  ; should clip

    (#"a" = take "abc")
    ("a" = take/part "abc" 1)  ; always a series
    (#"c" = take/last "abc")
    ("ab" = take/part "abc" 2)
    ("bc" = take/part next "abc" 100)  ; should clip

    ('a = take [a b c])
    ([a] = take/part [a b c] 1)  ; always a series
    ('c = take/last [a b c])
    ([a b] = take/part [a b c] 2)
    ([b c] = take/part next [a b c] 100)  ; should clip
]

; UTF-8 Removals in binary alias should not allow bad strings
[
    (
        str: "Tæke Pært"
        bin: as binary! str
        true
    )

    ('bad-utf8-bin-edit = pick trap [take/part bin 2] 'id)
    (str = {Tæke Pært})

    ((as binary! "Tæ") = take/part bin 3)
    (str = "ke Pært")

    ('bad-utf8-bin-edit = pick trap [take/part bin 5] 'id)
    (str = "ke Pært")

    ((as binary! "ke Pæ") = take/part bin 6)
    (str = "rt")
]
