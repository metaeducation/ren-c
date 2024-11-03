; functions/convert/to.r

; Originally discussed in #38, but logic is no longer a fundamental type, so
; the predicate of &logic? turns into logic?
[
    ('logic? = to word! logic?!)
]

; Fundamental datatypes are now `percent!: &percent` and give word with no
; decoration after conversion.
;
('percent = to word! percent!)
('money = to word! money!)

[#1967
    (not same? to blob! [1] to blob! [2])
]

; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
([a b c] = to block! 'a/b/c)
(the (a b c) = to group! 'a/b/c)
([a b c] = to block! the (a b c))
(the (a b c) = to group! [a b c])
(the a/b/c = to path! [a b c])
(the /(a b c) = to path! the (a b c))

; strings and words can TO-convert to ISSUE!
[
    (#x = to issue! 'x)
    (#xx = to issue! 'xx)

    (#x = to issue! "x")
    (#xx = to issue! "xx")

    ; !!! Should this be legal and return `#`?
    ('illegal-zero-byte = pick trap [to issue! ""] 'id)
]
