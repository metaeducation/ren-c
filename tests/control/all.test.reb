; functions/control/all.r

; Most languages consider variadic AND operations in the spirit of ALL to
; be truthy if there are no items.  While it may have a minor advantage in
; some cases, many more benefits arise from being able to let it count as
; a kind of "non-vote", as a void.
[
    (void? all [])
    (void' = ^ all [])

    ~bad-void~ !! (if all [] [<safety>])

    (
        x: <overwritten>
        all [
            void? x: all []
            voided? $x
            void? :x
        ]
    )
    (
        x: <overwritten>
        all [
            void? x: all [void void]
            voided? $x
            void? :x
        ]
    )
    (void? if did all [] [<did>])
    (void? all [] then [<then>])
    (<else> = all [] else [<else>])

    (void? (1 + 2 all []))
    (null = (1 + 2 all [1 < 2, 3 > 4]))
]

; voids opt-out of voting.
(
    check1: 'on, check2: 'off
    all [if on? check1 [1 = 1], if on? check2 [2 = 1]]
)

; one value
(:abs = all [:abs])
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
    a-value: blank!
    same? a-value all [a-value]
)
(1/Jan/0000 = all [1/Jan/0000])
(0.0 == all [0.0])
(1.0 == all [1.0])
(
    a-value: me@here.com
    same? a-value all [a-value]
)
(error? all [trap [1 / 0]])
(
    a-value: %""
    same? a-value all [a-value]
)
(
    a-value: does []
    same? :a-value all [:a-value]
)
(
    a-value: first [:a]
    :a-value == all [:a-value]
)
(NUL == all [NUL])

(0 == all [0])
(1 == all [1])
(#a == all [#a])
(
    a-value: first ['a/b]
    :a-value == all [:a-value]
)
(
    a-value: first ['a]
    :a-value == all [:a-value]
)
(okay = all [okay])
(null? all [null])
($1 == all [$1])
(same? :append all [:append])

(null? all [~null~])
(_ = all [_])

(
    a-value: make object! []
    same? :a-value all [:a-value]
)
(
    a-value: first [()]
    same? :a-value all [:a-value]
)
(same? get $+ all [get $+])
(0x0 == all [0x0])
(
    a-value: 'a/b
    :a-value == all [:a-value]
)
(
    a-value: make port! http://
    port? all [:a-value]
)
(/a == all [/a])
(
    a-value: first [a.b:]
    :a-value == all [:a-value]
)
(
    a-value: first [a:]
    :a-value == all [:a-value]
)
(
    a-value: ""
    same? :a-value all [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value all [:a-value]
)
(0:00 == all [0:00])
(0.0.0 == all [0.0.0])
(null? all [null])
('a == all ['a])
; two values
(:abs = all [okay :abs])
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
    a-value: blank!
    same? a-value all [okay a-value]
)
(1/Jan/0000 = all [okay 1/Jan/0000])
(0.0 == all [okay 0.0])
(1.0 == all [okay 1.0])
(
    a-value: me@here.com
    same? a-value all [okay a-value]
)
(error? all [okay trap [1 / 0]])
(
    a-value: %""
    same? a-value all [okay a-value]
)
(
    a-value: does []
    same? :a-value all [okay :a-value]
)
(
    a-value: first [:a]
    same? :a-value all [okay :a-value]
)
(NUL == all [okay NUL])

(0 == all [okay 0])
(1 == all [okay 1])
(#a == all [okay #a])
(
    a-value: first ['a/b]
    :a-value == all [okay :a-value]
)
(
    a-value: first ['a]
    :a-value == all [okay :a-value]
)
($1 == all [okay $1])
(same? ^append all [okay ^append])

(null? all [okay ~null~])
(_ = all [okay _])

(
    a-value: make object! []
    same? :a-value all [okay :a-value]
)
(
    a-value: first [()]
    same? :a-value all [okay :a-value]
)
(same? get $+ all [okay get $+])
(0x0 == all [okay 0x0])
(
    a-value: 'a/b
    :a-value == all [okay :a-value]
)
(
    a-value: make port! http://
    port? all [okay :a-value]
)
(/a == all [okay /a])
(
    a-value: first [a.b:]
    :a-value == all [okay :a-value]
)
(
    a-value: first [a:]
    :a-value == all [okay :a-value]
)
(
    a-value: ""
    same? :a-value all [okay :a-value]
)
(
    a-value: make tag! ""
    same? :a-value all [okay :a-value]
)
(0:00 == all [okay 0:00])
(0.0.0 == all [okay 0.0.0])
(null? all [1020 null])
('a == all [okay 'a])
(okay = all [:abs okay])
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
    a-value: blank!
    okay = all [a-value okay]
)
(okay = all [1/Jan/0000 okay])
(okay = all [0.0 okay])
(okay = all [1.0 okay])
(
    a-value: me@here.com
    okay = all [a-value okay]
)
(okay = all [trap [1 / 0] okay])
(
    a-value: %""
    okay = all [a-value okay]
)
(
    a-value: does []
    okay = all [:a-value okay]
)
(
    a-value: first [:a]
    okay = all [:a-value okay]
)
(okay = all [NUL okay])

(okay = all [0 okay])
(okay = all [1 okay])
(okay = all [#a okay])
(
    a-value: first ['a/b]
    okay = all [:a-value okay]
)
(
    a-value: first ['a]
    okay = all [:a-value okay]
)
(okay = all [okay okay])
(null? all [null okay])
(null? all [okay null])
(okay = all [$1 okay])
(okay = all [:append okay])
(okay = all [_ okay])
(
    a-value: make object! []
    okay = all [:a-value okay]
)
(
    a-value: first [()]
    okay = all [:a-value okay]
)
(okay = all [get $+ okay])
(okay = all [0x0 okay])
(
    a-value: 'a/b
    okay = all [:a-value okay]
)
(
    a-value: make port! http://
    okay = all [:a-value okay]
)
(okay = all [/a okay])
(
    a-value: first [a.b:]
    okay = all [:a-value okay]
)
(
    a-value: first [a:]
    okay = all [:a-value okay]
)
(
    a-value: ""
    okay = all [:a-value okay]
)
(
    a-value: make tag! ""
    okay = all [:a-value okay]
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
    all [blank success: null]
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
    counter: 0
    blk: [counter: me + 1, if counter = 5000 [throw <deep-enough>], all blk]
    <deep-enough> = catch [eval blk]
)

; PREDICATES

(15 = all/predicate [1 + 2 3 + 4 5 + 6 7 + 8] get $odd?)
(15 = all/predicate [1 + 2 3 + 4 5 + 6 7 + 8] cascade [get $even?, get $not])
(15 = all/predicate [1 + 2, 3 + 4, comment "Hi" 5 + 6, 7 + 8] get $odd?)
(15 = all/predicate [1 + 2, 3 + 4 5 + 6, 7 + 8,] cascade [get $even?, get $not])

; ALL returns void when contents completely erase
[
    ("A" = all ["A", all [comment "hi", void, eval []]])
]

; When used with @ blocks, ALL will treat the block as already reduced
; With all reified values being truthy, this is only useful with a predicate
[
    (void? all @[])

    (2 = all @[1 + 2])
    ('null = all @[okay okay null null])
    ('okay = all @[null okay])  ; just the word, and words are truthy
]

(^(spread [d e]) = ^(all [1 < 2, 3 < 4, spread [d e]]))
