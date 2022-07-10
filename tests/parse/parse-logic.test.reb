; %parse-logic.test.reb
;
; A logic true acts as a no-op, while a logic false causes matches to fail

("b" == parse "ab" ["a" true "b"])
(didn't parse "ab" ["a" false "b"])
("b" == parse "ab" ["a" :(1 = 1) "b"])
(didn't parse "ab" ["a" :(1 = 2) "b"])

[
    (didn't parse [] [false])
    (didn't parse [a] ['a false])
    (didn't parse [a] [[false]])
    (didn't parse [a] [false | false])
    (didn't parse [a] [[false | false]])
    (didn't parse [a] ['b | false])
]

[
    (didn't parse "" [false])
    (didn't parse "a" [#a false])
    (didn't parse "a" [[false]])
    (didn't parse "a" [false | false])
    (didn't parse "a" [[false | false]])
    (didn't parse "a" [#b | false])
]

[
    (didn't parse #{} [false])
    (didn't parse #{0A} [#{0A} false])
    (didn't parse #{0A} [[false]])
    (didn't parse #{0A} [false | false])
    (didn't parse #{0A} [[false | false]])
    (didn't parse #{0A} [#{0B} | false])
]
