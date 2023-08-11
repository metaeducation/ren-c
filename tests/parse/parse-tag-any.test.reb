; %parse-tag-any.test.reb
;
; <any> takes the place of R3-Alpha SKIP in UPARSE.  The ANY operation has been
; replaced by OPT SOME or MAYBE SOME with the optional use of FURTHER, which
; lets "any" mean its more natural non-iterative sense.
;
; This addresses the fact that `x: skip` seems fishy...if something is
; being "skipped over" then why would it yield a value?

(
    res: ~
    did all [
        'a == parse [a] [res: <any>]
        res = 'a
    ]
)

[
    (raised? parse [a a] [repeat 1 <any>])
    ('a == parse [a a] [repeat 2 <any>])
    (raised? parse [a a] [repeat 3 <any>])

    (raised? parse [a a] [repeat ([1 1]) <any>])
    ('a == parse [a a] [repeat ([1 2]) <any>])
    ('a == parse [a a] [repeat ([2 2]) <any>])
    ('a == parse [a a] [repeat ([2 3]) <any>])
    (raised? parse [a a] [repeat ([3 4]) <any>])

    ('a == parse [a] [<any>])
    ('b == parse [a b] [<any> <any>])
    ('b == parse [a b] [<any> [<any>]])
    ('b == parse [a b] [[<any>] [<any>]])
]

[
    (raised? parse "aa" [repeat 1 <any>])
    (#a == parse "aa" [repeat 2 <any>])
    (raised? parse "aa" [repeat 3 <any>])

    (raised? parse "aa" [repeat ([1 1]) <any>])
    (#a == parse "aa" [repeat ([1 2]) <any>])
    (#a == parse "aa" [repeat ([2 2]) <any>])
    (#a == parse "aa" [repeat ([2 3]) <any>])
    (raised? parse "aa" [repeat ([3 4]) <any>])

    (#a == parse "a" [<any>])
    (#b == parse "ab" [<any> <any>])
    (#b == parse "ab" [<any> [<any>]])
    (#b == parse "ab" [[<any>] [<any>]])
]
