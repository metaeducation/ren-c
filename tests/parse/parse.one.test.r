; %parse-one.test.r
;
; ONE takes the place of some instances of R3-Alpha SKIP in UPARSE.  While
; others are taken by NEXT...which is better for suggesting you don't want
; the value, or if you want a synthesized product you want a position.  This
; lines up better with SKIP and NEXT in normal series evaluation, where SKIP
; is not arity-0.
;
; Also addresses the fact that `x: skip` seems fishy...if something is
; being "skipped over" then why would it yield a value?
;
; There is no "THE" combinator in UPARSE, and so @ is used as a synonym for
; ONE.  This ties in a bit with its uses of "match an item literally here"
; e.g. @var, but when the var is missing it makes some sense to match with
; basically whatever is there.  (However this is probably not a great idea
; to make @(void) a synonym for @, but rather to opt out of the match...)

(
    res: ~
    all [
        'a = parse [a] [res: one]
        res = 'a
    ]
)

(
    res: ~
    all [
        'a = parse [a] [res: @]
        res = 'a
    ]
)

[
    ~parse-incomplete~ !! (parse [a a] [repeat 1 one])
    ('a = parse [a a] [repeat 2 one])
    ~parse-mismatch~ !! (parse [a a] [repeat 3 one])

    ~parse-incomplete~ !! (parse [a a] [repeat ([1 1]) one])
    ('a = parse [a a] [repeat ([1 2]) one])
    ('a = parse [a a] [repeat ([2 2]) one])
    ('a = parse [a a] [repeat ([2 3]) one])
    ~parse-mismatch~ !! (parse [a a] [repeat ([3 4]) one])

    ('a = parse [a] [one])
    ('b = parse [a b] [one one])
    ('b = parse [a b] [one [one]])
    ('b = parse [a b] [[one] [one]])
]

[
    ~parse-incomplete~ !! (parse "aa" [repeat 1 one])
    (#a = parse "aa" [repeat 2 one])
    ~parse-mismatch~ !! (parse "aa" [repeat 3 one])

    ~parse-incomplete~ !! (parse "aa" [repeat ([1 1]) one])
    (#a = parse "aa" [repeat ([1 2]) one])
    (#a = parse "aa" [repeat ([2 2]) one])
    (#a = parse "aa" [repeat ([2 3]) one])
    ~parse-mismatch~ !! (parse "aa" [repeat ([3 4]) one])

    (#a = parse "a" [one])
    (#b = parse "ab" [one one])
    (#b = parse "ab" [one [one]])
    (#b = parse "ab" [[one] [one]])
]
