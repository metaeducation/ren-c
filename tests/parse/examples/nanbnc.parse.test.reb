; %nanbnc.parse.test.reb
;
; taken from http://www.rebol.net/wiki/Parse_Project#AND

[
    (
        nanb: [#a opt nanb #b]
        nbnc: [#b opt nbnc #c]
        nanbnc: [ahead [nanb #c] some #a nbnc]
        ok
    )

    (#c = parse "abc" nanbnc)
    (#c = parse "aabbcc" nanbnc)
    (#c = parse "aaabbbccc" nanbnc)

    ~parse-mismatch~ !! (parse "abbc" nanbnc)
    ~parse-incomplete~ !! (parse "abcc" nanbnc)
    ~parse-mismatch~ !! (parse "aabbc" nanbnc)
]
