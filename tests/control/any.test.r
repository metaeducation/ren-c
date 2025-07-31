; %any.test.r

; Most languages consider variadic OR operations in the spirit of ANY to
; be truthy if there are no items.  We use void if things truly vanish.
[
    (void? any [])

    ~bad-void~ !! (if any [] [<safety>])

    (
        x: <overwritten>
        all [
            void? x: any []
            void? x
        ]
    )
    (
        x: <overwritten>
        all [
            void? x: any [comment "hi"]
            void? x
        ]
    )
    (<didn't> = if else? any [] [<didn't>])
    (<else> = any [] else [<else>])
    (void? (1 + 2 any []))
    (null = (1 + 2 any [1 > 2, 3 > 4]))
]


; one value
(:abs = any [:abs])
(
    a-value: #{}
    same? a-value any [a-value]
)
(
    a-value: charset ""
    same? a-value any [a-value]
)
(
    a-value: []
    same? a-value any [a-value]
)
(
    a-value: integer!
    same? a-value any [a-value]
)
(1/Jan/0000 = any [1/Jan/0000])
(0.0 = any [0.0])
(1.0 = any [1.0])
(
    a-value: me@here.com
    same? a-value any [a-value]
)
(warning? any [rescue [1 / 0]])
(
    a-value: %""
    same? a-value any [a-value]
)
(
    a-value: does []
    same? :a-value any [:a-value]
)
(
    a-value: first [:a]
    a-value = any [a-value]
)
(NUL = any [NUL])

(0 = any [0])
(1 = any [1])
(#a = any [#a])
(
    a-value: first ['a/b]
    :a-value = any [:a-value]
)
(
    a-value: first ['a]
    :a-value = any [:a-value]
)
(okay = any [okay])
(null? any [null])

(same? ^append any [^append])

(_ = any [_])
(null? any [~null~])

(
    a-value: make object! []
    same? :a-value any [:a-value]
)
(
    a-value: first [()]
    same? :a-value any [:a-value]
)
(same? +/ any [+/])
(0x0 = any [0x0])
(
    a-value: 'a/b
    :a-value = any [:a-value]
)
(
    a-value: make port! http://
    port? any [:a-value]
)
('/a = any ['/a])
; routine test?
(
    a-value: first [a.b:]
    :a-value = any [:a-value]
)
(
    a-value: first [a:]
    :a-value = any [:a-value]
)
(
    a-value: ""
    same? :a-value any [:a-value]
)
(
    a-value: to tag! ""
    same? a-value any [a-value]
)
(0:00 = any [0:00])
(0.0.0 = any [0.0.0])
(null? any [null])
('a = any ['a])
; two values
((unrun :abs) = any [null unrun :abs])
(
    a-value: #{}
    same? a-value any [null a-value]
)
(
    a-value: charset ""
    same? a-value any [null a-value]
)
(
    a-value: []
    same? a-value any [null a-value]
)
(
    a-value: integer!
    same? a-value any [null a-value]
)
(1/Jan/0000 = any [null 1/Jan/0000])
(0.0 = any [null 0.0])
(1.0 = any [null 1.0])
(
    a-value: me@here.com
    same? a-value any [null a-value]
)
(warning? any [null rescue [1 / 0]])
(
    a-value: %""
    same? a-value any [null a-value]
)
(
    a-value: does []
    same? unrun :a-value any [null unrun :a-value]
)
(
    a-value: first [:a]
    a-value = any [null a-value]
)
(NUL = any [null NUL])

(0 = any [null 0])
(1 = any [null 1])
(#a = any [null #a])
(
    a-value: first ['a/b]
    :a-value = any [null :a-value]
)
(
    a-value: first ['a]
    :a-value = any [null :a-value]
)
(okay = any [null okay])
(null? any [null null])
(same? unrun :append any [null unrun :append])

(null? any [null ~null~])
(_ = any [null _])

(
    a-value: make object! []
    same? :a-value any [null :a-value]
)
(
    a-value: first [()]
    same? :a-value any [null :a-value]
)
(same? +/ any [null +/])
(0x0 = any [null 0x0])
(
    a-value: 'a/b
    :a-value = any [null :a-value]
)
(
    a-value: make port! http://
    port? any [null :a-value]
)
('/a = any [null '/a])
(
    a-value: first [a.b:]
    :a-value = any [null :a-value]
)
(
    a-value: first [a:]
    :a-value = any [null :a-value]
)
(
    a-value: ""
    same? :a-value any [null :a-value]
)
(
    a-value: to tag! ""
    same? a-value any [null a-value]
)
(0:00 = any [null 0:00])
(0.0.0 = any [null 0.0.0])
(null? any [null null])
('a = any [null 'a])
(:abs = any [:abs null])
(
    a-value: #{}
    same? a-value any [a-value null]
)
(
    a-value: charset ""
    same? a-value any [a-value null]
)
(
    a-value: []
    same? a-value any [a-value null]
)
(
    a-value: integer!
    same? a-value any [a-value null]
)
(1/Jan/0000 = any [1/Jan/0000 null])
(0.0 = any [0.0 null])
(1.0 = any [1.0 null])
(
    a-value: me@here.com
    same? a-value any [a-value null]
)
(warning? any [rescue [1 / 0] null])
(
    a-value: %""
    same? a-value any [a-value null]
)
(
    a-value: does []
    same? unrun :a-value any [unrun :a-value null]
)
(
    a-value: first [:a]
    :a-value = any [:a-value null]
)
(NUL = any [NUL null])

(0 = any [0 null])
(1 = any [1 null])
(#a = any [#a null])
(
    a-value: first ['a/b]
    :a-value = any [:a-value null]
)
(
    a-value: first ['a]
    :a-value = any [:a-value null]
)
(okay = any [okay null])

(same? unrun :append any [unrun :append null])
(_ = any [_ null])
(null? any [~null~ null])

(
    a-value: make object! []
    same? :a-value any [:a-value null]
)
(
    a-value: first [()]
    same? :a-value any [:a-value null]
)
(same? +/ any [+/ null])
(0x0 = any [0x0 null])
(
    a-value: 'a/b
    :a-value = any [:a-value null]
)
(
    a-value: make port! http://
    port? any [:a-value null]
)
('/a = any ['/a null])
(
    a-value: first [a.b:]
    :a-value = any [:a-value null]
)
(
    a-value: first [a:]
    :a-value = any [:a-value null]
)
(
    a-value: ""
    same? :a-value any [:a-value null]
)
(
    a-value: to tag! ""
    same? a-value any [a-value null]
)
(0:00 = any [0:00 null])
(0.0.0 = any [0.0.0 null])
(null? any [null null])
('a = any ['a null])
; evaluation stops after encountering something else than null or NULL
(
    success: okay
    any [okay success: null]
    success
)
(
    success: okay
    any [1 success: null]
    success
)
; evaluation continues otherwise
(
    success: null
    any [null success: okay]
    success
)
(
    success: null
    space = any [space success: okay]
)
; RETURN stops evaluation
(
    f1: func [return: [integer!]] [any [return 1 2] 2]
    1 = f1
)
; THROW stops evaluation
(
    1 = catch [
        any [
            throw 1
            2
        ]
    ]
)
; BREAK stops evaluation
(
    null? repeat 1 [
        any [
            break
            2
        ]
    ]
)
; recursivity
(any [null any [okay]])
(null? any [null any [null]])

; infinite recursion
(
    <deep-enough> = catch wrap [
        n: 0
        blk: [all [either 5000 = n: n + 1 [throw <deep-enough>] [okay]]]
        append blk.2 as group! blk
        eval blk
    ]
)

; PREDICATES

(10 = any:predicate [1 + 2 3 + 4 5 + 5 6 + 7] even?/)
(10 = any:predicate [1 + 2 3 + 4 5 + 5 6 + 7] cascade [odd?/ not/])
(10 = any:predicate [1 + 2, comment "Hello", 3 + 4, 5 + 5, 6 + 7] even?/)
(10 = apply any/ [
    [1 + 2, 3 + 4 comment "No Comma" 5 + 5, 6 + 7]
    :predicate cascade [odd?/ not/]
])

('~[~null~]~ = ^ any:predicate [1 null 2] :not)
('~[~null~]~ = ^ any:predicate [1 null 2] :not)
("this is why" = (any:predicate [1 null 2] :not then ["this is why"]))

(10 = any [(10 elide "stale")])
(1 = any [1 elide <vaporize>])

[
    (
        two: ~
        3 = any [
            all [eval [comment "hi"], elide two: 2]
            1 + two
        ]
    )
]

; When used with @ blocks, ANY will treat the block as already reduced
; With all values becoming truthy, this is only really useful with a predicate.
[
    (void? any @[])
    (1 = any @[1 + 2])
    ('~null~ = any @[~null~ _])
    ('null = any pin reduce ['null space])
    ('null = any @[null])  ; just the word, and words are truthy
]

(append/ = any [1 > 2, append/, 3 > 4])
