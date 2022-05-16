; %parse-logic.test.reb
;
; A logic true acts as a no-op, while a logic false causes matches to fail

("b" == uparse "ab" ["a" true "b"])
(didn't uparse "ab" ["a" false "b"])
("b" == uparse "ab" ["a" :(1 = 1) "b"])
(didn't uparse "ab" ["a" :(1 = 2) "b"])

[
    (didn't uparse [] [false])
    (didn't uparse [a] ['a false])
    (didn't uparse [a] [[false]])
    (didn't uparse [a] [false | false])
    (didn't uparse [a] [[false | false]])
    (didn't uparse [a] ['b | false])
]

[
    (didn't uparse "" [false])
    (didn't uparse "a" [#a false])
    (didn't uparse "a" [[false]])
    (didn't uparse "a" [false | false])
    (didn't uparse "a" [[false | false]])
    (didn't uparse "a" [#b | false])
]

[
    (didn't uparse #{} [false])
    (didn't uparse #{0A} [#{0A} false])
    (didn't uparse #{0A} [[false]])
    (didn't uparse #{0A} [false | false])
    (didn't uparse #{0A} [[false | false]])
    (didn't uparse #{0A} [#{0B} | false])
]
