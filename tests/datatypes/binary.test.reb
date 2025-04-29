; datatypes/binary.r
(binary? #{00})
(not binary? 1)
(binary! = type of #{00})

(#{00} = 2#{00000000})
(#{000000} = 64#{AAAA})
(#{} = make binary! 0)
(error? sys/util/rescue [transcode {"^^(00)"}])
; minimum
(binary? #{})
; alternative literal representation
(#{} = #[binary! #{}])
; access symmetry
(
    b: #{0b}
    not error? sys/util/rescue [b/1: b/1]
)
[#42 (
    b: #{0b}
    b/1 = 11
)]
; case sensitivity
[#1459
    (lesser? #{0141} #{0161})
]

(
    a: make binary! 0
    insert a #"^(00)"
    a = #{00}
)
