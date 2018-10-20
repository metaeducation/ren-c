; series/rejoin.test.reb
(
    [] == rejoin []
)
(
    [] == rejoin [[]]
)
(
    "" == rejoin [null]
)
(
    "" == rejoin [null null]
)
(
    [[]] == rejoin [[][]]
)
(
    [[][]] == rejoin [[][][]]
)
(
    block: [[][]]
    not same? first block first rejoin block
)
(
    [1] == rejoin [[] 1]
)
(
    'a/b/c == rejoin ['a/b 'c]
)
(
    'a/b/c/d == rejoin ['a/b 'c 'd]
)
(
    "" == rejoin [{}]
)
(
    "1" == rejoin [1]
)
(
    value: 1
    "1" == rejoin [value]
)
