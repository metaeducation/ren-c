; %parse-tag-here.test.r
;
; As alternatives to using SET-WORD! to set the parse position and GET-WORD!
; to get the parse position, Ren-C has <here> and the SEEK keyword.  <HERE>
; follows Topaz precedent as the new means of capturing positions
; (e.g. POS: <HERE>).  But it is useful for other purposes, when a rule is
; needed for capturing the current position.
;
; https://github.com/giesse/red-topaz-parse
;
; For rationale on why it is a TAG! and not simply the word HERE, see:
;
; https://forum.rebol.info/t/tag-s-as-parse-keywords-vs-literal-matches/1558/5
;
; There was a phase where it was just HERE, and for temporary compatibility
; the LIB word HERE is defined as `<here>` to keep that working.  This crutch
; will eventually be removed.

[(
    all [
        let pos
        "b" = parse "aaabbb" [some "a", pos: <here>, some "b"]
        pos = "bbb"
    ]
)(
    all [
        let [left n x]
        "stuff" = parse "<<<stuff>>>" [
            left: across some "<"
            (n: length of left)
            x: between <here> repeat (n) ">"
        ]
        x = "stuff"
    ]
)]

(all wrap [
    pos: ~
    [x y] = res: parse ser: [x y] [pos: <here>, elide [one, one]]
    pos = ser
])
(all wrap [
    pos: ~
    [y] = res: parse ser: [x y] [one, pos: <here>, elide one]
    pos = next ser
])
(all wrap [
    pos: ~
    [] = res: parse ser: [x y] [one, one, pos: <here>]
    pos = tail of ser
])
[#2130 (
    all wrap [
        pos: val: ~
        'x = res: parse ser: [x] [pos: <here>, val: word!]
        val = 'x
        pos = ser
    ]
)(
    all wrap  [
        pos: val: ~
        'true = res: parse ser: "foo" [
            pos: <here>
            val: across one
            accept ('true)
        ]
        val = "f"
        pos = ser
    ]
)]

; Should return the same series type as input (Rebol2 did not do this)
; PATH! cannot be PARSE'd due to restrictions of the implementation
(
    a-value: first [a/b]
    b-value: ~
    all [
        error? parse as block! a-value [b-value: <here>]
        a-value = as path! b-value
    ]
)
(
    a-value: first [()]
    b-value: ~
    all [
        '() = parse a-value [b-value: <here>]
        same? a-value b-value
    ]
)

; TEXT! tests derived from %parse-test.red
[
    (
        p: ~
        all [
            "" = parse "" [p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            "" = parse "" [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            #a = parse "a" [p: <here> #a]
            p = "a"
        ]
    )
    (
        p: ~
        all [
            "" = parse "a" [#a p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            "" = parse "a" [#a [p: <here>]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            error? parse "ab" [#a p: <here>]
            p = "b"
        ]
    )
    (
        p: ~
        all [
            #b = parse "ab" [#a [p: <here>] [#b | #c]]
            p = "b"
        ]
    )
    (
        p: ~
        all [
            "b" = parse "aaabb" [
                repeat 3 #a p: <here> repeat 2 #b seek (p) [repeat 2 "b"]
            ]
            p = "bb"
        ]
    )
]

; BLOCK! tests derived from %parse-test.red
[
    (
        p: ~
        all [
            [] = parse [] [p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            [] = parse [] [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            'a = parse [a] [p: <here> 'a]
            p = [a]
        ]
    )
    (
        p: ~
        all [
            [] = parse [a] ['a p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            [] = parse [a] ['a [p: <here>]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            error? parse [a b] ['a p: <here>]
            p = [b]
        ]
    )
    (
        p: ~
        all [
            'b = parse [a b] ['a [p: <here>] ['b | 'c]]
            p = [b]
        ]
    )
    (
        p: ~
        all [
            'b = parse [a a a b b] [
                repeat 3 'a p: <here> repeat 2 'b seek (p) [repeat 2 'b]
            ]
            p = [b b]
        ]
    )
]

; BLOB! tests derived from %parse-test.red
[
    (
        p: ~
        all [
            #{} = parse #{} [p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            #{} = parse #{} [[[p: <here>]]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            #{0A} = parse #{0A} [p: <here> #{0A}]
            p = #{0A}
        ]
    )
    (
        p: ~
        all [
            #{} = parse #{0A} [#{0A} p: <here>]
            tail? p
        ]
    )
    (
        p: ~
        all [
            #{} = parse #{0A} [#{0A} [p: <here>]]
            tail? p
        ]
    )
    (
        p: ~
        all [
            error? parse #{0A0B} [#{0A} p: <here>]
            p = #{0B}
        ]
    )
    (
        p: ~
        all [
            #{0B} = parse #{0A0B} [#{0A} [p: <here>] [#{0B} | #"^L"]]
            p = #{0B}
        ]
    )
    (
        p: ~
        all [
            #{0B} = parse #{0A0A0A0B0B} [
                repeat 3 #{0A}
                p: <here>
                repeat 2 #{0B}
                seek (p)
                [repeat 2 #{0B}]
            ]
            p = #{0B0B}
        ]
    )
]
