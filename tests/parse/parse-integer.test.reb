; %parse-integer.test.reb
;
; INTEGER! has some open questions regarding how it is not very visible when
; used as a rule...that maybe it should need a keyword.  Ranges are not worked
; out in terms of how to make [2 4 rule] range between 2 and 4 occurrences,
; as that breaks the combinator pattern at this time.

(none? uparse "" [0 <any>])
("a" == uparse "a" [1 "a"])
("a" == uparse "aa" [2 "a"])

; A WHILE that never actually has a succeeding rule gives back a match that is
; a ~none~ isotope, as do 0-iteration REPEAT and INTEGER! rules.
[
    (none? uparse "a" ["a" 0 "b"])
    ('~none~ == uparse "a" ["a" ^[0 "b"]])

    ("a" = uparse "a" ["a" none-to-void/ 0 "b"])
    ("a" = uparse "a" ["a" none-to-void/ [0 "b"]])
]

[#1280 (
    uparse "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]

[https://github.com/red/red/issues/4591
    (none? uparse [] [0 [ignore me]])
    (none? uparse [] [0 "ignore me"])
    (none? uparse [] [0 0 [ignore me]])
    (none? uparse [] [0 0 "ignore me"])
    (didn't uparse [x] [0 0 'x])
    (didn't uparse " " [0 0 space])
]

[https://github.com/red/red/issues/564
    (didn't uparse "a" [0 <any>])
    (#a == uparse "a" [0 <any> #a])
    (
        z: ~
        did all [
            didn't uparse "a" [z: across 0 <any>]
            z = ""
        ]
    )
]

[https://github.com/red/red/issues/564
    (didn't uparse [a] [0 <any>])
    ('a == uparse [a] [0 <any> 'a])
    (
        z: ~
        did all [
            didn't uparse [a] [z: across 0 <any>]
            z = []
        ]
    )
]

[
    (didn't uparse [a a] [1 ['a]])
    ('a == uparse [a a] [2 ['a]])
    (didn't uparse [a a] [3 ['a]])

    (didn't uparse [a a] [1 'a])
    ('a == uparse [a a] [2 'a])
    (didn't uparse [a a] [3 'a])
    (didn't uparse [a a] [1 1 'a])  ; synonym for [1 [1 'a]] in UPARSE
]

[
    ('b == uparse [a a b b] [2 'a 2 'b])
    (didn't uparse [a a b b] [2 'a 3 'b])
]
