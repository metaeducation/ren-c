; functions/control/reduce.r
([1 2] = reduce [1 1 + 1])
([] = reduce [])
("1 + 1" = reduce "1 + 1")
(error? first reduce [rescue [1 / 0]])
[#1760 ; unwind functions should stop evaluation
    (null? repeat 1 [reduce [break]])
]
(heavy-void? repeat 1 [reduce [continue]])
(1 = catch [reduce [throw 1]])

; There used to be a multi-return situation where the name of a throw was
; returned as a block, e.g. this would produce [1 a].  When multi-return
; was introduced, the name was at first a secondary result...until the
; convenience of throwing packs was decided as better.
;
(~('1 '2)~ = catch [throw pack [1 2]])

(1 = reeval unrun func [return: [integer!]] [reduce [return 1 2] 2])
; recursive behaviour
(1 = first reduce [first reduce [1]])

; infinite recursion
(
    <deep-enough> = catch {
        x: 0
        blk: [x: x + 1, if x = 5000 [throw <deep-enough>] reduce blk]
        eval blk
    }
)

; Antiforms no longer tolerated unless passed to predicate
[
    ~???~ !! (reduce [comment <ae>])
    ~???~ !! (reduce [opt ~null~])

    (['1 ~] = reduce:predicate [1 elide <vaporize>] lift/)

    (
        success: 'false
        reduce:predicate [elide success: 'true] identity/
        true? success
    )
]


~bad-antiform~ !! (
    reduce [null]
)

~bad-antiform~ !! (
    reduce [~null~]
)



; === PREDICATES ===

; There was a bug pertaining to trying to set the new line flag on the output
; in the case of a non-existent null, test that.
[
    ([ZOMG <!!!> 1020 #wow] = apply reduce/ [
        ['ZOMG null 1000 + 20 #wow]
        predicate: lambda [x] [any [x, <!!!>]]
    ])
]

~expect-arg~ !! (reduce:predicate [null] cascade [null?/ non/])

(['3 ~ '300] = reduce:predicate [
    1 + 2 ^void 100 + 200
] lift/)

([-3 -300] = reduce // [
    [1 + 2 if null [10 + 20] 100 + 200]
    predicate: func [^x] [if void? ^x [return ~] return negate x]
])
([3 300] = reduce:predicate [1 + 2 if null [10 + 20] 100 + 200] opt/)

([3 ~null~ 300] = reduce:predicate [1 + 2 if ok [null] 100 + 200] reify/)
([3 300] = reduce:predicate [1 + 2 if ok [null] 100 + 200] opt/)

([3 ~null~ 300] = reduce:predicate [1 + 2 null 100 + 200] reify/)
([3 300] = reduce:predicate [1 + 2 null 100 + 200] opt/)

~expect-arg~ !! (reduce:predicate [1 + 2 null 3 + 4] lambda [x] [x * 10])
([30 70] = reduce:predicate [1 + 2 3 + 4] func [x] [return x * 10])

([~okay~ ~null~] = reduce:predicate [2 + 2 3 + 4] cascade [even?/ reify/])

([true false] = reduce:predicate [2 + 2 3 + 4] cascade [even?/ boolean/])


; REDUCE-EACH is a variant which lets you intercept the values, and thus
; intervene in questions of how unstable antiforms will be handled.
[
    ([<null> 3] = collect [reduce-each 'x [null 1 + 2] [keep (x else '<null>)]])

    ([3 7] = collect [reduce-each 'x [1 + 2 3 + 4] [keep x]])

    ([1 + 2] = collect [reduce-each 'x @[1 + 2] [keep x]])
]

; REDUCE-EACH can do ^META processing, this is the basis of ATTEMPT
; VOID!s are ignored, as in REDUCE
; if REDUCE gets a "can see light voids" mode, REDUCE-EACH should too
[
    (3 = reduce-each ^x [1 + 2] [x])  ; no effect if not unstable

    ([3 7] = collect [reduce-each ^x [1 + 2 comment "hi" 3 + 4] [keep x]])

    (
        e: reduce-each ^x [fail "foo"] [disarm ^x]
        e.message = "foo"
    )
]

; SPREAD is honored by REDUCE only if 1 item or a predicate used
[
    ~???~ !! (reduce [1 if ok [spread []] 2])
    ([1 2] = reduce:predicate [1 if ok [spread []] 2] identity/)
    ([1 2 3] = reduce [1 if ok [spread [2]] 3])
    ~???~ !! (reduce [1 if ok [spread [2 3]] 4])
    ([1 2 3 4] = reduce:predicate [1 if ok [spread [2 3]] 4] identity/)
]

; VETO can be used in REDUCE
[
    (null = reduce [1 + 2 veto])
    (null = reduce [1 + 2 lib.veto])

    ; predicates are not offered VETO
    ;
    (null = reduce:predicate [veto veto] lift/)
]
