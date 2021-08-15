; datatypes/bitset.r
(bitset? make bitset! "a")
(not bitset? 1)
(bitset! = type of make bitset! "a")
; minimum, literal representation
(bitset? #[bitset! #{}])
; TS crash
(bitset? charset ensure block! transcode {#"^(A0)"})

(" aa" = find "aa aa" make bitset! [1 - 32])
("a  " = find "  a  " make bitset! [not 1 - 32])


[https://github.com/metaeducation/ren-c/issues/825 (
    cs: charset [#"^(FFFE)" - #"^(FFFF)"]
    all [
        find cs #"^(FFFF)"
        find cs #"^(FFFE)"
        not find cs #"^(FFFD)"
    ]
)(
    cs: charset [#"^(FFFF)" - #"^(FFFF)"]
    all [
        find cs #"^(FFFF)"
        not find cs #"^(FFFE)"
        not find cs #"^(FFFD)"
    ]
)]
