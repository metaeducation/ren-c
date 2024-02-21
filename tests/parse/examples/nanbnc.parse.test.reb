; %nanbnc.parse.test.reb
;
; taken from http://www.rebol.net/wiki/Parse_Project#AND

[
    (
        nanb: [#a try nanb #b]
        nbnc: [#b try nbnc #c]
        nanbnc: [ahead [nanb #c] some #a nbnc]
        true
    )

    (#c = parse "abc" nanbnc)
    (#c = parse "aabbcc" nanbnc)
    (#c = parse "aaabbbccc" nanbnc)

    ~parse-mismatch~ !! (parse "abbc" nanbnc)
    ~parse-incomplete~ !! (parse "abcc" nanbnc)
    ~parse-mismatch~ !! (parse "aabbc" nanbnc)
]
