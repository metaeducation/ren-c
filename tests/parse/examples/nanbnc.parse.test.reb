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

    (#c = uparse "abc" nanbnc)
    (#c = uparse "aabbcc" nanbnc)
    (#c = uparse "aaabbbccc" nanbnc)
    (didn't uparse "abbc" nanbnc)
    (didn't uparse "abcc" nanbnc)
    (didn't uparse "aabbc" nanbnc)
]
