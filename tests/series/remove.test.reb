; functions/series/remove.r
([] == remove [])
([] == head of remove [1])

; bitset
(
    a-bitset: charset "a"
    remove/part a-bitset "a"
    null? find a-bitset #"a"
)
(
    a-bitset: charset "a"
    remove/part a-bitset to integer! #"a"
    null? find a-bitset #"a"
)

(
    1 == take copy #{010203}
)(
    3 == take/last copy #{010203}
)(
    #{0102} == take/part copy #{010203} 2
)(
    #{0203} == take/part copy next #{010203} 100 ;-- should clip
)

(
    #"a" == take copy "abc"
)(
    #"c" == take/last copy "abc"
)(
    "ab" == take/part copy "abc" 2
)(
    "bc" == take/part copy next "abc" 100 ;-- should clip
)
