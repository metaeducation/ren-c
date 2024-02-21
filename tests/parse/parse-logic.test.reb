; %parse-logic.test.reb
;
; A logic true acts as a no-op, while a logic false causes matches to fail

("b" == parse "ab" ["a" true "b"])
~parse-mismatch~ !! (parse "ab" ["a" false "b"])

("b" == parse "ab" ["a" :(1 = 1) "b"])
~parse-mismatch~ !! (parse "ab" ["a" :(1 = 2) "b"])

[
    ~parse-mismatch~ !! (parse [] [false])
    ~parse-mismatch~ !! (parse [a] ['a false])
    ~parse-mismatch~ !! (parse [a] [[false]])
    ~parse-mismatch~ !! (parse [a] [false | false])
    ~parse-mismatch~ !! (parse [a] [[false | false]])
    ~parse-mismatch~ !! (parse [a] ['b | false])
]

[
    ~parse-mismatch~ !! (parse "" [false])
    ~parse-mismatch~ !! (parse "a" [#a false])
    ~parse-mismatch~ !! (parse "a" [[false]])
    ~parse-mismatch~ !! (parse "a" [false | false])
    ~parse-mismatch~ !! (parse "a" [[false | false]])
    ~parse-mismatch~ !! (parse "a" [#b | false])
]

[
    ~parse-mismatch~ !! (parse #{} [false])
    ~parse-mismatch~ !! (parse #{0A} [#{0A} false])
    ~parse-mismatch~ !! (parse #{0A} [[false]])
    ~parse-mismatch~ !! (parse #{0A} [false | false])
    ~parse-mismatch~ !! (parse #{0A} [[false | false]])
    ~parse-mismatch~ !! (parse #{0A} [#{0B} | false])
]
