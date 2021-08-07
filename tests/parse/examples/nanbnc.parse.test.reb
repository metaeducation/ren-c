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

    (uparse? "abc" nanbnc)
    (uparse? "aabbcc" nanbnc)
    (uparse? "aaabbbccc" nanbnc)
    (not uparse? "abbc" nanbnc)
    (not uparse? "abcc" nanbnc)
    (not uparse? "aabbc" nanbnc)
]
