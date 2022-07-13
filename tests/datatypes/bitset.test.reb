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

(
    bs: make bitset! 8
    for i 8 [
        assert [bs.(i) = null]
        assert [not find bs i]
        bs.(i): true
    ]
    for i 8 [
        assert [bs.(i) = true]
    ]
    true
)

; Generically speaking, R3-Alpha's BITSET! negation concept did not work.  It
; lacked the implementation details needed to do set operations.  A branch
; which introduces use of the "Roaring Bitset" C library solves this, but it
; is somewhat of a detour to be taking on when major language questions remain
; unresolved--so that branch has yet to be integrated.
;
; But %pdf-maker.r uses a fairly trivial EXCLUDE operation to make work:
;
;    chset: exclude make bitset! chset (
;        wrp: union wrappers: txtb/wrappers charset " ^/"
;    )
;    invalid: exclude complement chset wrp
;
; Since every bit in a complemented bitset is actually *not* in the bitset,
; then excluding more things from it in a non-negated set really just means
; adding those things to the underlying binary.  The exception is made to
; do that one trick.
(
    bs: make bitset! [1 2 3]
    invalid: exclude (complement bs) make bitset! [4 5]
    for i 8 [
        if i <= 5 [
            assert [not find invalid i]
        ] else [
            assert [find invalid i]
        ]
    ]
    true
)
