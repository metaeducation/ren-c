; datatypes/binary.r
(binary? #{00})
(not binary? 1)
(binary! = type of #{00})
(
    system.options.binary-base: 2
    "2#{00000000}" == mold #{00}
)
(
    system.options.binary-base: 64
    "64#{AAAA}" == mold #{000000}
)
(
    system.options.binary-base: 16
    "#{00}" == mold #{00}
)
(#{00} == 2#{00000000})
(#{000000} == 64#{AAAA})
(#{} == make binary! 0)
; minimum
(binary? #{})
; alternative literal representation
(#{} == #[binary! #{}])
; access symmetry
(
    b: #{0b}
    not error? trap [b.1: b.1]
)
[#42 (
    b: #{0b}
    b.1 == 11
)]
; case sensitivity
[#1459
    (lesser? #{0141} #{0161})
]

(
    a: make binary! 0
    insert a make char! 0
    a == #{00}
)

('bad-pick = pick trap [pick #{00} 'x] 'id)


[#1791
    (#{E188B4} = head insert #{} "^(1234)")
    (#{E188B400} = head insert #{00} "^(1234)")
    (#{E188B40000} = head insert #{0000} "^(1234)")

    (#{E188B4} = append #{} "^(1234)")
    (#{00E188B4} = append #{00} "^(1234)")
    (#{0000E188B4} = append #{0000} "^(1234)")

    (#{E188B4} = head change #{} "^(1234)")
    (#{E188B4} = head change #{00} "^(1234)")
    (#{E188B4} = head change #{0000} "^(1234)")
]
