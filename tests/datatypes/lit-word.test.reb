; datatypes/lit-word.r
(lit-word? first ['a])
(not lit-word? 1)
(lit-word! = type of first ['a])
; lit-words are active
(
    a-value: first ['a]
    strict-equal? to word! :a-value eval reduce [:a-value]
)
[#1342
    (word? '<)
]
(word? '>)
(word? '<=)
(word? '>=)
(word? '<>)
