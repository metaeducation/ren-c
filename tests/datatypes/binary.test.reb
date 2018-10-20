; datatypes/binary.r
(binary? #{00})
(not binary? 1)
(binary! == type of #{00})
(
    system/options/binary-base: 2
    "2#{00000000}" == mold #{00}
)
(
    system/options/binary-base: 64
    "64#{AAAA}" == mold #{000000}
)
(
    system/options/binary-base: 16
    "#{00}" == mold #{00}
)
(#{00} == 2#{00000000})
(#{000000} == 64#{AAAA})
(#{} == make binary! 0)
(#{00} == to binary! "^(00)")
; minimum
(binary? #{})
; alternative literal representation
(#{} == #[binary! #{}])
; access symmetry
(
    b: #{0b}
    not error? trap [b/1: b/1]
)
[#42 (
    b: #{0b}
    b/1 == 11
)]
; case sensitivity
[#1459
    (lesser? #{0141} #{0161})
]

(
    a: make binary! 0
    insert a #"^(00)"
    a == #{00}
)
