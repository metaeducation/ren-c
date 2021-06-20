; functions/control/reduce.r
([1 2] = reduce [1 1 + 1])
(
    success: false
    reduce [success: true]
    success
)
([] = reduce [])
("1 + 1" = reduce "1 + 1")
(error? first reduce [trap [1 / 0]])
[#1760 ; unwind functions should stop evaluation
    (null? repeat 1 [reduce [break]])
]
('~null~ = ^ repeat 1 [reduce [continue]])
(1 = catch [reduce [throw 1]])
([a 1] = catch/name [reduce [throw/name 1 'a]] 'a)
(1 = reeval func [] [reduce [return 1 2] 2])
; recursive behaviour
(1 = first reduce [first reduce [1]])
; infinite recursion
(
    blk: [reduce blk]
    error? trap blk
)

; Quick flatten test, here for now
(
    [a b c d e f] = flatten [[a] [b] c d [e f]]
)
(
    [a b [c d] c d e f] = flatten [[a] [b [c d]] c d [e f]]
)
(
    [a b c d c d e f] = flatten/deep [[a] [b [c d]] c d [e f]]
)

; Invisibles tests
[
    ([] = reduce [comment <ae>])
    ([304 1020] = reduce [comment <AE> 300 + 4 1000 + 20])
    ([304 1020] = reduce [300 + 4 comment <AE> 1000 + 20])
    ([304 1020] = reduce [300 + 4 1000 + 20 comment <AE>])
]


; === PREDICATES ===
;
; Predicates influence the handling of NULLs, which vaporize by default.

([] = reduce [null])

; There was a bug pertaining to trying to set the new line flag on the output
; in the case of a non-existent null, test that.
[
    ([] = reduce [
        null
    ])
    ([] = reduce .identity [
        null
    ])
]

(error? trap [reduce .non.null [null]])

([3 _ 300] = reduce .try [1 + 2 if false [10 + 20] 100 + 200])
([3 ~null~ 300] = reduce .reify [1 + 2 if false [10 + 20] 100 + 200])
([3 300] = reduce .identity [1 + 2 if false [10 + 20] 100 + 200])

([#[true] #[false]] = reduce .even? [2 + 2 3 + 4])
