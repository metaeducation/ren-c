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

; It's rare to have code used in object construction that has top-level
; SET-WORD where the binding is supposed to be heeded
[
    (
        obj: make object! [a: 10 b: 20]
        z: 30
        ok
    )
    (all [
        let e: sys.util/rescue [extend obj compose $() '[(setify $z) 300 c: 40]]
        e.id = 'collectable-bound
        e.arg1 = 'z:
        z = 30
        obj = make object! [a: 10 b: 20]
    ])
    (all [
        extend:prebound obj compose $() '[(setify $z) 300 c: 40]
        z = 300
        obj = make object! [a: 10 b: 20 c: 40]
    ])
]
