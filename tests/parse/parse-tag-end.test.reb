; %parse-tag-end.test.reb
;
; In UPARSE, the <end> tag should be used.  This frees up the END
; word for variables (like start and end for ranges being copied, for
; example, or begin and end, etc.)


; BLOCK! end tests from %parse-test.red
[
    (
        block: [a]
        (tail block) = uparse block ['a <end>]
    )
    (didn't uparse [a b] ['a <end>])
    ([] == uparse [a] [<any> <end>])
    (didn't uparse [a b] [<any> <end>])
    ([] == uparse [] [<end>])
    (
        be6: ~
        did all [
            1 == uparse [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; TEXT! end tests from %parse-test.red
[
    (
        text: "a"
        (tail text) == uparse text [#a <end>]
    )
    (didn't uparse "ab" [#a <end>])
    ("" == uparse "a" [<any> <end>])
    (didn't uparse "ab" [<any> <end>])
    ("" == uparse "" [<end>])
    (
        be6: ~
        did all [
            1 == uparse "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; BINARY! end tests from %parse-test.red
[
    (
        binary: #{0A}
        (tail binary) == uparse #{0A} [#{0A} <end>]
    )
    (didn't uparse #{0A0B} [#{0A} <end>])
    (#{} == uparse #{0A} [<any> <end>])
    (didn't uparse #{0A0B} [<any> <end>])
    (#{} == uparse #{} [<end>])
    (
        be6: ~
        did all [
            1 == uparse #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]
