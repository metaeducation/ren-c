; %append.test.reb

[#75 (
    o: make object! [a: 1]
    p: make o []
    append p [b 2]
    not in o 'b
)]

([] = append copy [] (blank))


; Slipstream in some tests of MY (there don't seem to be a lot of tests here)
;
(
    data: [1 2 3 4]
    data: my next
    data: my skip 2
    data: my back

    block: copy [a b c d]
    block: my next
    block: my insert data
    block: my head

    block = [a 3 4 b c d]
)
(
    block: copy [a b c]
    block: my append/part/dup [d e f] 2 3
    [a b c d e d e d e] = block
)

; https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
;
([a b c d/e/f] = append copy [a b c] just d/e/f)
('a/b/c/d/e = join 'a/b/c [/ d/e])
('(a b c d/e/f) = append copy '(a b c) just d/e/f)
(did trap ['a/b/c/d/e/f = join 'a/b/c '(d e f)])
('a/b/c/d/e/f = join 'a/b/c/ 'd/e/f)

; BLOCKIFY gives alias of the original underlying array identify if there
; was one, or efficiently uses a virtual immutable container of size 1
[
    (
        b: blockify a: [<x> #y]
        append b/1 "x"
        append b just z
        a = [<xx> #y z]
    )(
        b: blockify a: <x>
        append a "x"  ; string contents are mutable, if they were initially
        did all [
            b = [<xx>]
            trap [
                append b just z  ; block doesn't truly "exist", can't append
            ] then e -> [
                e/id = 'series-frozen
            ]
        ]
    )
    ([] = blockify null)
    ([] = blockify [])
]


[
    ; Simple, reasoned behavior for deciding splices: only plain BLOCK! does
    ; https://forum.rebol.info/t/every-thought-on-array-splicing-has-been-had/1332/
    ;
    ([a b c d e] = append [a b c] [d e])
    ([a b c d e] = append [a b c] '[d e])  ; quote burned off by evaluation
    ([a b c (d e)] = append [a b c] just (d e))
    ([a b c d/e] = append [a b c] just d/e)
    ([a b c [d e]:] = append [a b c] just [d e]:)
    ([a b c (d e):] = append [a b c] just (d e):)
    ([a b c d/e:] = append [a b c] just d/e:)
    ([a b c :[d e]] = append [a b c] just :[d e])
    ([a b c :(d e)] = append [a b c] just :(d e))
    ([a b c :d/e] = append [a b c] just :d/e)
    ([a b c ^[d e]] = append [a b c] just ^[d e])
    ([a b c ^(d e)] = append [a b c] just ^(d e))
    ([a b c ^d/e] = append [a b c] just ^d/e)

    ; To efficiently make a new cell that acts as a block while not making
    ; a new allocation to do so, we can use AS.  This saves on the creation
    ; of a /SPLICE refinement, and makes up for the "lost ability" of path
    ; splicing by default.
    ;
    ([a b c d e] = append [a b c] as block! '[d e])
    ([a b c d e] = append [a b c] as block! '(d e))
    ([a b c d e] = append [a b c] as block! 'd/e)
    ([a b c d e] = append [a b c] as block! '[d e]:)
    ([a b c d e] = append [a b c] as block! '(d e):)
    ([a b c d e] = append [a b c] as block! 'd/e:)
    ([a b c d e] = append [a b c] as block! ':[d e])
    ([a b c d e] = append [a b c] as block! ':(d e))
    ([a b c d e] = append [a b c] as block! ':d/e)
    ([a b c d e] = append [a b c] as block! '^[d e])
    ([a b c d e] = append [a b c] as block! '^(d e))
    ([a b c d e] = append [a b c] as block! '^d/e)

    ; Classic way of subverting splicing is use of APPEND/ONLY
    ;
    ([a b c [d e]] = append/only [a b c] [d e])
    ([a b c (d e)] = append/only [a b c] '(d e))
    ([a b c d/e] = append/only [a b c] 'd/e)
    ([a b c [d e]:] = append/only [a b c] '[d e]:)
    ([a b c (d e):] = append/only [a b c] '(d e):)
    ([a b c d/e:] = append/only [a b c] 'd/e:)
    ([a b c :[d e]] = append/only [a b c] ':[d e])
    ([a b c :(d e)] = append/only [a b c] ':(d e))
    ([a b c :d/e] = append/only [a b c] ':d/e)
    ([a b c ^[d e]] = append/only [a b c] '^[d e])
    ([a b c ^(d e)] = append/only [a b c] '^(d e))
    ([a b c ^d/e] = append/only [a b c] '^d/e)

    ; Blockify test...should be a no-op
    ;
    ([a b c d e] = append [a b c] blockify [d e])
    ([a b c (d e)] = append [a b c] blockify '(d e))
    ([a b c d/e] = append [a b c] blockify 'd/e)
    ([a b c [d e]:] = append [a b c] blockify '[d e]:)
    ([a b c (d e):] = append [a b c] blockify '(d e):)
    ([a b c d/e:] = append [a b c] blockify 'd/e:)
    ([a b c :[d e]] = append [a b c] blockify ':[d e])
    ([a b c :(d e)] = append [a b c] blockify ':(d e))
    ([a b c :d/e] = append [a b c] blockify ':d/e)
    ([a b c ^[d e]] = append [a b c] blockify '^[d e])
    ([a b c ^(d e)] = append [a b c] blockify '^(d e))
    ([a b c ^d/e] = append [a b c] blockify '^d/e)

    ; Enblock test...this offers a cheap way to put the "don't splice"
    ; instruction onto the value itself.
    ;
    ([a b c [d e]] = append [a b c] enblock [d e])
    ([a b c (d e)] = append [a b c] enblock '(d e))
    ([a b c d/e] = append [a b c] enblock 'd/e)
    ([a b c [d e]:] = append [a b c] enblock '[d e]:)
    ([a b c (d e):] = append [a b c] enblock '(d e):)
    ([a b c d/e:] = append [a b c] enblock 'd/e:)
    ([a b c :[d e]] = append [a b c] enblock ':[d e])
    ([a b c :(d e)] = append [a b c] enblock ':(d e))
    ([a b c :d/e] = append [a b c] enblock ':d/e)
    ([a b c ^[d e]] = append [a b c] enblock '^[d e])
    ([a b c ^(d e)] = append [a b c] enblock '^(d e))
    ([a b c ^d/e] = append [a b c] enblock '^d/e)
]

; New idea: By default, a QUOTED! passed to APPEND will act as APPEND/ONLY.
[
    ([a b c [d e]] = append [a b c] ^[d e])

    ([a b c [3 d e]] = append [a b c] ^ compose [(1 + 2) d e])

    ([a b c ~null~] = append [a b c] quote null)

    (
        e: trap [[a b c] = append [a b c] ^(null)]
        e/id = 'arg-required
    )
]

[#2383 (
    "abcde" = append/part "abc" ["defg"] 2
)(
    "abcdefgh" = append/part "abc" ["defg" "hijk"] 5
)]

('illegal-zero-byte = (trap [append "abc" make char! 0]).id)
('illegal-zero-byte = (trap [append "abc" #{410041}]).id)


[#146 (
    b: append [] 0
    count-up n 10 [
        append b n
        remove b
    ]
    b = [10]
)]

; @ types are now appended as-is minus the @ unless quoted or in a block
[
    ([a b c d] = append [a b c] @d)
    ([a b c @d] = append [a b c] ^ @d)
    ([a b c [d e]] = append [a b c] @[d e])
    ([a b c (d e)] = append [a b c] @(d e))
    ([a b c d.e] = append [a b c] @d.e)
    ([a b c d/e] = append [a b c] @d/e)
    ([a b c @] = append [a b c] ^ '@)
]

; Special case... appending a quoted NULL is neither an error or a no-op.
; As with the attempt to do a single element compose of a NULL, it's ~null~
;
([a b c ~null~] = append [a b c] the ')

; Added support for /PART on ISSUE!
;
("abcdef" = append/part "abc" #defghi 3)
