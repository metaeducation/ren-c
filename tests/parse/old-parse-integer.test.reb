; %parse-integer.test.reb
;
; INTEGER! has some open questions regarding how it is not very visible when
; used as a rule...that maybe it should need a keyword.  Ranges are not worked
; out in terms of how to make [2 4 rule] range between 2 and 4 occurrences,
; as that breaks the combinator pattern at this time.

(trash? parse "" [0 <any>])
("a" == parse "a" [1 "a"])
("a" == parse "aa" [2 "a"])

; Plain loops that never actually runs the body gives back a match that is
; a void, as do 0-iteration REPEAT and INTEGER! rules.
[
    ("a" = parse "a" ["a" 0 "b"])
    (
        x: ~
        all [
            void' = parse "a" ["a" x: ^[0 "b"]]
            void' = x
        ]
    )

    ("a" = parse "a" ["a" maybe/ 0 "b"])
    ("a" = parse "a" ["a" maybe/ [0 "b"]])
]

[#1280 (
    parse "" [(i: 0) 3 [["a" |] (i: i + 1)]]
    i == 3
)]

[https://github.com/red/red/issues/4591
    (trash? parse [] [0 [ignore me]])
    (trash? parse [] [0 "ignore me"])
    (trash? parse [] [0 0 [ignore me]])
    (trash? parse [] [0 0 "ignore me"])
    (raised? parse [x] [0 0 'x])
    (raised? parse " " [0 0 space])
]

[https://github.com/red/red/issues/564
    (raised? parse "a" [0 <any>])
    (#a == parse "a" [0 <any> #a])
    (
        z: ~
        all [
            raised? parse "a" [z: across 0 <any>]
            z = ""
        ]
    )
]

[https://github.com/red/red/issues/564
    (raised? parse [a] [0 <any>])
    ('a == parse [a] [0 <any> 'a])
    (
        z: ~
        all [
            raised? parse [a] [z: across 0 <any>]
            z = []
        ]
    )
]

[
    (raised? parse [a a] [1 ['a]])
    ('a == parse [a a] [2 ['a]])
    (raised? parse [a a] [3 ['a]])

    (raised? parse [a a] [1 'a])
    ('a == parse [a a] [2 'a])
    (raised? parse [a a] [3 'a])
    (raised? parse [a a] [1 1 'a])  ; synonym for [1 [1 'a]] in UPARSE
]

[
    ('b == parse [a a b b] [2 'a 2 'b])
    (raised? parse [a a b b] [2 'a 3 'b])
]
