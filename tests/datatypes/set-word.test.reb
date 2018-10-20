; datatypes/set-word.r
(set-word? first [a:])
(not set-word? 1)
(set-word! == type of first [a:])
; set-word is active
(
    a: :abs
    :a == :abs
)
(
    a: #{}
    :a == #{}
)
(
    a: charset ""
    :a == charset ""
)
(
    a: []
    a == []
)
(
    a: action!
    :a == action!
)
[#1817 (
    a: make map! []
    a/b: make object! [
        c: make map! []
    ]
    integer? a/b/c/d: 1
)]

[#1477 (
    e: trap [load "/:"]
    error? e and [e/id is 'Scan-Invalid]
)]

; These are comments in Ren-C
;
(load "//:" == [])
(load "///:" == [])
