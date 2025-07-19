; datatypes/binary.r
(blob? #{00})
(not blob? 1)
(blob! = type of #{00})

(#{00} = 2#{00000000})
(#{000000} = 64#{AAAA})
(#{} = make blob! 0)
(error? sys/util/rescue [transcode {"^^(00)"}])
; minimum
(blob? #{})
; alternative literal representation
(#{} = #[blob! #{}])
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
    a: make blob! 0
    insert a #"^(00)"
    a = #{00}
)
