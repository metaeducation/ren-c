; %parse-rune.test.r
;
; We don't leak internal detail that BLOB! or ANY-STRING? are 0-terminated
; This comes "for free" because the NUL codepoint is #{00} as a blob, and
; we don't allow binary searches in strings already for other reasons
; (e.g. a binary could be found on half a codepoint, and we couldn't return
; that position in the input).
[
    (#{00} = make-char 0)

    ~find-string-binary~ !! (parse "" [to NUL])
    ~find-string-binary~ !! (parse "" [thru NUL])
    ~find-string-binary~ !! (parse "" [to [NUL]])
    ~find-string-binary~ !! (parse "" [thru [NUL]])

    ~parse-mismatch~ !! (parse #{} [to NUL])
    ~parse-mismatch~ !! (parse #{} [thru NUL])
    ~parse-mismatch~ !! (parse #{} [to [NUL]])
    ~parse-mismatch~ !! (parse #{} [thru [NUL]])
]

[
    (#a = parse "a" [#a])

    ~parse-mismatch~ !! (parse "a" [#b])

    (#b = parse "ab" [#a #b])
    (#a = parse "a" [[#a]])
    ("b" = parse "ab" [[#a] "b"])
    (#b = parse "ab" [#a [#b]])
    (#b = parse "ab" [[#a] [#b]])
    (#a = parse "a" [#b | #a])

    ~parse-incomplete~ !! (parse "ab" [#b | "a"])

    (#a = parse "a" [[#b | #a]])

    ~parse-incomplete~ !! (parse "ab" [[#b | "a"]])

    (#b = parse "ab" [["a" | #b] [#b | "a"]])
]

[
    (#b = parse [a #b] ['a #b])
    (#a = parse [#a] [#b | #a])
]

; "string extraction" tests from %parse-test.red
[
    (
        wa: [#a]
        ok
    )

    (
        res: ~
        all [
            #a = parse "a" [res: one]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a = parse "a" [res: #a]
            res = #a
        ]
    )
    (
        res: ~
        res2: ~
        all [
            #a = parse "a" [res: res2: #a]
            res = #a
            res2 = #a
        ]
    )
    (
        res: ~
        all [
            #a = parse "aa" [res: repeat 2 #a]
            res = #a
        ]
    )
    (
        res: '~before~
        all [
            error? parse "aa" [res: repeat 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #a = parse "a" [res: [#a]]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a = parse "a" [res: wa]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a = parse "aa" [res: repeat 2 wa]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #b = parse "aab" [<next> res: #a one]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #b = parse "aab" [<next> res: [#a | #b] one]
            res = #a
        ]
    )
    (
        res: '~before~
        all [
            error? parse "a" [res: [#c | #b]]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #c = parse "baaac" [<next> res: some #a #c]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #c = parse "baaac" [<next> res: some wa #c]
            res = #a
        ]
    )
]

; More "string" tests from %parse-test.red
[
    (
        res: ~
        all [
            1 = parse "" [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 = parse "a" [#a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            error? parse "a" [#b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            1 = parse "" [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 = parse "a" [[#a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            error? parse "a" [[#b (res: 1)]]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            3 = parse "ab" [#a (res: 1) [#c (res: 2) | #b (res: 3)]]
            res = 3
        ]
    )
    (
        res: ~
        all [
            error? parse "ab" [#a (res: 1) [#c (res: 2) | #d (res: 3)]]
            res = 1
        ]
    )

    ~parse-incomplete~ !! (parse "aa" [repeat 1 [#a]])

    (#a = parse "aa" [repeat 2 [#a]])

    ~parse-mismatch~ !! (parse "aa" [repeat 3 [#a]])
    ~parse-incomplete~ !! (parse "aa" [repeat ([1 1]) [#a]])

    (#a = parse "aa" [repeat ([1 2]) [#a]])
    (#a = parse "aa" [repeat ([2 2]) [#a]])
    (#a = parse "aa" [repeat ([2 3]) [#a]])

    ~parse-mismatch~ !! (parse "aa" [repeat ([3 4]) [#a]])
    ~parse-incomplete~ !! (parse "aa" [repeat ([1 1]) #a])

    (#a = parse "aa" [repeat ([1 2]) #a])
    (#a = parse "aa" [repeat ([2 2]) #a])
    (#a = parse "aa" [repeat ([2 3]) #a])

    ~parse-mismatch~ !! (parse "aa" [repeat ([3 4]) #a])
    ~parse-incomplete~ !! (parse "aa" [repeat 1 #a])

    (#a = parse "aa" [repeat 2 #a])

    ~parse-mismatch~ !! (parse "aa" [repeat 3 #a])

    (#a = parse "aa" [some [#a]])
    (void? parse "aa" [some [#a] repeat (#) [#b]])
    ("b" = parse "aabb" [repeat 2 #a, repeat 2 "b"])

    ~parse-mismatch~ !! (parse "aabb" [repeat 2 "a", repeat 3 #b])
]
