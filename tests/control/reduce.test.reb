; functions/control/reduce.r
([1 2] = reduce [1 1 + 1])
(
    success: null
    'bad-antiform = (sys/util/rescue [reduce [success: okay]])/id
)
([] = reduce [])
(error? sys/util/rescue [first reduce [null]])
("1 + 1" = reduce "1 + 1")
[#1760 ; unwind functions should stop evaluation
    (null? repeat 1 [reduce [break]])
]
(trash? repeat 1 [reduce [continue]])
(1 = catch [reduce [throw 1]])
([a 1] = catch/name [reduce [throw/name 1 'a]] 'a)
(1 = reeval func [] [reduce [return 1 2] 2])
(null? if 1 < 2 [reeval does [reduce [unwind :if 1] 2]])
; recursive behaviour
(1 = first reduce [first reduce [1]])
; infinite recursion
(
    blk: [reduce blk]
    error? sys/util/rescue blk
)

[
    (did blk: [1 + 2 null 100 + 200])
    ('bad-antiform = (sys/util/rescue [reduce blk])/id)
]

[
    (did blk: [1 + 2 when null [10 + 20] 100 + 200])
    ([3 300] = reduce blk)
]

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
