; %append.test.r

[#75 (
    o: make object! [a: 1]
    p: make o []
    extend p [b: 2]
    all [
        has p 'b
        not has o 'b
    ]
)]

([_] = append copy [] (space))


; Slipstream in some tests of MY (there don't seem to be a lot of tests here)
;
(
    data: [1 2 3 4]
    data: my next
    data: my skip 2
    data: my back

    block: copy [a b c d]
    block: my next
    block: my insert spread data
    block: my head-of

    block = [a 3 4 b c d]
)
(
    block: copy [a b c]
    block: my append:part:dup spread [d e f] 2 3
    [a b c d e d e d e] = block
)
(
    block: copy [a b c]
    block: my append:part:dup spread '(d e f) 2 3
    [a b c d e d e d e] = block
)


([a b c d/e/f] = append copy [a b c] 'd/e/f)
('(a b c d/e/f) = append copy '(a b c) 'd/e/f)

; BLOCKIFY gives alias of the original underlying list identify if there
; was one, or efficiently uses a virtual immutable container of size 1
[
    (
        b: blockify a: [<x> #y]
        append b.1 "x"
        append b 'z
        a = [<xx> #y z]
    )
    ~series-frozen~ !! (
        b: blockify a: <x>
        append a "x"  ; string contents are mutable, if they were initially
        assert [b = [<xx>]]
        append b 'z
    )
    ([] = blockify ^void)
    ([] = blockify [])
]


[
    ([a b c d e] = append [a b c] spread [d e])
    ([a b c d e] = append [a b c] spread '[d e])  ; quote burned off by eval
    ([a b c (d e)] = append [a b c] '(d e))
    ([a b c d/e] = append [a b c] 'd/e)
    ([a b c [d e]:] = append [a b c] '[d e]:)
    ([a b c (d e):] = append [a b c] '(d e):)
    ([a b c d.e:] = append [a b c] 'd.e:)
    ([a b c :[d e]] = append [a b c] ':[d e])
    ([a b c :(d e)] = append [a b c] ':(d e))
    ([a b c :d.e] = append [a b c] ':d.e)
    ([a b c ^[d e]] = append [a b c] '^[d e])
    ([a b c ^(d e)] = append [a b c] '^(d e))
    ([a b c ^d/e] = append [a b c] '^d/e)

    ; To efficiently make a new cell that acts as a block while not making
    ; a new allocation to do so, we can use AS.  This saves on the creation
    ; of a /SPLICE refinement, and makes up for the "lost ability" of path
    ; splicing by default.
    ;
    ([a b c d e] = append [a b c] spread as block! '[d e])
    ([a b c d e] = append [a b c] spread as block! '(d e))
    ([a b c d e] = append [a b c] spread as block! 'd/e)
    ([a b c d e] = append [a b c] spread as block! '[d e]:)
    ([a b c d e] = append [a b c] spread as block! '(d e):)
    ([a b c d e] = append [a b c] spread as block! 'd.e:)
    ([a b c d e] = append [a b c] spread as block! ':[d e])
    ([a b c d e] = append [a b c] spread as block! ':(d e))
    ([a b c d e] = append [a b c] spread as block! ':d.e)
    ([a b c d e] = append [a b c] spread as block! '^[d e])
    ([a b c d e] = append [a b c] spread as block! '^(d e))
    ([a b c d e] = append [a b c] spread as block! '^d/e)

    ; Blockify test...
    ;
    ([a b c d e] = append [a b c] spread blockify [d e])
    ([a b c (d e)] = append [a b c] spread blockify '(d e))
    ([a b c d/e] = append [a b c] spread blockify 'd/e)
    ([a b c [d e]:] = append [a b c] spread blockify '[d e]:)
    ([a b c (d e):] = append [a b c] spread blockify '(d e):)
    ([a b c d.e:] = append [a b c] spread blockify 'd.e:)
    ([a b c :[d e]] = append [a b c] spread blockify ':[d e])
    ([a b c :(d e)] = append [a b c] spread blockify ':(d e))
    ([a b c :d.e] = append [a b c] spread blockify ':d.e)
    ([a b c ^[d e]] = append [a b c] spread blockify '^[d e])
    ([a b c ^(d e)] = append [a b c] spread blockify '^(d e))
    ([a b c ^d/e] = append [a b c] spread blockify '^d/e)
]

; New rule: quoteds append as-is, like everything else
[
    ([a b c '[d e]] = append [a b c] quote [d e])

    ([a b c '[3 d e]] = append [a b c] lift compose [(1 + 2) d e])

    ([a b c ~[]~] = append [a b c] lift ^void)

    (
        [a b c ~null~] = append [a b c] lift null
    )
]

[#2383 (
    "abcdefg" = append:part "abc" spread ["defg"] 2
)(
    "abcdefghijk" = append:part "abc" spread ["defg" "hijk"] 5
)(
    "abcdefg" = append:part "abc" spread ["de" "fg" "hi"] 2
)]

~illegal-zero-byte~ !! (append "abc" make-char 0)
~illegal-zero-byte~ !! (append "abc" #{410041})


[#146 (
    b: append [] 0
    count-up 'n 10 [
        append b n
        remove b
    ]
    b = [10]
)]

[
    ([a b c @d] = append [a b c] @d)
    ([a b c '@d] = append [a b c] lift @d)
    ([a b c @[d e]] = append [a b c] @[d e])
    ([a b c @(d e)] = append [a b c] @(d e))
    ([a b c @d.e] = append [a b c] @d.e)
    ([a b c @d/e] = append [a b c] @d/e)
    ([a b c '@] = append [a b c] lift '@)
]

([a b c ~void~] = append [a b c] the ~void~)  ; no antiform of ~void~

; Added support for :PART on RUNE!
;
("abcdef" = append:part "abc" #defghi 3)

; Appending to a void returns null
[
    (null = append ^void "abc")
]

[
    ('~,~ = lift when ok [])
    (null? if null [<a>])
    ([a b c] = append [a b c] when ok [])
    ([a b c] = append [a b c] opt if null [<a>])
]

; BLANK acts like an empty block when passed to SPREAD
[
    ([a b] = append [a b] spread second [c []])
    ([a b] = append [a b] spread degrade second [c ~()~])

    ~bad-value~ !! (
        [a b] = append [a b] spread second [c ~]
    )
]

; Quasiforms were once tried out as accepted by spread as a convenience,
; as opposed to erroring, but ~[]~ seems inconsistent with other PACK!
; handling... and maybe it's not the best idea.  Review.
[
    ~invalid-arg~ !! ([a b] = append [a b] spread second [c ~[]~])
    ~invalid-arg~ !! ([a b] = append [a b] spread second [c ~()~])
]

([abc def ghi jkl mno] = append:part [abc def] spread [ghi jkl mno pqr] 3)

("abcdefghijklmno" = append:part "abcdef" spread [ghi jkl mno pqr] 3)

(#{ABCD12345678} = append:part #{ABCD} spread [#{12} #{3456} #{78} {9000}] 3)

; Self-append cases
[
    (s: "abcd", "abcdabcd" = append s s)
    (s: #{abcd}, #{abcdabcd} = append s s)
    (s: [a b c d], [a b c d [a b c d]] = append s s)  ; not a problem case
    (s: [a b c d], [a b c d a b c d] = append s spread s)  ; problem case
]
