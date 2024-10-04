; series/join.test.reb
;
; JOIN has become the unified replacement for REJOIN, AJOIN, etc.
;
; It does not reduce block arguments that are being joined onto the base value.
; You need to use either a GET-BLOCK! or run reduce.
;
; An additional feature is that it accepts a datatype as the first argument,
; to say what the JOIN result should be typed as.
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

~bad-sequence-blank~ !! (join 'a/ spread [/b _ /c.c])

('a/b/c.c = join 'a/ spread [b /c.c])

('.a.(b).c = join '.a spread [. (b) .c])

('/a/ = join '/a spread [/])

('^a.b = join meta-tuple! [a . b])

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
    ~bad-sequence-blank~ !! (transcode "//c/d")
    ('a/b/c/d = join 'a/b/ 'c/d)
    ~???~ !! (join 'a/b 'c/d)  ; missing slash, can't glue b to c

    ('a.b.c.d = join 'a.b '.c.d)
    ~bad-sequence-blank~ !! (transcode "..c.d")
    ~bad-sequence-blank~ !! (join 'a.b. '.c.d)
    ('a.b.c.d = join 'a.b. 'c.d)
    ~???~ !! (join 'a.b 'c.d)  ; missing slash, can't glue b to c
]

; ADDING SINGLE ELEMENTS TO SEQUENCES
[
    ('a/b = join 'a/b _)  ; no-op

    ('a/b/c = join 'a/b/ 'c)
]

; BLANK! when joining BINARY! or ANY-STRING? should be ignored, but ANY-LIST?
; has to keep them.
[
    (#{1020} = join #{10} spread [_ #{20} _])
    ("A B " = join "A" spread [_ "B" _])

    ([A _ B _] = join [A] spread [_ B _])
    ('(A _ B _) = join '(A) spread [_ B _])
]

[
    https://github.com/metaeducation/ren-c/issues/1085
    (#a30bc = join #a spread reduce [10 + 20 "b" #c])
]
