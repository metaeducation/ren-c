; datatypes/binary.r
(blob? #{00})
(not blob? 1)
(blob! = type of #{00})
(#{00} = 2#{00000000})
(#{000000} = 64#{AAAA})
(#{} = make blob! 0)
; minimum
(blob? #{})
; alternative literal representation
; access symmetry
(
    b: #{0b}
    null? rescue [b.1: b.1]
)
[#42 (
    b: #{0b}
    b.1 = 11
)]
; case sensitivity
[#1459
    (lesser? #{0141} #{0161})
]

(
    a: make blob! 0
    insert a make-char 0
    a = #{00}
)

~bad-pick~ !! (pick #{00} 'x)


[#1791
    (#{E188B4} = head of insert #{} "^(1234)")
    (#{E188B400} = head of insert #{00} "^(1234)")
    (#{E188B40000} = head of insert #{0000} "^(1234)")

    (#{E188B4} = append #{} "^(1234)")
    (#{00E188B4} = append #{00} "^(1234)")
    (#{0000E188B4} = append #{0000} "^(1234)")

    (#{E188B4} = head of change #{} "^(1234)")
    (#{E188B4} = head of change #{00} "^(1234)")
    (#{E188B4} = head of change #{0000} "^(1234)")
]
