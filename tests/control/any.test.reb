; functions/control/any.r
; zero values
(null? any [])
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
    a-value: blank!
    same? a-value any [a-value]
)
(1/Jan/0000 = any [1/Jan/0000])
(0.0 == any [0.0])
(1.0 == any [1.0])
(
    a-value: me@here.com
    same? a-value any [a-value]
)
(error? any [trap [1 / 0]])
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
    :a-value == any [:a-value]
)
(NUL == any [NUL])
(
    a-value: make image! 0x0
    same? a-value any [a-value]
)
(0 == any [0])
(1 == any [1])
(#a == any [#a])
(
    a-value: first ['a/b]
    :a-value == any [:a-value]
)
(
    a-value: first ['a]
    :a-value == any [:a-value]
)
(true = any [true])
(null? any [false])
($1 == any [$1])
(same? :append any [:append])
(null? any [_])
(
    a-value: make object! []
    same? :a-value any [:a-value]
)
(
    a-value: first [()]
    same? :a-value any [:a-value]
)
(same? get '+ any [get '+])
(0x0 == any [0x0])
(
    a-value: 'a/b
    :a-value == any [:a-value]
)
(
    a-value: make port! http://
    port? any [:a-value]
)
(/a == any [/a])
; routine test?
(
    a-value: first [a/b:]
    :a-value == any [:a-value]
)
(
    a-value: first [a:]
    :a-value == any [:a-value]
)
(
    a-value: ""
    same? :a-value any [:a-value]
)
(
    a-value: make tag! ""
    same? :a-value any [:a-value]
)
(0:00 == any [0:00])
(0.0.0 == any [0.0.0])
(null? any [null])
('a == any ['a])
; two values
(:abs = any [false :abs])
(
    a-value: #{}
    same? a-value any [false a-value]
)
(
    a-value: charset ""
    same? a-value any [false a-value]
)
(
    a-value: []
    same? a-value any [false a-value]
)
(
    a-value: blank!
    same? a-value any [false a-value]
)
(1/Jan/0000 = any [false 1/Jan/0000])
(0.0 == any [false 0.0])
(1.0 == any [false 1.0])
(
    a-value: me@here.com
    same? a-value any [false a-value]
)
(error? any [false trap [1 / 0]])
(
    a-value: %""
    same? a-value any [false a-value]
)
(
    a-value: does []
    same? :a-value any [false :a-value]
)
(
    a-value: first [:a]
    :a-value == any [false :a-value]
)
(NUL == any [false NUL])
(
    a-value: make image! 0x0
    same? a-value any [false a-value]
)
(0 == any [false 0])
(1 == any [false 1])
(#a == any [false #a])
(
    a-value: first ['a/b]
    :a-value == any [false :a-value]
)
(
    a-value: first ['a]
    :a-value == any [false :a-value]
)
(true = any [false true])
(null? any [false false])
($1 == any [false $1])
(same? :append any [false :append])
(null? any [false _])
(
    a-value: make object! []
    same? :a-value any [false :a-value]
)
(
    a-value: first [()]
    same? :a-value any [false :a-value]
)
(same? get '+ any [false get '+])
(0x0 == any [false 0x0])
(
    a-value: 'a/b
    :a-value == any [false :a-value]
)
(
    a-value: make port! http://
    port? any [false :a-value]
)
(/a == any [false /a])
(
    a-value: first [a/b:]
    :a-value == any [false :a-value]
)
(
    a-value: first [a:]
    :a-value == any [false :a-value]
)
(
    a-value: ""
    same? :a-value any [false :a-value]
)
(
    a-value: make tag! ""
    same? :a-value any [false :a-value]
)
(0:00 == any [false 0:00])
(0.0.0 == any [false 0.0.0])
(null? any [false null])
('a == any [false 'a])
(:abs = any [:abs false])
(
    a-value: #{}
    same? a-value any [a-value false]
)
(
    a-value: charset ""
    same? a-value any [a-value false]
)
(
    a-value: []
    same? a-value any [a-value false]
)
(
    a-value: blank!
    same? a-value any [a-value false]
)
(1/Jan/0000 = any [1/Jan/0000 false])
(0.0 == any [0.0 false])
(1.0 == any [1.0 false])
(
    a-value: me@here.com
    same? a-value any [a-value false]
)
(error? any [trap [1 / 0] false])
(
    a-value: %""
    same? a-value any [a-value false]
)
(
    a-value: does []
    same? :a-value any [:a-value false]
)
(
    a-value: first [:a]
    :a-value == any [:a-value false]
)
(NUL == any [NUL false])
(
    a-value: make image! 0x0
    same? a-value any [a-value false]
)
(0 == any [0 false])
(1 == any [1 false])
(#a == any [#a false])
(
    a-value: first ['a/b]
    :a-value == any [:a-value false]
)
(
    a-value: first ['a]
    :a-value == any [:a-value false]
)
(true = any [true false])
($1 == any [$1 false])
(same? :append any [:append false])
(null? any [_ false])
(
    a-value: make object! []
    same? :a-value any [:a-value false]
)
(
    a-value: first [()]
    same? :a-value any [:a-value false]
)
(same? get '+ any [get '+ false])
(0x0 == any [0x0 false])
(
    a-value: 'a/b
    :a-value == any [:a-value false]
)
(
    a-value: make port! http://
    port? any [:a-value false]
)
(/a == any [/a false])
(
    a-value: first [a/b:]
    :a-value == any [:a-value false]
)
(
    a-value: first [a:]
    :a-value == any [:a-value false]
)
(
    a-value: ""
    same? :a-value any [:a-value false]
)
(
    a-value: make tag! ""
    same? :a-value any [:a-value false]
)
(0:00 == any [0:00 false])
(0.0.0 == any [0.0.0 false])
(null? any [null false])
('a == any ['a false])
; evaluation stops after encountering something else than FALSE or NONE
(
    success: true
    any [true success: false]
    success
)
(
    success: true
    any [1 success: false]
    success
)
; evaluation continues otherwise
(
    success: false
    any [false success: true]
    success
)
(
    success: false
    any [blank success: true]
    success
)
; RETURN stops evaluation
(
    f1: func [] [any [return 1 2] 2]
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
(any [false any [true]])
(null? any [false any [false]])
; infinite recursion
(
    blk: [any blk]
    error? trap blk
)

; PREDICATES

(10 = any .even? [1 + 2 3 + 4 5 + 5 6 + 7])
(10 = any .not.odd? [1 + 2 3 + 4 5 + 5 6 + 7])
(10 = any .even? [1 + 2, comment "Hello", 3 + 4, 5 + 5, 6 + 7])
(10 = any .not.odd? [1 + 2, 3 + 4 comment "No Comma" 5 + 5, 6 + 7])

(_ = any .not [1 _ 2])
('~null~ = ^ any .not [1 null 2])
("this is why" = any .not [1 null 2] then ["this is why"])

(10 = any [(10 elide "stale")])
