; %parse-tag-end.test.reb
;
; In UPARSE, the <end> tag should be used.  This frees up the END
; word for variables (like start and end for ranges being copied, for
; example, or begin and end, etc.)
;
; It vanishes, because this is the overwhelmingly most useful behavior.
; e.g. recognizing a single element in a block can be done with [<any> <end>].


; BLOCK! end tests from %parse-test.red
[
    (
        block: [a]
        'a = parse block ['a <end>]
    )
    ('a == parse [a] [<any> <end>])
    (void? parse [] [<end>])

    ~parse-mismatch~ !! (parse [a b] ['a <end>])
    ~parse-mismatch~ !! (parse [a b] [<any> <end>])

    (
        be6: ~
        all [
            1 == parse [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; TEXT! end tests from %parse-test.red
[
    (
        text: "a"
        #a == parse text [#a <end>]
    )
    (#a == parse "a" [<any> <end>])
    (void? parse "" [<end>])

    ~parse-mismatch~ !! (parse "ab" [#a <end>])
    ~parse-mismatch~ !! (parse "ab" [<any> <end>])

    (
        be6: ~
        all [
            1 == parse "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; BINARY! end tests from %parse-test.red
[
    (
        binary: #{0A}
        #{0A} == parse #{0A} [#{0A} <end>]
    )
    (10 == parse #{0A} [<any> <end>])
    (void? parse #{} [<end>])

    ~parse-mismatch~ !! (parse #{0A0B} [#{0A} <end>])
    ~parse-mismatch~ !! (parse #{0A0B} [<any> <end>])

    (
        be6: ~
        all [
            1 == parse #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]
