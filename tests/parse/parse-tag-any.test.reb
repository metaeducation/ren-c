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
        'a == uparse [a] [res: <any>]
        res = 'a
    ]
)

[
    (didn't uparse [a a] [1 <any>])
    ('a == uparse [a a] [2 <any>])
    (didn't uparse [a a] [3 <any>])

    (didn't uparse [a a] [repeat ([1 1]) <any>])
    ('a == uparse [a a] [repeat ([1 2]) <any>])
    ('a == uparse [a a] [repeat ([2 2]) <any>])
    ('a == uparse [a a] [repeat ([2 3]) <any>])
    (didn't uparse [a a] [repeat ([3 4]) <any>])

    ('a == uparse [a] [<any>])
    ('b == uparse [a b] [<any> <any>])
    ('b == uparse [a b] [<any> [<any>]])
    ('b == uparse [a b] [[<any>] [<any>]])
]

[
    (didn't uparse "aa" [1 <any>])
    (#a == uparse "aa" [2 <any>])
    (didn't uparse "aa" [3 <any>])

    (didn't uparse "aa" [repeat ([1 1]) <any>])
    (#a == uparse "aa" [repeat ([1 2]) <any>])
    (#a == uparse "aa" [repeat ([2 2]) <any>])
    (#a == uparse "aa" [repeat ([2 3]) <any>])
    (didn't uparse "aa" [repeat ([3 4]) <any>])

    (#a == uparse "a" [<any>])
    (#b == uparse "ab" [<any> <any>])
    (#b == uparse "ab" [<any> [<any>]])
    (#b == uparse "ab" [[<any>] [<any>]])
]
