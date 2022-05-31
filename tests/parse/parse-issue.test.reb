; %parse-issue.test.reb
;
; The migration of ISSUE! to be a unified type with CHAR! as TOKEN! is
; something that is moving along slowly, as the impacts are absorbed.
;
; They are case-sensitive matched in PARSE, unlike text strings by default.

; Don't leak internal detail that BINARY! or ANY-STRING! are 0-terminated
[
    (NUL = as issue! 0)

    (null = uparse "" [to NUL])
    (null = uparse "" [thru NUL])
    (null = uparse "" [to [NUL]])
    (null = uparse "" [thru [NUL]])

    (null = uparse #{} [to NUL])
    (null = uparse #{} [thru NUL])
    (null = uparse #{} [to [NUL]])
    (null = uparse #{} [thru [NUL]])
]

[
    (#a == uparse "a" [#a])
    (didn't uparse "a" [#b])
    (#b == uparse "ab" [#a #b])
    (#a == uparse "a" [[#a]])
    ("b" == uparse "ab" [[#a] "b"])
    (#b == uparse "ab" [#a [#b]])
    (#b == uparse "ab" [[#a] [#b]])
    (#a == uparse "a" [#b | #a])
    (didn't uparse "ab" [#b | "a"])
    (#a == uparse "a" [[#b | #a]])
    (didn't uparse "ab" [[#b | "a"]])
    (#b == uparse "ab" [["a" | #b] [#b | "a"]])
]

[
    (#b == uparse [a #b] ['a #b])
    (#a == uparse [#a] [#b | #a])
]

; "string extraction" tests from %parse-test.red
[
    (
        wa: [#a]
        true
    )

    (
        res: ~
        did all [
            #a == uparse "a" [res: <any>]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #a == uparse "a" [res: #a]
            res = #a
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            #a == uparse "a" [res: res2: #a]
            res = #a
            res2 = #a
        ]
    )
    (
        res: ~
        did all [
            #a == uparse "aa" [res: 2 #a]
            res = #a
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse "aa" [res: 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            #a == uparse "a" [res: [#a]]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #a == uparse "a" [res: wa]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #a == uparse "aa" [res: 2 wa]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #b == uparse "aab" [<any> res: #a <any>]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #b == uparse "aab" [<any> res: [#a | #b] <any>]
            res = #a
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse "a" [res: [#c | #b]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            #c == uparse "baaac" [<any> res: some #a #c]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            #c == uparse "baaac" [<any> res: some wa #c]
            res = #a
        ]
    )
]

; More "string" tests from %parse-test.red
[
    (
        res: ~
        did all [
            1 == uparse "" [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == uparse "a" [#a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse "a" [#b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            1 == uparse "" [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            1 == uparse "a" [[#a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            didn't uparse "a" [[#b (res: 1)]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            3 == uparse "ab" [#a (res: 1) [#c (res: 2) | #b (res: 3)]]
            res = 3
        ]
    )
    (
        res: ~
        did all [
            didn't uparse "ab" [#a (res: 1) [#c (res: 2) | #d (res: 3)]]
            res = 1
        ]
    )
    (didn't uparse "aa" [1 [#a]])
    (#a == uparse "aa" [2 [#a]])
    (didn't uparse "aa" [3 [#a]])

    (didn't uparse "aa" [repeat ([1 1]) [#a]])
    (#a == uparse "aa" [repeat ([1 2]) [#a]])
    (#a == uparse "aa" [repeat ([2 2]) [#a]])
    (#a == uparse "aa" [repeat ([2 3]) [#a]])
    (didn't uparse "aa" [repeat ([3 4]) [#a]])
    (didn't uparse "aa" [repeat ([1 1]) #a])
    (#a == uparse "aa" [repeat ([1 2]) #a])
    (#a == uparse "aa" [repeat ([2 2]) #a])
    (#a == uparse "aa" [repeat ([2 3]) #a])
    (didn't uparse "aa" [repeat ([3 4]) #a])

    (didn't uparse "aa" [1 #a])
    (#a == uparse "aa" [2 #a])
    (didn't uparse "aa" [3 #a])
    (#a == uparse "aa" [some [#a]])
    (#a = uparse "aa" [some [#a] repeat (#) [#b]])
    ("b" == uparse "aabb" [2 #a 2 "b"])
    (didn't uparse "aabb" [2 "a" 3 #b])
]
