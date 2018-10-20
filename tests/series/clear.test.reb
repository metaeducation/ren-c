; functions/series/clear.r
([] == clear [])
([] == clear copy [1])
(
    block: at copy [1 2 3 4] 3
    clear block
    [1 2] == head of clear block
)
