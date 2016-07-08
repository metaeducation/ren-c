; functions/series/remove.r
[[] = remove []]
[[] = head remove [1]]
; blank
[blank = remove blank]
; bitset
[
    a-bitset: charset "a"
    remove/part a-bitset "a"
    blank? find a-bitset #"a"
]
[
    a-bitset: charset "a"
    remove/part a-bitset to integer! #"a"
    blank? find a-bitset #"a"
]
