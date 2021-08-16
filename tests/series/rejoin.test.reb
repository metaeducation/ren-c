; series/rejoin.test.reb
;
; REJOIN is deprecated in Ren-C in favor of JOIN-ALL:
;
; https://forum.rebol.info/t/rejoin-ugliness-and-the-usefulness-of-tests/248

(
    [] = rejoin []
)
(
    [] = rejoin [[]]
)
(
    "~null~" = rejoin ["" null]
)
(
    [] = rejoin [null null]
)
(
    [[]] = rejoin [[][]]
)
(
    [[][]] = rejoin [[][][]]
)
(
    block: [[][]]
    not same? first block first rejoin block
)
(
    [1] = rejoin [[] 1]
)
(
    'a/b/c = join-all ['a/b 'c]
)
(
    'a/b/c/d = join-all ['a/b 'c 'd]
)
(
    "" = rejoin [{}]
)
(
    "1" = rejoin [1]
)
(
    value: 1
    "1" = rejoin [value]
)

[
    https://github.com/metaeducation/ren-c/issues/1084
    (#ab = rejoin [#a #b])
]

[
    https://github.com/metaeducation/ren-c/issues/1085
    (#a30bc = join #a [10 + 20 "b" #c])
]
