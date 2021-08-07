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

[
    (
        bs: charset ["hello" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (uparse? "abc" [some bs])
    (not uparse? "123" [some bs])
    (not uparse? "ABC" [some bs])
    (uparse? "abc" [some [bs]])
    (not uparse? "123" [some [bs]])
    (uparse? "abc" [some wbs])
    (not uparse? "123" [some wbs])
    (uparse? "abc" [some wbs2])
    (not uparse? "123" [some wbs2])
    (uparse? "abc" [bs bs bs])
    (not uparse? "123" [bs bs bs])
    (uparse? "abc" [[bs] [bs] [bs]])
    (not uparse? "123" [[bs] [bs] [bs]])
    (uparse? "abc" [wbs wbs wbs])
    (not uparse? "123" [wbs wbs wbs])
    (uparse? "abc" [wbs2 wbs2 wbs2])
    (not uparse? "123" [wbs2 wbs2 wbs2])
]

[
    (
        bs: charset [not "hello123" #a - #z]
        wbs: [bs]
        wbs2: reduce wbs
        true
    )
    (not uparse? "abc" [some bs])
    (uparse? "ABC" [some bs])
    (not uparse? "123" [some bs])
    (uparse? "789" [some bs])
    (not uparse? "abc" [bs bs bs])
    (uparse? "ABC" [bs bs bs])
    (not uparse? "123" [bs bs bs])
    (uparse? "789" [bs bs bs])
    (
        digit: charset "0123456789"
        did all [
            uparse? "hello 123" [to digit p: <here> 3 <any>]
            p = "123"
        ]
    )
]
