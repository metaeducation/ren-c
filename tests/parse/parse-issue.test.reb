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
    (uparse? "a" [#a])
    (not uparse? "a" [#b])
    (uparse? "ab" [#a #b])
    (uparse? "a" [[#a]])
    (uparse? "ab" [[#a] "b"])
    (uparse? "ab" [#a [#b]])
    (uparse? "ab" [[#a] [#b]])
    (uparse? "a" [#b | #a])
    (not uparse? "ab" [#b | "a"])
    (uparse? "a" [[#b | #a]])
    (not uparse? "ab" [[#b | "a"]])
    (uparse? "ab" [["a" | #b] [#b | "a"]])
]

[
    (uparse? [a #b] ['a #b])
    (uparse? [#a] [#b | #a])
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
            uparse? "a" [res: <any>]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "a" [res: #a]
            res = #a
        ]
    )
    (
        res: ~
        res2: ~
        did all [
            uparse? "a" [res: res2: #a]
            res = #a
            res2 = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "aa" [res: 2 #a]
            res = #a
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? "aa" [res: 3 #a]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? "a" [res: [#a]]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "a" [res: wa]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "aa" [res: 2 wa]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "aab" [<any> res: #a <any>]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "aab" [<any> res: [#a | #b] <any>]
            res = #a
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? "a" [res: [#c | #b]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? "baaac" [<any> res: some #a #c]
            res = #a
        ]
    )
    (
        res: ~
        did all [
            uparse? "baaac" [<any> res: some wa #c]
            res = #a
        ]
    )
]

; More "string" tests from %parse-test.red
[
    (
        res: ~
        did all [
            uparse? "" [(res: 1)]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            uparse? "a" [#a (res: 1)]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? "a" [#b (res: 1)]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? "" [[(res: 1)]]
            res = 1
        ]
    )
    (
        res: ~
        did all [
            uparse? "a" [[#a (res: 1)]]
            res = 1
        ]
    )
    (
        res: '~before~
        did all [
            not uparse? "a" [[#b (res: 1)]]
            res = '~before~
        ]
    )
    (
        res: ~
        did all [
            uparse? "ab" [#a (res: 1) [#c (res: 2) | #b (res: 3)]]
            res = 3
        ]
    )
    (
        res: ~
        did all [
            not uparse? "ab" [#a (res: 1) [#c (res: 2) | #d (res: 3)]]
            res = 1
        ]
    )
    (not uparse? "aa" [1 [#a]])
    (uparse? "aa" [2 [#a]])
    (not uparse? "aa" [3 [#a]])

;    (not uparse? "aa" [1 1 [#a]])
;    (uparse? "aa" [1 2 [#a]])
;    (uparse? "aa" [2 2 [#a]])
;    (uparse? "aa" [2 3 [#a]])
;    (not uparse? "aa" [3 4 [#a]])
;    (not uparse? "aa" [1 1 #a])
;    (uparse? "aa" [1 2 #a])
;    (uparse? "aa" [2 2 #a])
;    (uparse? "aa" [2 3 #a])
;    (not uparse? "aa" [3 4 #a])

    (not uparse? "aa" [1 #a])
    (uparse? "aa" [2 #a])
    (not uparse? "aa" [3 #a])
    (uparse? "aa" [while [#a]])
    (uparse? "aa" [some [#a] while [#b]])
    (uparse? "aabb" [2 #a 2 "b"])
    (not uparse? "aabb" [2 "a" 3 #b])
]
