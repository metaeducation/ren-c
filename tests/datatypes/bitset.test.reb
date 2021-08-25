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


; Taken from:
; https://github.com/red/red/blob/master/tests/source/units/bitset-test.red


=== MOLD ===

@disable [
    ("make bitset! #{00}" = mold make bitset! 1)
    ("make bitset! #{00}" = mold charset "")
    ("make bitset! #{00}" = mold charset [])
    ("make bitset! #{80}" = mold charset #"^(00)")
    ("make bitset! #{40}" = mold charset #"^(01)")
    ("make bitset! #{000000000000FFC0}" = mold charset "0123456789")
    ("make bitset! #{F0}" = mold charset [0 1 2 3])

    ("make bitset! #{FF800000FFFF8000048900007FFFFFE0}"
            = mold charset [#"a" - #"z" 0 - 8 32 - 48 "HELLO"])
]

=== PICK ===

[
    (bs: make bitset! [0 1 2 3], true)

    ; !!! Red returns the "capacity" of a bitset as the length, while Ren-C
    ; returns the cardinality of the set (number of items in it)
    ;
    (4 = length of bs)

    (true = pick bs 0)
    (true = pick bs 1)
    (true = pick bs 2)
    (true = pick bs 3)

    ; !!! Red returns #[false] for when a bit isn't in a bitset, in PICK.
    ; Ren-C uses NULL.  It may be that # should be used for found, in order to
    ; avoid giving the false impression that it always returns a LOGIC!...or
    ; to switch to using logic?
    ;
    (null = pick bs 4)
    (null = pick bs 256)
    (null = pick bs 257)
    (null = pick bs 2147483647)

    ; !!! Red returns false for negative number picks
    ;
    (e: trap [pick bs -2147483648], e.id = 'out-of-range)

    (true = bs.0)
    (true = bs.1)
    (true = bs.2)
    (true = bs.3)
    (null = bs.4)
    (null = bs.256)
    (null = bs.257)

    (e: trap [bs.-2147483648], e.id = 'out-of-range)
]

[
    (bs: make bitset! [0 1 2 3])

    (8 = length? bs)
    (true = pick bs #"^(00)")
    (true = pick bs #"^(01)")
    (true = pick bs #"^(02)")
    (true = pick bs #"^(03)")
    (false = pick bs #"^(04)")
    (false = pick bs #"^(0100)")
    (false = pick bs #"^(0101)")
]

[
    (bs: make bitset! [0100h 0102h])

    (264 = length? bs)
    (true = pick bs 0100h)
    (false = pick bs 0101h)
    (true = pick bs 0102h)
]

[
    (bs: make bitset! [255 257])

    (264 = length? bs)
    (true  = pick bs 255)
    (false = pick bs 256)
    (true  = pick bs 257)
]

[
    (bs: make bitset! [255 256])

    (264 = length? bs)
    (true = pick bs 255)
    (true = pick bs 256)
]

[
    (bs: make bitset! [00010000h])

    (65544 = length? bs)
    (true = pick bs 00010000h)
]

=== POKE ===

[
    (bs: make bitset! 9)

    (16 = length? bs)

    (bs/7: yes)

    (bs/7 = true)
    (bs/8 = false)

    (bs/8: yes)

    (bs/8 = true)
    (bs/9 = false)
]

[
    (bs: make bitset! 8)

    (8 = length? bs)

    (bs/7: yes)

    (bs/7 = true)
    (bs/8 = false)

    (bs/8: yes)

    (16 = length? bs)
    (bs/8 = true)
    (bs/9 = false)
]

[
    (bs: make bitset! [0 1 2 3]
    poke bs 4 true
    true)

    (true = pick bs 0)
    (true = pick bs 1)
    (true = pick bs 2)
    (true = pick bs 3)
    (true = pick bs 4)
    (false = pick bs 5)
]

[
    (bs: make bitset! [0 1 2 3], true)

    (true = pick bs 0)

    (poke bs 0 false
    false = pick bs 0)

    (poke bs 0 true
    true = pick bs 0)

    (poke bs 0 none
    false = pick bs 0)

    (bs/0: yes
    bs/0 = true)

    (bs/0: no
    bs/0 = false)

    (bs/0: yes
    bs/0 = true)

    (bs/0: none
    bs/0 = false)
]

[
    (bs: make bitset! 8)

    (8 = length? bs)

    (append bs ["hello" #"x" - #"z"]
    "make bitset! #{000000000000000000000000048900E0}" = mold bs)

    (clear bs
    "make bitset! #{00000000000000000000000000000000}" = mold bs
]

[
    (bs: charset "^(00)^(01)^(02)^(03)^(04)^(05)^(06)^(07)", true)

    (8 = length? bs)

    ("make bitset! #{FF}" = mold bs)

    (clear bs
    "make bitset! #{00}" = mold bs)
]

[
    (bs: charset "012345789", true)

    (64 = length? bs)
    ("make bitset! #{000000000000FDC0}" = mold bs)
    ("make bitset! #{0000000000007DC0}" = mold remove/key bs #"0")
    ("make bitset! #{0000000000003DC0}" = mold remove/key bs 49)
    ("make bitset! #{0000000000000000}" = mold remove/key bs [#"2" - #"7" "8" #"9"])
]

=== UNION ===

[
    (c1: charset "0123456789"
    c2: charset [#"a" - #"z"]
    u: "make bitset! #{000000000000FFC0000000007FFFFFE0}")

    (u = mold union c1 c2)
    (u = mold union c2 c1)
]

[
    (nd: charset [not #"0" - #"9"]
    zero: charset #"0"
    nd-zero: union nd zero
    true)

    (not find nd #"0")
    (not find nd #"1")
    (find nd #"B")
    (find nd #"}")

    (find zero #"0")
    (not find zero #"1")
    (not find zero #"B")
    (not find zero #"}")

    (find nd-zero #"0")
    (not find nd-zero #"1")
    (find nd-zero #"B")
    (find nd-zero #"}")
]

=== INTERSECT ===

[
    (c1: charset "b"
    c2: charset "1"
    u: "make bitset! #{00000000000000}")

    (u = mold c1 and c2)
    (u = mold c2 and c1)
]

[
    (c1: charset "b"
    c2: charset "1"
    c3: complement c1
    u: "make bitset! [not #{FFFFFFFFFFFFBF}]"
    true)

    (u = mold c3 and c2)
    (u = mold c2 and c3)

    (u: "make bitset! [not #{FFFFFFFFFFFFFFFFFFFFFFFFFF}]"
    u = mold c1 and c3)

    (c4: complement c2
    "make bitset! #{FFFFFFFFFFFFBF}" = mold c3 and c4)
]

=== XOR ===

[
    (c1: charset "b"
    c2: charset "1"
    u: "make bitset! #{00000000000040000000000020}"
    true)

    (u = mold c1 xor c2)
    (u = mold c2 xor c1)
]

[
    (c1: charset "b"
    c2: charset "1"
    c3: complement c1
    u: "make bitset! [not #{00000000000040000000000020}]"
    true)

    (u = mold c3 xor c2)
    (u = mold c2 xor c3)

    (u: "make bitset! [not #{00000000000000000000000000}]"
    u = mold c1 xor c3)

    (c4: complement c2
    "make bitset! #{00000000000040FFFFFFFFFFDF}" = mold c3 xor c4)
]

=== COMPLEMENT ===

("make bitset! [not #{}]" = mold charset [not])
("make bitset! [not #{80}]" = mold charset [not #"^(00)"])
("make bitset! [not #{40}]" = mold charset [not #"^(01)"])
("make bitset! [not #{000000000000FFC0}]" = mold charset [not "0123456789"])
("make bitset! [not #{F0}]" = mold charset [not 0 1 2 3])

[
    (bs: make bitset! 1, true)

    (false = complement? bs)

    ("make bitset! #{00}" = mold bs)
    (8 = length? bs)

    (bs: complement bs
    true = complement? bs)

    (8 = length? bs)

    ("make bitset! [not #{00}]" = mold bs)
]

[
    (bs: charset [not "hello123" #"a" - #"z"], true)

    (128 = length? bs)

    ("make bitset! [not #{0000000000007000000000007FFFFFE0}]" = mold bs)

    (clear bs
    128 = length? bs)

    ("make bitset! [not #{00000000000000000000000000000000}]" = mold bs)
]

[
    (bs: complement charset " ", true)

    (40 = length? bs)
    (bs/31 = true)
    (bs/32 = false)
    (bs/33 = true)
    (bs/200 = true)

    (bs/32: true
    bs/32 = true
    "make bitset! [not #{0000000000}]" = mold bs)

    (poke bs #" " none
    bs/32 = false
    "make bitset! [not #{0000000080}]" = mold bs)

    (clear bs
    "make bitset! [not #{0000000000}]" = mold bs)

    (poke bs [32 - 40] none
    "make bitset! [not #{00000000FF80}]" = mold bs)

    (poke bs [32 - 40] true
    "make bitset! [not #{000000000000}]" = mold bs)
]

=== NONSENSE ===

(error? try [make bitset! [-1]])

(error? try [make bitset!  1.#NaN])
(error? try [make bitset!  1.#INF])
(error? try [make bitset! -1.#INF])
(error? try [make bitset! -1.0])
(equal? make bitset! 0 make bitset! 0.0)
(equal? make bitset! 1 make bitset! 1.0)
(equal? make bitset! 2 make bitset! 2.999)

=== ISSUES ===

[https://github.com/red/red/issues/3443 (
    bs: make bitset! #{}
    n: 135 idx: 0
    until [
        bs/(idx): true
        idx: idx + 1
        idx > n
    ]
    "make bitset! #{FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF}" = mold bs)
]

[https://github.com/red/red/issues/3950 (
    ((make bitset! [not #{000000000000000040}]) = do mold complement charset "A")
]

[https://github.com/red/red/issues/ (
    (bitset: attempt [make bitset! 0], true)

    (bitset? bitset)
    (zero? length? bitset)
    (equal? mold bitset "make bitset! #{}")
]
