; SET-GROUP!
; What these might do in PARSE could be more ambitious, but for starters they
; provide a level of indirection in SET.

(
    m: ~
    word: 'm
    did all [
        did parse [1020] [(word): integer!]
        word = 'm
        m = 1020
    ]
)
(
    sum: ~
    m: <unchanged> 
    word: 'm
    did all [
        didn't parse [1020] [(sum: 1 + 2, word): text!]
        word = 'm
        sum = 3
        m = <unchanged>
    ]
)
