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
    b: make bitset! [1 2 3]
    empty? clear b
)

; Note: Besides returning an error, this should also not leak...e.g. the
; bitset which is in progress being built needs to be sync'd well enough
; that the malloc()s are handled.
(
    error? trap [
        b: make bitset! [1 - 10 asdf 20 - 30]
    ]
)

((make bitset! [not]) != (make bitset! []))

[
    (did btest: func ['op [word!] 'spec1 'spec2 'eq [word!] 'spec3] [
        let result: reeval op make bitset! spec1 make bitset! spec2
        probe result
        if result != make bitset! spec3 [
            print mold spec1
            print mold spec2
            fail ["MISMATCH: expected" mold spec3]
        ]
        return true
    ])

    (btest intersect [] [] = [])
    (btest intersect [not] [] = [])
    (btest intersect [] [not] = [])
    (btest intersect [not] [not] = [not])

    (btest intersect [1] [1] = [1])
    (btest intersect [not 1] [1] = [])
    (btest intersect [1] [not 1] = [])

    (btest intersect [1 1000000] [1] = [1])
    (btest intersect [1 1000000] [1000000] = [1000000])

    (btest intersect [1 - 5] [not 4] = [1 2 3 5])
]
