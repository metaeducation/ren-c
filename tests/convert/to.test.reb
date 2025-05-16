; functions/convert/to.r


; Fundamental datatypes are now `percent!: ~{percent~}` and give word with no
; decoration after conversion.
;
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
(the a/b/c = join path! @[a b c])
(the /(a b c) = join path! [_ the (a b c)])

; strings and words can TO-convert to RUNE!
[
    (#x = to rune! 'x)
    (#xx = to rune! 'xx)

    (#x = to rune! "x")
    (#xx = to rune! "xx")

    ; !!! Should this be legal and return `#`?
    ('illegal-zero-byte = pick trap [to rune! ""] 'id)
]
