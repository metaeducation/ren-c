; %parse-tag-here.test.reb
;
; As alternatives to using SET-WORD! to set the uparse position and GET-WORD!
; to get the uparse position, Ren-C has <here> and the SEEK keyword.  HERE
; follows Topaz precedent as the new means of capturing positions
; (e.g. POS: HERE).  But it is useful for other purposes, when a rule is
; needed for capturing the current position.
;
; https://github.com/giesse/red-topaz-uparse

[(
    did all [
        uparse? "aaabbb" [some "a", pos: <here>, some "b"]
        pos = "bbb"
    ]
)(
    did all [
        uparse? "<<<stuff>>>" [
            left: across some "<"
            (n: length of left)
            x: between <here> repeat (n) ">"
        ]
        x = "stuff"
    ]
)]

(
    res: uparse? ser: [x y] [pos: <here>, skip, skip]
    all [res, pos = ser]
)
(
    res: uparse? ser: [x y] [skip, pos: <here>, skip]
    all [res, pos = next ser]
)
(
    res: uparse? ser: [x y] [skip, skip, pos: <here>]
    all [res, pos = tail of ser]
)
[#2130 (
    res: uparse? ser: [x] [pos: <here>, val: word!]
    all [res, val = 'x, pos = ser]
)(
    res: uparse? ser: "foo" [pos: <here>, val: across <any>]
    all [not res, val = "f", pos = ser]
)]

; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    uparse as block! a-value [b-value: <here>]
    a-value = to path! b-value
)
(
    a-value: first [()]
    uparse a-value [b-value: <here>]
    same? a-value b-value
)

; TEXT! tests derived from %parse-test.red
[
    (
        p: blank
        did all [
            uparse? "" [p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "" [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [p: <here> #a]
            p = "a"
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [#a p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? "a" [#a [p: <here>]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            not uparse? "ab" [#a p: <here>]
            p = "b"
        ]
    )
    (
        p: blank
        did all [
            uparse? "ab" [#a [p: <here>] [#b | #c]]
            p = "b"
        ]
    )
    (
        p: blank
        did all [
            uparse? "aaabb" [3 #a p: <here> 2 #b seek (p) [2 "b"]]
            p = "bb"
        ]
    )
]

; BLOCK! tests derived from %parse-test.red
[
    (
        p: blank
        did all [
            uparse? [] [p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [] [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] [p: <here> 'a]
            p = [a]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] ['a p: <here>]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            uparse? [a] ['a [p: <here>]]
            tail? p
        ]
    )
    (
        p: blank
        did all [
            not uparse? [a b] ['a p: <here>]
            p = [b]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a b] ['a [p: <here>] ['b | 'c]]
            p = [b]
        ]
    )
    (
        p: blank
        did all [
            uparse? [a a a b b] [3 'a p: <here> 2 'b seek (p) [2 'b]]
            p = [b b]
        ]
    )
]

; BINARY! tests derived from %parse-test.red
[
    (
        p: ~
        did all [
            uparse? #{} [p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        did all [
            uparse? #{} [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: ~
        did all [
            uparse? #{0A} [p: <here> #{0A}]
            p = #{0A}
        ]
    )
    (
        p: ~
        did all [
            uparse? #{0A} [#{0A} p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        did all [
            uparse? #{0A} [#{0A} [p: <here>]]
            tail? p
        ]
    )
    (
        p: ~
        did all [
            not uparse? #{0A0B} [#{0A} p: <here>]
            p = #{0B}
        ]
    )
    (
        p: ~
        did all [
            uparse? #{0A0B} [#{0A} [p: <here>] [#{0B} | #"^L"]]
            p = #{0B}
        ]
    )
    (
        p: ~
        did all [
            uparse? #{0A0A0A0B0B} [3 #{0A} p: <here> 2 #{0B} seek (p) [2 #{0B}]]
            p = #{0B0B}
        ]
    )
]
