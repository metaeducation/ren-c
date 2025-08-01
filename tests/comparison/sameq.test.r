; functions/comparison/sameq.r
; reflexivity test for action
(same? :abs :abs)
; reflexivity test for native
(same? :all :all)
; reflexivity test for infix
(same? :+ :+)
; reflexivity test for action
(
    a-value: func [] []
    same? a-value/ a-value/
)
; no structural equality for action
(not same? func [] [] func [] [])
; blob!
(not same? #{00} #{00})
; binary versus bitset
(not same? #{00} make bitset! #{00})
; symmetry
(equal? same? make bitset! #{00} #{00} same? #{00} make bitset! #{00})
(
    a-value: %""
    not same? a-value to text! a-value
)
; symmetry
(
    a-value: %""
    equal? same? a-value to text! a-value same? to text! a-value a-value
)
(not same? #{00} decode [BE +/-] #{00})
; symmetry
(equal? same? #{00} decode [BE +/-] #{00} same? decode [BE +/-] #{00} #{00})
(
    a-value: #a
    not same? a-value to text! a-value
)
; symmetry
(
    a-value: #a
    equal? same? a-value to text! a-value same? to text! a-value a-value
)
(not same? #{} space)
; symmetry
(equal? same? #{} space same? space #{})
(
    a-value: to tag! ""
    not same? a-value to text! a-value
)
; symmetry
(
    a-value: to tag! ""
    equal? same? a-value to text! a-value same? to text! a-value a-value
)
(
    a-value: 0.0.0.0
    not same? to blob! a-value a-value
)
; symmetry
(
    a-value: 0.0.0.0
    equal? same? to blob! a-value a-value same? a-value to blob! a-value
)
(not same? make bitset! #{00} make bitset! #{00})
(not same? make bitset! #{} make bitset! #{00})
; block!
(not same? [] [])
; reflexivity
(
    a-value: []
    same? a-value a-value
)
; reflexivity for past-tail blocks
(
    a-value: tail of [1]
    clear head of a-value
    same? a-value a-value
)
; reflexivity for cyclic blocks
(
    a-value: copy []
    insert a-value a-value
    same? a-value a-value
)
; comparison of cyclic blocks
(
    a-value: copy []
    insert a-value a-value
    b-value: copy []
    insert b-value b-value
    not same? a-value b-value
)

[
    #1068 #1066
    ; https://forum.rebol.info/t/1084
    (
        a-value: first [a/b]
        b-value: ~
        parse as block! :a-value [b-value: <here>, to <end>]
        equal? as block! :a-value :b-value
        not same? as block! :a-value :b-value  ; !!! as makes new array, review
    )
]

(not same? [] space)
; symmetry
(equal? same? [] space same? space [])
[#1068 #1066 (
    a-value: first [()]
    b-value: ~
    parse a-value [b-value: <here>, to <end>]
    same? a-value b-value
)]
; symmetry
(
    a-value: first [()]
    b-value: ~
    parse a-value [b-value: <here>, to <end>]
    equal? same? a-value b-value same? b-value a-value
)
(not same? any-number?/ integer!)
; symmetry
(equal? same? any-number?/ integer! same? integer! any-number?/)
; reflexivity
(same? -1 -1)
; reflexivity
(same? 0 0)
; reflexivity
(same? 1 1)
; reflexivity
(same? 0.0 0.0)
(not same? 0.0 -0.0)
; reflexivity
(same? 1.0 1.0)
; reflexivity
(same? -1.0 -1.0)
; reflexivity
<64bit>
(same? -9223372036854775808 -9223372036854775808)
; reflexivity
<64bit>
(same? -9223372036854775807 -9223372036854775807)
; reflexivity
<64bit>
(same? 9223372036854775807 9223372036854775807)
; -9223372036854775808 not same?
<64bit>
(not same? -9223372036854775808 -9223372036854775807)
<64bit>
(not same? -9223372036854775808 -1)
<64bit>
(not same? -9223372036854775808 0)
<64bit>
(not same? -9223372036854775808 1)
<64bit>
(not same? -9223372036854775808 9223372036854775806)
<64bit>
(not same? -9223372036854775808 9223372036854775807)
; -9223372036854775807 not same?
<64bit>
(not same? -9223372036854775807 -9223372036854775808)
<64bit>
(not same? -9223372036854775807 -1)
<64bit>
(not same? -9223372036854775807 0)
<64bit>
(not same? -9223372036854775807 1)
<64bit>
(not same? -9223372036854775807 9223372036854775806)
<64bit>
(not same? -9223372036854775807 9223372036854775807)
; -1 not same?
<64bit>
(not same? -1 -9223372036854775808)
<64bit>
(not same? -1 -9223372036854775807)
(not same? -1 0)
(not same? -1 1)
<64bit>
(not same? -1 9223372036854775806)
<64bit>
(not same? -1 9223372036854775807)
; 0 not same?
<64bit>
(not same? 0 -9223372036854775808)
<64bit>
(not same? 0 -9223372036854775807)
(not same? 0 -1)
(not same? 0 1)
<64bit>
(not same? 0 9223372036854775806)
<64bit>
(not same? 0 9223372036854775807)
; 1 not same?
<64bit>
(not same? 1 -9223372036854775808)
<64bit>
(not same? 1 -9223372036854775807)
(not same? 1 -1)
(not same? 1 0)
<64bit>
(not same? 1 9223372036854775806)
<64bit>
(not same? 1 9223372036854775807)
; 9223372036854775806 not same?
<64bit>
(not same? 9223372036854775806 -9223372036854775808)
<64bit>
(not same? 9223372036854775806 -9223372036854775807)
<64bit>
(not same? 9223372036854775806 -1)
<64bit>
(not same? 9223372036854775806 0)
<64bit>
(not same? 9223372036854775806 1)
<64bit>
(not same? 9223372036854775806 9223372036854775807)
; 9223372036854775807 not same?
<64bit>
(not same? 9223372036854775807 -9223372036854775808)
<64bit>
(not same? 9223372036854775807 -9223372036854775807)
<64bit>
(not same? 9223372036854775807 -1)
<64bit>
(not same? 9223372036854775807 0)
<64bit>
(not same? 9223372036854775807 1)
<64bit>
(not same? 9223372036854775807 9223372036854775806)

; datatype differences
(not same? 0 0.0)
(not same? 0 0%)

; datatype differences
(not same? 0.0 0%)

; symmetry
(equal? same? 1 1.0 same? 1.0 1)
; symmetry
(equal? same? 1 100% same? 100% 1)
; symmetry
(equal? same? 1.0 100% same? 100% 1.0)
; approximate equality
(not same? 10% + 10% + 10% 30%)
; symmetry
(equal? same? 10% + 10% + 10% 30% same? 30% 10% + 10% + 10%)

(not same? 2-Jul-2009 2-Jul-2009/22:20)
(
    equal? same? 2-Jul-2009 2-Jul-2009/22:20 same? 2-Jul-2009/22:20 2-Jul-2009
)
(
    not same? 2-Jul-2009 2-Jul-2009/00:00:00+00:00
)
(
    equal? not same? 2-Jul-2009 2-Jul-2009/00:00 not same? 2-Jul-2009/00:00 2-Jul-2009
)
(
    not same? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00
)

; time!
(same? 00:00 00:00)
; missing components are 0
(same? 00:00 00:00:00)
; no timezone math
(not same? 22:20 20:20)
; char?/ symmetry
(equal? same? #"a" 97 same? 97 #"a")
; symmetry
(equal? same? #"a" 97.0 same? 97.0 #"a")
; case
(not same? #"a" #"A")
; case
(not same? "a" "A")
; words; reflexivity
(same? 'a 'a)
; aliases
(not same? 'a 'A)
; symmetry
(equal? same? 'a 'A same? 'A 'a)
; binding
(not same? $a use [a] [$a])
; symmetry
(equal? same? $a use [a] [$a] same? use [a] [$a] $a)
; different word types
(not same? 'a first [:a])
; symmetry
(equal? same? 'a first [:a] same? first [:a] 'a)
; different word types
(not same? 'a first ['a])
; symmetry
(equal? same? 'a first ['a] same? first ['a] 'a)
; different word types
(not same? 'a '/a)
; symmetry
(equal? same? 'a '/a same? '/a 'a)
; different word types
(not same? 'a first [a:])
; symmetry
(equal? same? 'a first [a:] same? first [a:] 'a)
; reflexivity
(same? first [:a] first [:a])
; different word types
(not same? first [:a] first ['a])
; symmetry
(equal? same? first [:a] first ['a] same? first ['a] first [:a])
; different word types
(not same? first [:a] '/a)
; symmetry
(equal? same? first [:a] '/a same? '/a first [:a])
; different word types
(not same? first [:a] first [a:])
; symmetry
(equal? same? first [:a] first [a:] same? first [a:] first [:a])
; reflexivity
(same? first ['a] first ['a])
; different word types
(not same? first ['a] '/a)
; symmetry
(equal? same? first ['a] '/a same? '/a first ['a])
; different word types
(not same? first ['a] first [a:])
; symmetry
(equal? same? first ['a] first [a:] same? first [a:] first ['a])
; reflexivity
(same? '/a '/a)
; different word types
(not same? '/a first [a:])
; symmetry
(equal? same? '/a first [a:] same? first [a:] '/a)
; reflexivity
(same? first [a:] first [a:])
; logic? values
(same? okay okay)
(same? null null)
(not same? okay null)
(not same? null okay)
; port! values; reflexivity; in this case the error should not be generated, I think
(
    p: make port! http://
    any [
        warning? rescue [same? p p]
        same? p p
    ]
)
