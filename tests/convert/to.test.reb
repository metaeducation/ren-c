; functions/convert/to.r
[#38
    ('logic! = to word! logic!)
]
('percent! = to word! percent!)
('money! = to word! money!)
[#1967
    (not same? to binary! [1] to binary! [2])
]

; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
([a b c] = to block! 'a/b/c)
(lit (a b c) = to group! 'a/b/c)
([a b c] = to block! lit (a b c))
(lit (a b c) = to group! [a b c])
(lit a/b/c = to path! [a b c])
(lit a/b/c = to path! lit (a b c))

; strings and words can TO-convert to ISSUE!
[
    (#x = to issue! 'x)
    (#xx = to issue! 'xx)

    (#x = to issue! "x")
    (#xx = to issue! "xx")

    ; !!! Should this be legal and return `#`?
    ('illegal-zero-byte = pick trap [to issue! ""] 'id)
]
