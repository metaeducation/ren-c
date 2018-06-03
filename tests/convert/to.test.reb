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
([a/b/c] = to block! 'a/b/c)
(quote (a/b/c) = to group! 'a/b/c)
([a b c] = to block! quote (a b c))
(quote (a b c) = to group! [a b c])
(quote a/b/c = to path! [a b c])
(quote a/b/c = to path! quote (a b c))
