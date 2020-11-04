; datatypes/lit-word.r
;
; !!! LIT-WORD! is no longer an independent data type, as all quoted values
; are now of type QUOTED!.  So LIT-WORD! is actually a "type constraint"
; of singly-quoted words (QUOTED-WORD! would be a better name)
;

(lit-word? first ['a])
(not lit-word? 1)
((quote word!) = type of first ['a])

; lit-words are active
(
    a-value: first ['a]
    strict-equal? to word! unquote :a-value do reduce [:a-value]
)

[#1342
    (word? '<)
]

(word? '>)
(word? '<=)
(word? '>=)
(word? '<>)
