; functions/control/reduce.r
([1 2] = reduce [1 1 + 1])
(
    success: false
    reduce [success: true]
    success
)
([] = reduce [])
(error? try [first reduce [()]])
("1 + 1" = reduce "1 + 1")
(error? first reduce [try [1 / 0]])
[#1760 ; unwind functions should stop evaluation
    (blank? loop 1 [reduce [break]])
]
(bar? loop 1 [reduce [continue]])
(1 = catch [reduce [throw 1]])
(1 = catch/name [reduce [throw/name 1 'a]] 'a)
(1 = eval does [reduce [return 1 2] 2])
(void? if 1 < 2 [eval does [reduce [unwind :if 1] 2]])
; recursive behaviour
(1 = first reduce [first reduce [1]])
; infinite recursion
(
    blk: [reduce blk]
    error? try blk
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
