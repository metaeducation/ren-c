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
    remove:part a-bitset "a"
    null? select a-bitset #"a"
)
(
    a-bitset: charset "a"
    remove:part a-bitset as integer! #"a"
    null? select a-bitset #"a"
)

[
    (1 = take #{010203})
    (#{01} = take:part #{010203} 1)  ; always a series
    (3 = take:last #{010203})
    (#{0102} = take:part #{010203} 2)
    (#{0203} = take:part next #{010203} 100)  ; should clip

    (#"a" = take "abc")
    ("a" = take:part "abc" 1)  ; always a series
    (#"c" = take:last "abc")
    ("ab" = take:part "abc" 2)
    ("bc" = take:part next "abc" 100)  ; should clip

    ('a = take [a b c])
    ([a] = take:part [a b c] 1)  ; always a series
    ('c = take:last [a b c])
    ([a b] = take:part [a b c] 2)
    ([b c] = take:part next [a b c] 100)  ; should clip
]


; Types should match when you TAKE:PART from a List
[
    (
        group: '(a b c d e f)
        all [
            '(a b c) = take:part group 3
            group = '(d e f)
        ]
    )
    (
        block: [a b c d e f]
        all [
            [a b c] = take:part block 3
            block = [d e f]
        ]
    )
]

; UTF-8 Removals in binary alias should not allow bad strings
[
    (
        str: "Tæke Pært"
        bin: as blob! str
        ok
    )

    ~bad-utf8-bin-edit~ !! (pick rescue [take:part bin 2])
    (str = "Tæke Pært")

    ((as blob! "Tæ") = take:part bin 3)
    (str = "ke Pært")

    ~bad-utf8-bin-edit~ !! (pick rescue [take:part bin 5])
    (str = "ke Pært")

    ((as blob! "ke Pæ") = take:part bin 6)
    (str = "rt")
]

; Negative :PART should work the same as positive :PART
[
    (
        string: "abcdef"
        remove:part (skip string 4) (skip string 2)
        string = "abef"
    )
    (
        string: "abcdef"
        remove:part (skip string 2) (skip string 4)
        string = "abef"
    )

    (
        block: [a b c d e f]
        remove:part (skip block 4) (skip block 2)
        block = [a b e f]
    )
    (
        block: [a b c d e f]
        remove:part (skip block 2) (skip block 4)
        block = [a b e f]
    )

    (
        binary: #{ABCDEF}
        remove:part (skip binary 2) (skip binary 1)
        binary = #{ABEF}
    )
    (
        binary: #{ABCDEF}
        remove:part (skip binary 1) (skip binary 2)
        binary = #{ABEF}
    )
]
