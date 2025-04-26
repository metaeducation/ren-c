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
(#"^@" == any [#"^@"])
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
(okay = any [okay])
(null? any [null])
($1 == any [$1])
(same? :append any [:append])
(blank? any [_])
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
(:abs = any [null :abs])
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
    a-value: blank!
    same? a-value any [null a-value]
)
(1/Jan/0000 = any [null 1/Jan/0000])
(0.0 == any [null 0.0])
(1.0 == any [null 1.0])
(
    a-value: me@here.com
    same? a-value any [null a-value]
)
(
    a-value: %""
    same? a-value any [null a-value]
)
(
    a-value: does []
    same? :a-value any [null :a-value]
)
(
    a-value: first [:a]
    :a-value == any [null :a-value]
)
(#"^@" == any [null #"^@"])
(0 == any [null 0])
(1 == any [null 1])
(#a == any [null #a])
(
    a-value: first ['a/b]
    :a-value == any [null :a-value]
)
(
    a-value: first ['a]
    :a-value == any [null :a-value]
)
(okay = any [null okay])
(null? any [null null])
($1 == any [null $1])
(same? :append any [null :append])
(blank? any [null _])
(
    a-value: make object! []
    same? :a-value any [null :a-value]
)
(
    a-value: first [()]
    same? :a-value any [null :a-value]
)
(same? get '+ any [null get '+])
(0x0 == any [null 0x0])
(
    a-value: 'a/b
    :a-value == any [null :a-value]
)
(
    a-value: make port! http://
    port? any [null :a-value]
)
(/a == any [null /a])
(
    a-value: first [a/b:]
    :a-value == any [null :a-value]
)
(
    a-value: first [a:]
    :a-value == any [null :a-value]
)
(
    a-value: ""
    same? :a-value any [null :a-value]
)
(
    a-value: make tag! ""
    same? :a-value any [null :a-value]
)
(0:00 == any [null 0:00])
(0.0.0 == any [null 0.0.0])
(null? any [null null])
('a == any [null 'a])
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
    a-value: blank!
    same? a-value any [a-value null]
)
(1/Jan/0000 = any [1/Jan/0000 null])
(0.0 == any [0.0 null])
(1.0 == any [1.0 null])
(
    a-value: me@here.com
    same? a-value any [a-value null]
)
(
    a-value: %""
    same? a-value any [a-value null]
)
(
    a-value: does []
    same? :a-value any [:a-value null]
)
(
    a-value: first [:a]
    :a-value == any [:a-value null]
)
(#"^@" == any [#"^@" null])
(0 == any [0 null])
(1 == any [1 null])
(#a == any [#a null])
(
    a-value: first ['a/b]
    :a-value == any [:a-value null]
)
(
    a-value: first ['a]
    :a-value == any [:a-value null]
)
(okay = any [okay null])
($1 == any [$1 null])
(same? :append any [:append null])
(null? any [null null])
(
    a-value: make object! []
    same? :a-value any [:a-value null]
)
(
    a-value: first [()]
    same? :a-value any [:a-value null]
)
(same? get '+ any [get '+ null])
(0x0 == any [0x0 null])
(
    a-value: 'a/b
    :a-value == any [:a-value null]
)
(
    a-value: make port! http://
    port? any [:a-value null]
)
(/a == any [/a null])
(
    a-value: first [a/b:]
    :a-value == any [:a-value null]
)
(
    a-value: first [a:]
    :a-value == any [:a-value null]
)
(
    a-value: ""
    same? :a-value any [:a-value null]
)
(
    a-value: make tag! ""
    same? :a-value any [:a-value null]
)
(0:00 == any [0:00 null])
(0.0.0 == any [0.0.0 null])
(null? any [null null])
('a == any ['a null])
; evaluation stops after encountering something else than FALSE or NONE
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
    any [null success: okay]
    success
)
; RETURN stops evaluation
(
    f1: func [] [return any [return 1 2] 2]
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
    blk: [any blk]
    error? sys/util/rescue blk
)
