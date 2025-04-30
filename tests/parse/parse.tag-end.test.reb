; %parse-tag-end.test.reb
;
; In UPARSE, the <end> tag should be used.  This frees up the END
; word for variables (like start and end for ranges being copied, for
; example, or begin and end, etc.)
;
; It vanishes, because this is the overwhelmingly most useful behavior.


; BLOCK! end tests from %parse-test.red
[
    (
        block: [a]
        'a = parse block ['a <end>]
    )
    ('a = parse [a] [one <end>])
    (void? parse [] [<end>])

    ~parse-mismatch~ !! (parse [a b] ['a <end>])
    ~parse-mismatch~ !! (parse [a b] [one <end>])

    (
        be6: ~
        all [
            1 = parse [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; TEXT! end tests from %parse-test.red
[
    (
        text: "a"
        #a = parse text [#a <end>]
    )
    (#a = parse "a" [one <end>])
    (void? parse "" [<end>])

    ~parse-mismatch~ !! (parse "ab" [#a <end>])
    ~parse-mismatch~ !! (parse "ab" [one <end>])

    (
        be6: ~
        all [
            1 = parse "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; BLOB! end tests from %parse-test.red
[
    (
        binary: #{0A}
        #{0A} = parse #{0A} [#{0A} <end>]
    )
    (10 = parse #{0A} [one <end>])
    (void? parse #{} [<end>])

    ~parse-mismatch~ !! (parse #{0A0B} [#{0A} <end>])
    ~parse-mismatch~ !! (parse #{0A0B} [one <end>])

    (
        be6: ~
        all [
            1 = parse #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]
