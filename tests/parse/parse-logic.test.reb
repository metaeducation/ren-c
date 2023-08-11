; %parse-logic.test.reb
;
; A logic true acts as a no-op, while a logic false causes matches to fail

("b" == parse "ab" ["a" true "b"])
(raised? parse "ab" ["a" false "b"])
("b" == parse "ab" ["a" :(1 = 1) "b"])
(raised? parse "ab" ["a" :(1 = 2) "b"])

[
    (raised? parse [] [false])
    (raised? parse [a] ['a false])
    (raised? parse [a] [[false]])
    (raised? parse [a] [false | false])
    (raised? parse [a] [[false | false]])
    (raised? parse [a] ['b | false])
]

[
    (raised? parse "" [false])
    (raised? parse "a" [#a false])
    (raised? parse "a" [[false]])
    (raised? parse "a" [false | false])
    (raised? parse "a" [[false | false]])
    (raised? parse "a" [#b | false])
]

[
    (raised? parse #{} [false])
    (raised? parse #{0A} [#{0A} false])
    (raised? parse #{0A} [[false]])
    (raised? parse #{0A} [false | false])
    (raised? parse #{0A} [[false | false]])
    (raised? parse #{0A} [#{0B} | false])
]
