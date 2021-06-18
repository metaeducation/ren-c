; Note: LIT-PATH! no longer exists as a distinct datatype.  It is defined for
; compatibility purposes under the mechanisms of fully generalized quoting:
;
; https://forum.rebol.info/t/quoted-arrives-formerly-known-as-lit-bit/995

(lit-path? first ['a/b])
(not lit-path? 1)
((quote path!) = type of first ['a/b])

; minimum

[#1947
    (lit-path? quote load-value "#[path! [[a] 1]]")
]

; lit-paths are active
(
    a-value: first ['a/b]
    strict-equal? as path! noquote :a-value do reduce [:a-value]
)
