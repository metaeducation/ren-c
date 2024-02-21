; %parse-issue.test.reb
;
; The migration of ISSUE! to be a unified type with CHAR! as TOKEN! is
; something that is moving along slowly, as the impacts are absorbed.
;
; They are case-sensitive matched in PARSE, unlike text strings by default.

; Don't leak internal detail that BINARY! or ANY-STRING? are 0-terminated
[
    (NUL = as issue! 0)

    (raised? parse "" [to NUL])
    (raised? parse "" [thru NUL])
    (raised? parse "" [to [NUL]])
    (raised? parse "" [thru [NUL]])

    (raised? parse #{} [to NUL])
    (raised? parse #{} [thru NUL])
    (raised? parse #{} [to [NUL]])
    (raised? parse #{} [thru [NUL]])
]

[
    (#a == parse "a" [#a])
    (raised? parse "a" [#b])
    (#b == parse "ab" [#a #b])
    (#a == parse "a" [[#a]])
    ("b" == parse "ab" [[#a] "b"])
    (#b == parse "ab" [#a [#b]])
    (#b == parse "ab" [[#a] [#b]])
    (#a == parse "a" [#b | #a])
    (raised? parse "ab" [#b | "a"])
    (#a == parse "a" [[#b | #a]])
    (raised? parse "ab" [[#b | "a"]])
    (#b == parse "ab" [["a" | #b] [#b | "a"]])
]

[
    (#b == parse [a #b] ['a #b])
    (#a == parse [#a] [#b | #a])
]

; "string extraction" tests from %parse-test.red
[
    (
        wa: [#a]
        true
    )

    (
        res: ~
        all [
            #a == parse "a" [res: <any>]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a == parse "a" [res: #a]
            res = #a
        ]
    )
    (
        res: ~
        res2: ~
        all [
            #a == parse "a" [res: res2: #a]
            res = #a
            res2 = #a
        ]
    )
    (
        res: ~
        all [
            #a == parse "aa" [res: repeat 2 #a]
            res = #a
        ]
    )
    (
        res: '~before~
        all [
            raised? parse "aa" [res: repeat 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #a == parse "a" [res: [#a]]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a == parse "a" [res: wa]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #a == parse "aa" [res: repeat 2 wa]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #b == parse "aab" [<any> res: #a <any>]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #b == parse "aab" [<any> res: [#a | #b] <any>]
            res = #a
        ]
    )
    (
        res: '~before~
        all [
            raised? parse "a" [res: [#c | #b]]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            #c == parse "baaac" [<any> res: some #a #c]
            res = #a
        ]
    )
    (
        res: ~
        all [
            #c == parse "baaac" [<any> res: some wa #c]
            res = #a
        ]
    )
]

; More "string" tests from %parse-test.red
[
    (
        res: ~
        all [
            1 == parse "" [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 == parse "a" [#a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            raised? parse "a" [#b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            1 == parse "" [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        all [
            1 == parse "a" [[#a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        all [
            raised? parse "a" [[#b (res: 1)]]
            res = '~before~
        ]
    )
    (
        res: ~
        all [
            3 == parse "ab" [#a (res: 1) [#c (res: 2) | #b (res: 3)]]
            res = 3
        ]
    )
    (
        res: ~
        all [
            raised? parse "ab" [#a (res: 1) [#c (res: 2) | #d (res: 3)]]
            res = 1
        ]
    )
    (raised? parse "aa" [repeat 1 [#a]])
    (#a == parse "aa" [repeat 2 [#a]])
    (raised? parse "aa" [repeat 3 [#a]])

    (raised? parse "aa" [repeat ([1 1]) [#a]])
    (#a == parse "aa" [repeat ([1 2]) [#a]])
    (#a == parse "aa" [repeat ([2 2]) [#a]])
    (#a == parse "aa" [repeat ([2 3]) [#a]])
    (raised? parse "aa" [repeat ([3 4]) [#a]])
    (raised? parse "aa" [repeat ([1 1]) #a])
    (#a == parse "aa" [repeat ([1 2]) #a])
    (#a == parse "aa" [repeat ([2 2]) #a])
    (#a == parse "aa" [repeat ([2 3]) #a])
    (raised? parse "aa" [repeat ([3 4]) #a])

    (raised? parse "aa" [repeat 1 #a])
    (#a == parse "aa" [repeat 2 #a])
    (raised? parse "aa" [repeat 3 #a])
    (#a == parse "aa" [some [#a]])
    (void? parse "aa" [some [#a] repeat (#) [#b]])
    ("b" == parse "aabb" [repeat 2 #a, repeat 2 "b"])
    (raised? parse "aabb" [repeat 2 "a", repeat 3 #b])
]
