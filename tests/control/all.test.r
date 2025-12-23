; functions/control/all.r

; Most languages consider variadic AND operations in the spirit of ALL to
; be truthy if there are no items.  While it may have a minor advantage in
; some cases, many more benefits arise from being able to let it count as
; a kind of "non-vote", as a void.
[
    (ghost? all [])
    ((lift ^ghost) = lift all [])

    ~bad-void~ !! (if all [] [<safety>])

    (
        x: <overwritten>
        all [
            ghost? x: all []
            ghost? x
        ]
    )
    (
        x: <overwritten>
        all [
            ghost? x: all [^ghost ^ghost ()]
            ghost? x
        ]
    )
    (ghost? all [])
    (ghost? all [] then [<then>])
    (<else> = all [] else [<else>])

    (ghost? (1 + 2 all []))
    (null = (1 + 2 all [1 < 2, 3 > 4]))
]

; voids opt-out of voting.
(
    check1: 'on, check2: 'off
    all [if on? check1 [1 = 1], if on? check2 [2 = 1]]
)

; one value
(abs/ = all [abs/])
(
    a-value: #{}
    same? a-value all [a-value]
)
(
    a-value: charset ""
    same? a-value all [a-value]
)
(
    a-value: []
    same? a-value all [a-value]
)
(
    a-value: integer!
    same? a-value all [a-value]
)
(1/Jan/0000 = all [1/Jan/0000])
(0.0 = all [0.0])
(1.0 = all [1.0])
(
    a-value: me@here.com
    same? a-value all [a-value]
)
(warning? all [rescue [1 / 0]])
(
    a-value: %""
    same? a-value all [a-value]
)
(
    a-value: does []
    same? a-value all [a-value]
)
(
    a-value: first [:a]
    a-value = all [a-value]
)
(NUL = all [NUL])

(0 = all [0])
(1 = all [1])
(#a = all [#a])
(
    a-value: first ['a/b]
    a-value = all [a-value]
)
(
    a-value: first ['a]
    a-value = all [a-value]
)
(okay = all [okay])
(null? all [null])
(same? append/ all [append/])

(null? all [~null~])
(_ = all [_])

(
    a-value: make object! []
    same? a-value all [a-value]
)
(
    a-value: first [()]
    same? a-value all [a-value]
)
(same? +/ all [+/])
(0x0 = all [0x0])
(
    a-value: 'a/b
    a-value = all [a-value]
)
(
    a-value: make port! http://
    port? all [a-value]
)
('/a = all ['/a])
(
    a-value: first [a.b:]
    a-value = all [a-value]
)
(
    a-value: first [a:]
    a-value = all [a-value]
)
(
    a-value: ""
    same? a-value all [a-value]
)
(
    a-value: to tag! ""
    same? a-value all [a-value]
)
(0:00 = all [0:00])
(0.0.0 = all [0.0.0])
(null? all [null])
('a = all ['a])
; two values
(abs/ = all [okay abs/])
(
    a-value: #{}
    same? a-value all [okay a-value]
)
(
    a-value: charset ""
    same? a-value all [okay a-value]
)
(
    a-value: []
    same? a-value all [okay a-value]
)
(
    a-value: integer!
    same? a-value all [okay a-value]
)
(1/Jan/0000 = all [okay 1/Jan/0000])
(0.0 = all [okay 0.0])
(1.0 = all [okay 1.0])
(
    a-value: me@here.com
    same? a-value all [okay a-value]
)
(warning? all [okay rescue [1 / 0]])
(
    a-value: %""
    same? a-value all [okay a-value]
)
(
    a-value: does []
    same? a-value/ all [okay a-value/]
)
(
    a-value: first [:a]
    same? a-value all [okay a-value]
)
(NUL = all [okay NUL])

(0 = all [okay 0])
(1 = all [okay 1])
(#a = all [okay #a])
(
    a-value: first ['a/b]
    a-value = all [okay a-value]
)
(
    a-value: first ['a]
    a-value = all [okay a-value]
)
(same? append/ all [okay append/])

(null? all [okay ~null~])
(_ = all [okay _])

(
    a-value: make object! []
    same? a-value all [okay a-value]
)
(
    a-value: first [()]
    same? a-value all [okay a-value]
)
(same? +/ all [okay +/])
(0x0 = all [okay 0x0])
(
    a-value: 'a/b
    a-value = all [okay a-value]
)
(
    a-value: make port! http://
    port? all [okay a-value]
)
('/a = all [okay '/a])
(
    a-value: first [a.b:]
    a-value = all [okay a-value]
)
(
    a-value: first [a:]
    a-value = all [okay a-value]
)
(
    a-value: ""
    same? a-value all [okay a-value]
)
(
    a-value: to tag! ""
    same? a-value all [okay a-value]
)
(0:00 = all [okay 0:00])
(0.0.0 = all [okay 0.0.0])
(null? all [1020 null])
('a = all [okay 'a])
(okay = all [abs/ okay])
(
    a-value: #{}
    okay = all [a-value okay]
)
(
    a-value: charset ""
    okay = all [a-value okay]
)
(
    a-value: []
    okay = all [a-value okay]
)
(
    a-value: integer!
    okay = all [a-value okay]
)
(okay = all [1/Jan/0000 okay])
(okay = all [0.0 okay])
(okay = all [1.0 okay])
(
    a-value: me@here.com
    okay = all [a-value okay]
)
(okay = all [rescue [1 / 0] okay])
(
    a-value: %""
    okay = all [a-value okay]
)
(
    a-value: does []
    okay = all [a-value/ okay]
)
(
    a-value: first [:a]
    okay = all [a-value okay]
)
(okay = all [NUL okay])

(okay = all [0 okay])
(okay = all [1 okay])
(okay = all [#a okay])
(
    a-value: first ['a/b]
    okay = all [a-value okay]
)
(
    a-value: first ['a]
    okay = all [a-value okay]
)
(okay = all [okay okay])
(null? all [null okay])
(null? all [okay null])

(okay = all [append/ okay])
(okay = all [_ okay])
(
    a-value: make object! []
    okay = all [a-value okay]
)
(
    a-value: first [()]
    okay = all [a-value okay]
)
(okay = all [+/ okay])
(okay = all [0x0 okay])
(
    a-value: 'a/b
    okay = all [a-value okay]
)
(
    a-value: make port! http://
    okay = all [a-value okay]
)
(okay = all ['/a okay])
(
    a-value: first [a.b:]
    okay = all [a-value okay]
)
(
    a-value: first [a:]
    okay = all [a-value okay]
)
(
    a-value: ""
    okay = all [a-value okay]
)
(
    a-value: to tag! ""
    okay = all [a-value okay]
)
(okay = all [0:00 okay])
(okay = all [0.0.0 okay])
(okay = all ['a okay])
; evaluation stops after encountering null or NULL
(
    success: okay
    all [null success: null]
    success
)
(
    success: okay
    all [space success: null]
    success = null
)
; evaluation continues otherwise
(
    success: null
    all [okay success: okay]
    success
)
(
    success: null
    all [1 success: okay]
    success
)
; RETURN stops evaluation
(
    f1: func [return: [integer!]] [all [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        all [
            throw 1
            2
        ]
    ]
)
; BREAK stops evaluation
(
    null? repeat 1 [
        all [
            break
            2
        ]
    ]
)
; recursivity
(all [okay all [okay]])
(not all [okay all [null]])

; infinite recursion
(
    <deep-enough> = catch wrap [
        counter: 0
        blk: [counter: me + 1, if counter = 5000 [throw <deep-enough>], all blk]
        eval blk
    ]
)

; PREDICATES

(15 = all:predicate [1 + 2 3 + 4 5 + 6 7 + 8] odd?/)
(15 = all:predicate [1 + 2 3 + 4 5 + 6 7 + 8] cascade [even?/ not/])
(15 = all:predicate [1 + 2, 3 + 4, comment "Hi" 5 + 6, 7 + 8] odd?/)
(15 = all:predicate [1 + 2, 3 + 4 5 + 6, 7 + 8,] cascade [even?/ not/])

; ALL returns void when contents completely erase
[
    ("A" = all ["A", all [comment "hi", ^ghost, eval []]])
]

; When used with @ blocks, ALL will treat the block as already reduced
; With all reified values being truthy, this is only useful with a predicate
[
    (ghost? all @[])

    (2 = all @[1 + 2])
    ('null = all @[okay okay null null])
    ('okay = all @[null okay])  ; just the word, and words are truthy
]

((spread [d e]) = all [1 < 2, 3 < 4, spread [d e]])

(1 = all [1 elide <vaporize>])
