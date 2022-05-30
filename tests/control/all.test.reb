; functions/control/all.r

; Most languages consider variadic AND operations in the spirit of ALL to
; be truthy if there are no items.  While it may have a minor advantage in
; some cases, many more benefits arise from being able to let it count as
; a kind of "non-vote", as a ~void~ isotope.
[
    ('~void~ = ^ all [])
    (
        e: trap [if all [] [<safety>]]
        e.id = 'isotope-arg
    )
    (
        x: <overwritten>
        did all [
            '~void~ = ^ x: all []
            (unset? 'x)
        ]
    )
    ('~void~ = ^ if did all [] [<did>])
    ('~void~ = ^ all [] then [<then>])
    (<else> = all [] else [<else>])

    (3 = (1 + 2 maybe all []))
    (null = (1 + 2 maybe all [1 < 2, 3 > 4]))
]

; light isotopes can be MAYBE'd to opt-out of voting.
(
    check1: true, check2: false
    all [maybe if check1 [1 = 1], maybe if check2 [2 = 1]]
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
(
    a-value: make image! 0x0
    same? a-value all [a-value]
)
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
(true = all [true])
(null? all [false])
($1 == all [$1])
(same? :append all [:append])
(null? all [_])
(
    a-value: make object! []
    same? :a-value all [:a-value]
)
(
    a-value: first [()]
    same? :a-value all [:a-value]
)
(same? get '+ all [get '+])
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
    a-value: first [a/b:]
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
(:abs = all [true :abs])
(
    a-value: #{}
    same? a-value all [true a-value]
)
(
    a-value: charset ""
    same? a-value all [true a-value]
)
(
    a-value: []
    same? a-value all [true a-value]
)
(
    a-value: blank!
    same? a-value all [true a-value]
)
(1/Jan/0000 = all [true 1/Jan/0000])
(0.0 == all [true 0.0])
(1.0 == all [true 1.0])
(
    a-value: me@here.com
    same? a-value all [true a-value]
)
(error? all [true trap [1 / 0]])
(
    a-value: %""
    same? a-value all [true a-value]
)
(
    a-value: does []
    same? :a-value all [true :a-value]
)
(
    a-value: first [:a]
    same? :a-value all [true :a-value]
)
(NUL == all [true NUL])
(
    a-value: make image! 0x0
    same? a-value all [true a-value]
)
(0 == all [true 0])
(1 == all [true 1])
(#a == all [true #a])
(
    a-value: first ['a/b]
    :a-value == all [true :a-value]
)
(
    a-value: first ['a]
    :a-value == all [true :a-value]
)
($1 == all [true $1])
(same? :append all [true :append])
(null? all [true _])
(
    a-value: make object! []
    same? :a-value all [true :a-value]
)
(
    a-value: first [()]
    same? :a-value all [true :a-value]
)
(same? get '+ all [true get '+])
(0x0 == all [true 0x0])
(
    a-value: 'a/b
    :a-value == all [true :a-value]
)
(
    a-value: make port! http://
    port? all [true :a-value]
)
(/a == all [true /a])
(
    a-value: first [a/b:]
    :a-value == all [true :a-value]
)
(
    a-value: first [a:]
    :a-value == all [true :a-value]
)
(
    a-value: ""
    same? :a-value all [true :a-value]
)
(
    a-value: make tag! ""
    same? :a-value all [true :a-value]
)
(0:00 == all [true 0:00])
(0.0.0 == all [true 0.0.0])
(null? all [1020 null])
('a == all [true 'a])
(true = all [:abs true])
(
    a-value: #{}
    true = all [a-value true]
)
(
    a-value: charset ""
    true = all [a-value true]
)
(
    a-value: []
    true = all [a-value true]
)
(
    a-value: blank!
    true = all [a-value true]
)
(true = all [1/Jan/0000 true])
(true = all [0.0 true])
(true = all [1.0 true])
(
    a-value: me@here.com
    true = all [a-value true]
)
(true = all [trap [1 / 0] true])
(
    a-value: %""
    true = all [a-value true]
)
(
    a-value: does []
    true = all [:a-value true]
)
(
    a-value: first [:a]
    true = all [:a-value true]
)
(true = all [NUL true])
(
    a-value: make image! 0x0
    true = all [a-value true]
)
(true = all [0 true])
(true = all [1 true])
(true = all [#a true])
(
    a-value: first ['a/b]
    true = all [:a-value true]
)
(
    a-value: first ['a]
    true = all [:a-value true]
)
(true = all [true true])
(null? all [false true])
(null? all [true false])
(true = all [$1 true])
(true = all [:append true])
(null? all [_ true])
(
    a-value: make object! []
    true = all [:a-value true]
)
(
    a-value: first [()]
    true = all [:a-value true]
)
(true = all [get '+ true])
(true = all [0x0 true])
(
    a-value: 'a/b
    true = all [:a-value true]
)
(
    a-value: make port! http://
    true = all [:a-value true]
)
(true = all [/a true])
(
    a-value: first [a/b:]
    true = all [:a-value true]
)
(
    a-value: first [a:]
    true = all [:a-value true]
)
(
    a-value: ""
    true = all [:a-value true]
)
(
    a-value: make tag! ""
    true = all [:a-value true]
)
(true = all [0:00 true])
(true = all [0.0.0 true])
(true = all ['a true])
; evaluation stops after encountering FALSE or NONE
(
    success: true
    all [false success: false]
    success
)
(
    success: true
    all [blank success: false]
    success
)
; evaluation continues otherwise
(
    success: false
    all [true success: true]
    success
)
(
    success: false
    all [1 success: true]
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
(all [true all [true]])
(not all [true all [false]])
; infinite recursion
(
    blk: [all blk]
    error? trap blk
)

; PREDICATES

(15 = all/predicate [1 + 2 3 + 4 5 + 6 7 + 8] :odd?)
(15 = all/predicate [1 + 2 3 + 4 5 + 6 7 + 8] chain [:even? | :not])
(15 = all/predicate [1 + 2, 3 + 4, comment "Hi" 5 + 6, 7 + 8] :odd?)
(15 = all/predicate [1 + 2, 3 + 4 5 + 6, 7 + 8,] chain [:even? | :not])

('~blank~ = ^ all/predicate [false null _] :not)
('~null~ = ^ all/predicate [false _ null] :not)
("this is why" = (all/predicate [false _ null] :not then ["this is why"]))


; ALL returns an ~void~ isotope when contents completely erase
[
    ("A" = all ["A", maybe all [comment "hi", maybe eval []]])
]

; When used with @ blocks, ALL will treat the block as already reduced
[
    ('~void~ = ^ all @[])

    (2 = all @[1 + 2])
    (null = all inert reduce [true true #[false]])
    ('true = all @[false true])  ; just the word, and words are truthy
]

; Isotopes should raise errors vs. decay:
;
;     if not all [match logic! false] [  ; the match returns ~false~ isotope
;         print "We want to avoid this printing, motivate use of DID MATCH"
;     ]
;
('bad-isotope = pick trap [all [match logic! false]] 'id)
(#[true] = all [did match logic! false])
