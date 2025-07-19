; functions/convert/to.r

('percent! = to word! percent!)
('money! = to word! money!)
[#1967
    (not same? to blob! [1] to blob! [2])
]

; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
([a/b/c] = to block! 'a/b/c)
(the (a/b/c) = to group! 'a/b/c)
([a b c] = to block! the (a b c))
(the (a b c) = to group! [a b c])
(the a/b/c = to path! [a b c])
(the a/b/c = to path! the (a b c))
