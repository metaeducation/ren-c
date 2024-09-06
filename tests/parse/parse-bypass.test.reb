; %parse-bypass.test.reb
;
; This is an implementation of what historical Redbol calls FAIL.
;
; It skips to the next alternate rule.

; BLOCK!
[
    ~parse-mismatch~ !! (parse [] [bypass])
    ~parse-mismatch~ !! (parse [a] ['a bypass])
    ~parse-mismatch~ !! (parse [a] [[bypass]])
    ~parse-mismatch~ !! (parse [a] [bypass | bypass])
    ~parse-mismatch~ !! (parse [a] [[bypass | bypass]])
    ~parse-mismatch~ !! (parse [a] ['b | bypass])
]

; TEXT!
[
    ~parse-mismatch~ !! (parse "" [bypass])
    ~parse-mismatch~ !! (parse "a" [#a bypass])
    ~parse-mismatch~ !! (parse "a" [[bypass]])
    ~parse-mismatch~ !! (parse "a" [bypass | bypass])
    ~parse-mismatch~ !! (parse "a" [[bypass | bypass]])
    ~parse-mismatch~ !! (parse "a" [#b | bypass])
]

; BINARY!
[
    ~parse-mismatch~ !! (parse #{} [bypass])
    ~parse-mismatch~ !! (parse #{0A} [#{0A} bypass])
    ~parse-mismatch~ !! (parse #{0A} [[bypass]])
    ~parse-mismatch~ !! (parse #{0A} [bypass | bypass])
    ~parse-mismatch~ !! (parse #{0A} [[bypass | bypass]])
    ~parse-mismatch~ !! (parse #{0A} [#{0B} | bypass])
]
