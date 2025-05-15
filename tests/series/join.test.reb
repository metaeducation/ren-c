; series/join.test.reb
;
; JOIN has become the unified replacement for REJOIN, AJOIN, etc.
;
; It accepts a datatype as the first argument, to say what the JOIN result
; should be typed as.
;
; https://forum.rebol.info/t/rejoin-ugliness-and-the-usefulness-of-tests/248

('a/b/c = join 'a/b '/c)
('a/b/c = join path! [a/b /c])
('a/b/c/d = join path! reduce ['a/b '/c '/d])

('a/b/c/d/e = join 'a/b/c spread [/ d/e])

~???~ !! (join 'a/b/c '(d e f))  ; missing slash
('a/b/c/(d e f) = join 'a/b/c/ '(d e f))
('a/b/c/(d e f) = join 'a/b/c '/(d e f))

('a/b/c/d/e/f = join 'a/b/c/ 'd/e/f)

~???~ !! (join path! [a b])

~bad-sequence-space~ !! (join 'a/ spread [/b _ /c.c])

('a/b/c.c = join 'a/ spread [b /c.c])

('.a.(b).c = join '.a spread [. (b) .c])

('/a/ = join '/a spread [/])

('^a.b = join tuple! [^a . b])

; VOID is not a no-op, because you still get a copy made for ANY-SERIES?
(
    input: "abc"
    output: ~
    all [
        input = output: join input void
        not same? input output
    ]
)

; DIRECT PATH AND TUPLE SPLICING WITHOUT A BLOCK
[
    ('a/b/c/d = join 'a/b '/c/d)
    ~bad-sequence-space~ !! (transcode "//c/d")
    ('a/b/c/d = join 'a/b/ 'c/d)
    ~???~ !! (join 'a/b 'c/d)  ; missing slash, can't glue b to c

    ('a.b.c.d = join 'a.b '.c.d)
    ~bad-sequence-space~ !! (transcode "..c.d")
    ~bad-sequence-space~ !! (join 'a.b. '.c.d)
    ('a.b.c.d = join 'a.b. 'c.d)
    ~???~ !! (join 'a.b 'c.d)  ; missing slash, can't glue b to c
]

; ADDING SINGLE ELEMENTS TO SEQUENCES
[
    ('a/b = join 'a/b _)  ; no-op

    ('a/b/c = join 'a/b/ 'c)
]

[
    (#{1020} = join #{10} @[_ #{20} _])
    ("A B " = join "A" @[_ "B" _])

    ([A _ B _] = join [A] @[_ B _])
    ('(A _ B _) = join '(A) @[_ B _])
]

[
    https://github.com/metaeducation/ren-c/issues/1085
    (#a30bc = join #a spread reduce [10 + 20 "b" #c])
]

(
    [a b] = join:with:head:tail [a b] [] '+
)(
    [a b + c +] = join:with:head:tail [a b] ['c] '+
)(
    [a b + c + d +] = join:with:head:tail [a b] ['c 'd] '+
)(
    [a b + c + d] = join:with:head [a b] ['c 'd] '+
)(
    [a + b + c + d] = join:with block! [spread [a b] 'c 'd] '+
)
