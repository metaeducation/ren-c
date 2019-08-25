; functions/series/append.r
[#75 (
    o: make object! [a: 1]
    p: make o []
    append p [b 2]
    not in o 'b
)]

([] = append copy [] (null))


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
([a b c d/e/f] = append copy [a b c] [d/e/f])
('a/b/c/d/e/f = join 'a/b/c ['d 'e 'f])
('(a b c d/e/f) = append copy '(a b c) [d/e/f])
(did trap ['a/b/c/d/e/f = join 'a/b/c '('d 'e 'f)])
('a/b/c/d/e/f = join 'a/b/c 'd/e/f)

[#2383 (
    "abcde" = append/part "abc" ["defg"] 2
)(
    "abcdefgh" = append/part "abc" ["defg" "hijk"] 5
)]
