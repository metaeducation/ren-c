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

('a/b/c = join 'a/b [/c])
('a/b/c = join path! [a/b /c])
('a/b/c/d = join path! :['a/b '/c '/d])

(error? trap [join path! [a b]])

('a//b/c.c = join 'a/ [/b _ /c.c])

('.a.(b).c = join '.a [. (b) .c])

('/a/ = join '/a [/])

(':a/b = join get-path! [_ _ a / b _ _])

; BLANK! is not a no-op, because you still get a copy made for ANY-SERIES!
(
    input: "abc"
    output: ~
    did all [
        input = output: join input _
        not same? input output
    ]
)

; DIRECT PATH AND TUPLE SPLICING WITHOUT A BLOCK
;
; Though APPEND doesn't allow you to pass in evaluative arguments, JOIN lets
; you do so when you are building a PATH!.
[
    ('a/b/c/d = join 'a/b '/c/d)
    ('a/b//c/d = join 'a/b '//c/d)
    ('a/b//c/d = join 'a/b/ '/c/d)
    ('a/b/c/d = join 'a/b/ 'c/d)
    (error? trap [join 'a/b 'c/d])  ; missing slash, can't glue b to c

    ('a.b.c.d = join 'a.b '.c.d)
    ('a.b..c.d = join 'a.b '..c.d)
    ('a.b..c.d = join 'a.b. '.c.d)
    ('a.b.c.d = join 'a.b. 'c.d)
    (error? trap [join 'a.b 'c.d])  ; missing slash, can't glue b to c
]

; ADDING SINGLE ELEMENTS TO SEQUENCES
;
; Right now this doesn't enforce the "no evaluative" rule.  (It doesn't for
; PATH! or TUPLE! so why for WORD?)
[
    ('a/b = join 'a/b _)  ; no-op

    ('a/b/c = join 'a/b/ 'c)
    ('a/b//c = join 'a/b// 'c)
    ('a/b//[d] = join 'a/b// quote [d])
]

; BLANK! when joining BINARY! or ANY-STRING! should be ignored, but ANY-ARRAY!
; has to keep them.
[
    (#{1020} = join #{10} [_ #{20} _])
    ("AB" = join "A" [_ "B" _])

    ([A _ B _] = join [A] [_ B _])
    ('(A _ B _) = join '(A) [_ B _])
]

[
    https://github.com/metaeducation/ren-c/issues/1085
    (#a30bc = join #a :[10 + 20 "b" #c])
]
