; functions/control/reduce.r
([1 2] = reduce [1 1 + 1])
(
    success: 'false
    reduce [elide success: 'true]
    true? success
)
([] = reduce [])
("1 + 1" = reduce "1 + 1")
(error? first reduce [trap [1 / 0]])
[#1760 ; unwind functions should stop evaluation
    (null? repeat 1 [reduce [break]])
]
('~[~void~]~ = ^ repeat 1 [reduce [continue]])
(1 = catch [reduce [throw 1]])

; There used to be a multi-return situation where the name of a throw was
; returned as a block, e.g. this would produce [1 a].  When multi-return
; was introduced, the name was at first a secondary result...until the
; convenience of throwing packs was decided as better.
;
(1 = catch/name [reduce [throw/name 1 'a]] 'a)
(~['1 '2]~ = catch [throw pack [1 2]])

(1 = reeval unrun func [return: [integer!]] [reduce [return 1 2] 2])
; recursive behaviour
(1 = first reduce [first reduce [1]])

; infinite recursion
(
    x: 0
    blk: [x: x + 1, if x = 5000 [throw <deep-enough>] reduce blk]
    <deep-enough> = catch blk
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


~need-non-null~ !! (
    reduce [null]
)
([] = reduce [maybe null])

~need-non-null~ !! (
    reduce [~null~]
)
([] = reduce [maybe ~null~])


([] = reduce [void])


; === PREDICATES ===

; There was a bug pertaining to trying to set the new line flag on the output
; in the case of a non-existent null, test that.
[
    ([] = reduce [
        maybe null
    ])
    ([ZOMG <!!!> 1020 #wow] = apply get $reduce [
        ['ZOMG null 1000 + 20 #wow]
        /predicate lambda [x] [any [x, <!!!>]]
    ])
]

~expect-arg~ !! (reduce/predicate [null] cascade [get $null?, get $non])

; Voids are offered, but omitted if predicate doesn't take them.
; https://forum.rebol.info/t/should-void-be-offered-to-predicates-for-reduce-any-all-etc/1872
;
([3 ~void~ 300] = reduce/predicate [
    1 + 2 if null [10 + 20] 100 + 200
] get $reify)

([-3 -300] = reduce/predicate [1 + 2 if null [10 + 20] 100 + 200] get $negate)
([3 300] = reduce/predicate [1 + 2 if null [10 + 20] 100 + 200] get $maybe)

([3 ~null~ 300] = reduce/predicate [1 + 2 if ok [null] 100 + 200] get $reify)
([3 300] = reduce/predicate [1 + 2 if ok [null] 100 + 200] get $maybe)

([3 ~null~ 300] = reduce/predicate [1 + 2 null 100 + 200] get $reify)
([3 300] = reduce/predicate [1 + 2 null 100 + 200] get $maybe)

; REDUCE* is a specialization of REDUCE with MAYBE
;
([3 300] = reduce* [1 + 2 null 100 + 200])

~bad-antiform~ !! (reduce/predicate [1 + 2 3 + 4] func [x] [x * 10])
([30 70] = reduce/predicate [1 + 2 3 + 4] func [x] [return x * 10])

([~okay~ ~null~] = reduce/predicate [2 + 2 3 + 4] cascade [
    get $even?, get $reify
])

([true false] = reduce/predicate [2 + 2 3 + 4] cascade [
    get $even?, get $boolean
])


; REDUCE-EACH is a variant which lets you intercept the values, and thus
; intervene in questions of how unstable antiforms will be handled.
[
    ([<null> 3] = collect [reduce-each x [null 1 + 2] [keep (x else '<null>)]])

    ([3 7] = collect [reduce-each x [1 + 2 3 + 4] [keep x]])

    ([1 + 2] = collect [reduce-each x @[1 + 2] [keep x]])
]

; REDUCE-EACH can do ^META processing, this is the basis of ATTEMPT
[
    ((the '3) = reduce-each ^x [1 + 2] [x])

    (
        e: reduce-each ^x [raise "foo"] [unquasi x]
        e.message = "foo"
    )
]

; SPREAD is honored by REDUCE
[
    ([1 2 3 4] = reduce [1 if ok [spread [2 3]] 4])
]
