; %parse-tag-end.test.reb
;
; In UPARSE, the <end> tag should be used.  This frees up the END
; word for variables (like start and end for ranges being copied, for
; example, or begin and end, etc.)


; BLOCK! end tests from %parse-test.red
[
    (uparse? [a] ['a <end>])
    (not uparse? [a b] ['a <end>])
    (uparse? [a] [<any> <end>])
    (not uparse? [a b] [<any> <end>])
    (uparse? [] [<end>])
    (
        be6: ~
        did all [
            uparse? [] [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; TEXT! end tests from %parse-test.red
[
    (uparse? "a" [#a <end>])
    (not uparse? "ab" [#a <end>])
    (uparse? "a" [<any> <end>])
    (not uparse? "ab" [<any> <end>])
    (uparse? "" [<end>])
    (
        be6: ~
        did all [
            uparse? "" [<end> (be6: 1)]
            be6 = 1
        ]
    )
]

; BINARY! end tests from %parse-test.red
[
    (uparse? #{0A} [#{0A} <end>])
    (not uparse? #{0A0B} [#{0A} <end>])
    (uparse? #{0A} [<any> <end>])
    (not uparse? #{0A0B} [<any> <end>])
    (uparse? #{} [<end>])
    (
        be6: ~
        did all [
            uparse? #{} [<end> (be6: 1)]
            be6 = 1
        ]
    )
]
