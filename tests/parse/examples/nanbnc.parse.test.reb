; %nanbnc.parse.test.reb
;
; taken from http://www.rebol.net/wiki/Parse_Project#AND

[
    (
        nanb: [#a opt nanb #b]
        nbnc: [#b opt nbnc #c]
        nanbnc: [ahead [nanb #c] some #a nbnc]
        true
    )

    (#c = parse "abc" nanbnc)
    (#c = parse "aabbcc" nanbnc)
    (#c = parse "aaabbbccc" nanbnc)
    (didn't parse "abbc" nanbnc)
    (didn't parse "abcc" nanbnc)
    (didn't parse "aabbc" nanbnc)
]
