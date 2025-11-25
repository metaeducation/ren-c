; %loops/remove-each.test.r
;
; Ren-C's REMOVE-EACH is a multi-return routine which gives you the updated
; series as the main result, and the count as a secondary return.  This
; nicely resolves a historically contentious issue of which it should return.
;
;   https://github.com/metaeducation/rebol-issues/issues/931

(
    all wrap [
        s: [1 2]
        s = [_ count]: remove-each 'i s [okay]
        empty? s
        count = 2
    ]
)
(
    all wrap [
        s: [1 2]
        s = [_ count]: remove-each 'i s: [1 2] [null]
        [1 2] = s
        count = 0
    ]
)

; BLOCK!
(
    block: copy [1 2 3 4]
    remove-each 'i block [
        all [i > 1, i < 4]
    ]
    block = [1 4]
)

; !!! REMOVE-EACH currently has no effect on BREAK.  This is because as part
; of the loop protocol, it returns NULL and thus cannot return the number
; of removed elements.
(
    block: copy [1 2 3 4]
    remove-each 'i block [
        if i = 3 [break]
        ok
    ]
    block = [1 2 3 4]
)
(
    block: copy [1 2 3 4]
    returned-null: 'no
    remove-each 'i block [
        if i = 3 [break]
        i = 2
    ] else [
        returned-null: 'yes
    ]
    all [
        block = [1 2 3 4]
        yes? returned-null
    ]
)
(
    block: copy [1 2 3 4]
    remove-each 'i block [
        if i = 3 [continue:with okay]
        if i = 4 [okay] else [null]
    ]
    block = [1 2]
)
(
    block: copy [1 2 3 4]
    sys.util/recover [
        remove-each 'i block [
            if i = 3 [panic "midstream panic"]
            okay
        ]
    ]
    block = [3 4]
)
(
    was-b-null: 'no

    block: copy [1 2 3 4 5]
    remove-each [a b] block [
        if a = 5 [
            was-b-null: to-yesno null? b
        ]
        continue
    ]
    yes? was-b-null
)

; STRING!
(
    string: copy "1234"
    remove-each 'i string [
        any [i = #"2", i = #"3"]
    ]
    string = "14"
)
(
    string: copy "1234"
    returned-null: 'false
    remove-each 'i string [
        if i = #"3" [break]
        okay
    ] else [
        returned-null: 'true
    ]
    all [
        string = "1234" comment "not changed if BREAK"
        returned-null = 'true
    ]
)
(
    string: copy "1234"
    sys.util/recover [
        remove-each 'i string [
            if i = #"3" [panic "midstream panic"]
            okay
        ]
    ]
    string = "34"
)
(
    b-was-null: 'false

    string: copy "12345"
    remove-each [a b] string [
        if a = #"5" [
            true? b-was-null: boolean null? b
        ]
    ]
    true? b-was-null
)
(
    string: "cօʊʀֆօռǟɢɢօռi"
    remove-each 'x string [
        x = #"օ"
    ]
    string = "cʊʀֆռǟɢɢռi"
)

; BLOB!
(
    binary: copy #{01020304}
    remove-each 'i binary [
        any [i = 2, i = 3]
    ]
    binary = #{0104}
)
(
    binary: copy #{01020304}
    returned-null: 'false
    remove-each 'i binary [
        if i = 3 [break]
        okay
    ] else [
        returned-null: 'true
    ]
    all [
        binary = #{01020304} comment "Not changed with BREAK"
        returned-null = 'true
    ]
)
(
    binary: copy #{01020304}
    sys.util/recover [
        remove-each 'i binary [
            if i = 3 [panic "midstream panic"]
            okay
        ]
    ]
    binary = #{0304}
)
(
    b-was-null: 'false

    binary: copy #{0102030405}
    remove-each [a b] binary [
        if a = 5 [
            true? b-was-null: boolean null? b
        ]
    ]
    true? b-was-null
)

; You can opt out of the series input with a void
(
    null = remove-each 'x ^void [okay]
)
