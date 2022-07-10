; %parse-tag-end.test.reb
;
; In UPARSE, the <end> tag should be used.  This frees up the END
; word for variables (like start and end for ranges being copied, for
; example, or begin and end, etc.)


; BLOCK! end tests from %parse-test.red
[
    (
        block: [a]
        (tail block) = parse block ['a <end>]
    )
    (didn't parse [a b] ['a <end>])
    ([] == parse [a] [<any> <end>])
    (didn't parse [a b] [<any> <end>])
    ([] == parse [] [<end>])
    (
        be6: ~
        did all [
            1 == parse [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; TEXT! end tests from %parse-test.red
[
    (
        text: "a"
        (tail text) == parse text [#a <end>]
    )
    (didn't parse "ab" [#a <end>])
    ("" == parse "a" [<any> <end>])
    (didn't parse "ab" [<any> <end>])
    ("" == parse "" [<end>])
    (
        be6: ~
        did all [
            1 == parse "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; BINARY! end tests from %parse-test.red
[
    (
        binary: #{0A}
        (tail binary) == parse #{0A} [#{0A} <end>]
    )
    (didn't parse #{0A0B} [#{0A} <end>])
    (#{} == parse #{0A} [<any> <end>])
    (didn't parse #{0A0B} [<any> <end>])
    (#{} == parse #{} [<end>])
    (
        be6: ~
        did all [
            1 == parse #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]
