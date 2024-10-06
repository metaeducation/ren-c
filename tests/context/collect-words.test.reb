; test suite for COLLECT-WORDS

( ; base case
    words: collect-words [a a 'b c: [d] e/f 1 "" % ] []
    empty? difference words [a b c]
)

( ; :SET
    words: collect-words:set [a 'b c: d:]
    empty? difference words [c d]
)

( ; :DEEP
    words: collect-words:deep [a ['b [c:]]]
    empty? difference words [a b c]
)

( ; :IGNORE
    words: collect-words:ignore [a 'b c:] [c]
    empty? difference words [a b]
)

( ; :DEEP :SET :IGNORE
    words: collect-words:deep:set:ignore [a [b: [c:]]] [c]
    empty? difference words [b]
)

; vim: set expandtab sw=4 ts=4:
