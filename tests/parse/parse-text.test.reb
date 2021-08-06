; %parse-text.test.reb
;
; A TEXT! rule will capture the actual match in a block.  But for a string, it
; will capture the *rule*.

; No-op rules need thought in UPARSE, in terms of NULL/BLANK! behavior.  But
; empty string should be a no-op on string input, and an empty rule should
; always match.
;
("" = uparse "" [""])

(uparse? "abc" ["abc"])
(uparse? "abc" ["abc" <end>])

; Ren-C does not mandate that rules make progress, so matching empty strings
; works, as it does in Red.
[
    (uparse? "ab" [to [""] "ab"])
    (uparse? "ab" [to ["a"] "ab"])
    (uparse? "ab" [to ["ab"] "ab"])
    (uparse? "ab" [thru [""] "ab"])
    (uparse? "ab" [thru ["a"] "b"])
    (uparse? "ab" [thru ["ab"] ""])
]

[(
    rule: [x: "a"]
    did all [
        uparse? "a" rule
        same? x second rule
    ]
)(
    data: ["a"]
    rule: [x: "a"]
    did all [
        uparse? data rule
        same? x first data
    ]
)]

; Multi-byte characters and strings present a lot of challenges.  There should
; be many more tests and philosophies written up of what the semantics are,
; especially when it comes to BINARY! and ANY-STRING! mixtures.  These tests
; are better than nothing...
(
    catchar: #"🐱"
    uparse? #{F09F90B1} [catchar]
)(
    cattext: "🐱"
    uparse? #{F09F90B1} [cattext]
)(
    catbin: #{F09F90B1}
    e: trap [uparse? "🐱" [catbin]]
    'find-string-binary = e/id
)(
    catchar: #"🐱"
    uparse? "🐱" [catchar]
)

[
    (
        bincat: to-binary {C😺T}
        bincat = #{43F09F98BA54}
    )

    (uparse? bincat [{C😺T}])

    (uparse? bincat [{c😺t}])

    (not uparse?/case bincat [{c😺t} <end>])
]

(
    test: to-binary {The C😺T Test}
    did all [
        uparse? test [to {c😺t} x: across to space to <end>]
        x = #{43F09F98BA54}
        "C😺T" = to-text x
    ]
)
