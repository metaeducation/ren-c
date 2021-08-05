; %uparse2.test.reb
;
; UPARSE2 should have a more comprehensive test as part of Redbol, but until
; that is done here are just a few basics to make sure it's working at all.

(true = uparse2 "aaa" [some "a"])
(true = uparse2 "aaa" [any "a"])
(true = uparse2 ["aaa"] [into any "a"])
(true = uparse2 "aaabbb" [
    pos: some "a" some "b" :pos some "a" some "b"
])
(
    x: null
    did all [
        true = uparse2 "aaabbbccc" [to "b" copy x to "c" some "c"]
        x = "bbb"
    ]
)
(
    x: null
    did all [
        true = uparse2 "aaabbbccc" [to "b" set x some "b" thru <end>]
        x = #"b"
    ]
)
