; %parse-tag-any.test.reb
;
; <any> takes the place of SKIP in UPARSE.  The ANY operation has been
; replaced by WHILE with the optional use of FURTHER, which lets "any"
; mean its more natural non-iterative sense.
;
; This addresses the fact that `x: skip` seems fishy...if something is
; being "skipped over" then why would it yield a value?
;
; Pursuant to that, SKIP still exists tentatively as a non-value-bearing
; way of going to the next value--a synonym for `elide <any>`.

(
    res: ~
    did all [
        uparse? [a] [res: <any>]
        res = 'a
    ]
)

[
    (not uparse? [a a] [1 <any>])
    (uparse? [a a] [2 <any>])
    (not uparse? [a a] [3 <any>])

    (not uparse? [a a] [repeat ([1 1]) <any>])
    (uparse? [a a] [repeat ([1 2]) <any>])
    (uparse? [a a] [repeat ([2 2]) <any>])
    (uparse? [a a] [repeat ([2 3]) <any>])
    (not uparse? [a a] [repeat ([3 4]) <any>])

    (uparse? [a] [<any>])
    (uparse? [a b] [<any> <any>])
    (uparse? [a b] [<any> [<any>]])
    (uparse? [a b] [[<any>] [<any>]])
]

[
    (not uparse? "aa" [1 <any>])
    (uparse? "aa" [2 <any>])
    (not uparse? "aa" [3 <any>])

    (not uparse? "aa" [repeat ([1 1]) <any>])
    (uparse? "aa" [repeat ([1 2]) <any>])
    (uparse? "aa" [repeat ([2 2]) <any>])
    (uparse? "aa" [repeat ([2 3]) <any>])
    (not uparse? "aa" [repeat ([3 4]) <any>])

    (uparse? "a" [<any>])
    (uparse? "ab" [<any> <any>])
    (uparse? "ab" [<any> [<any>]])
    (uparse? "ab" [[<any>] [<any>]])
]
