; datatypes/lit-path.r
(lit-path? first ['a/b])
(not lit-path? 1)
(lit-path! = type of first ['a/b])
; minimum
[#1947
    (lit-path? load "#[lit-path! [[a] 1]]")
]
(
    all [
        lit-path? a: load "#[lit-path! [[a b c] 2]]"
        2 == index? a
    ]
)
; lit-paths are active
(
    a-value: first ['a/b]
    strict-equal? to path! :a-value do reduce [:a-value]
)
