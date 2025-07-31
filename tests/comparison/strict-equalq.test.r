; functions/comparison/strict-equalq.r
(equal? abs/ abs/)
; reflexivity test for native
(equal? all/ all/)
; reflexivity test for infix
(equal? +/ +/)
; reflexivity test for action
(
    a-value: func [] []
    equal? a-value/ a-value/
)
; no structural equality for action
(not equal? func [] [] func [] [])
; blob!
(equal? #{00} #{00})
; binary versus bitset
(not equal? #{00} make bitset! #{00})
; symmetry
(equal? equal? make bitset! #{00} #{00} equal? #{00} make bitset! #{00})
(
    a-value: %""
    not equal? a-value to text! a-value
)
; symmetry
(
    a-value: %""
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)

(not equal? #{00} decode [BE +/-] #{00})
; symmetry
(equal? equal? #{00} decode [BE +/-] #{00} equal? decode [BE +/-] #{00} #{00})
(
    a-value: #a
    not equal? a-value to text! a-value
)
; symmetry
(
    a-value: #a
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)
(not equal? #{} space)
; symmetry
(equal? equal? #{} space equal? space #{})
(
    a-value: to tag! ""
    not equal? a-value to text! a-value
)
; symmetry
(
    a-value: to tag! ""
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)
(
    a-value: 0.0.0.0
    not equal? to blob! a-value a-value
)
; symmetry
(
    a-value: 0.0.0.0
    equal? equal? to blob! a-value a-value equal? a-value to blob! a-value
)
(equal? make bitset! #{00} make bitset! #{00})
(not equal? make bitset! #{} make bitset! #{00})
; block!
(equal? [] [])
; reflexivity
(
    a-value: []
    equal? a-value a-value
)

~index-out-of-range~ !! (  ; error for past-tail blocks
    a-value: tail of [1]
    clear head of a-value
    equal? a-value a-value
)

; reflexivity for cyclic blocks
(
    a-value: copy []
    insert a-value a-value
    equal? a-value a-value
)

; comparison of cyclic blocks
; [#1049
;     ~stack-overflow~ !! (
;         a-value: copy []
;         insert a-value a-value
;         b-value: copy []
;         insert b-value b-value
;        equal? a-value b-value
;    )
; ]

(not equal? [] space)
; symmetry
(equal? equal? [] space equal? space [])
[#1068 #1066 (
    a-value: first [()]
    b-value: ~
    parse a-value [b-value: <here>, to <end>]
    equal? a-value b-value
)]
(not equal? any-number?/ integer!)
; symmetry
(equal? equal? any-number?/ integer! equal? integer! any-number?/)
; reflexivity
(equal? -1 -1)
; reflexivity
(equal? 0 0)
; reflexivity
(equal? 1 1)
; reflexivity
(equal? 0.0 0.0)
(equal? 0.0 -0.0)
; reflexivity
(equal? 1.0 1.0)
; reflexivity
(equal? -1.0 -1.0)
; reflexivity
<64bit>
(equal? -9223372036854775808 -9223372036854775808)
; reflexivity
<64bit>
(equal? -9223372036854775807 -9223372036854775807)
; reflexivity
<64bit>
(equal? 9223372036854775807 9223372036854775807)
; -9223372036854775808 not equal?
<64bit>
(not equal? -9223372036854775808 -9223372036854775807)
<64bit>
(not equal? -9223372036854775808 -1)
<64bit>
(not equal? -9223372036854775808 0)
<64bit>
(not equal? -9223372036854775808 1)
<64bit>
(not equal? -9223372036854775808 9223372036854775806)
<64bit>
(not equal? -9223372036854775808 9223372036854775807)
; -9223372036854775807 not equal?
<64bit>
(not equal? -9223372036854775807 -9223372036854775808)
<64bit>
(not equal? -9223372036854775807 -1)
<64bit>
(not equal? -9223372036854775807 0)
<64bit>
(not equal? -9223372036854775807 1)
<64bit>
(not equal? -9223372036854775807 9223372036854775806)
<64bit>
(not equal? -9223372036854775807 9223372036854775807)
; -1 not equal?
<64bit>
(not equal? -1 -9223372036854775808)
<64bit>
(not equal? -1 -9223372036854775807)
(not equal? -1 0)
(not equal? -1 1)
<64bit>
(not equal? -1 9223372036854775806)
<64bit>
(not equal? -1 9223372036854775807)
; 0 not equal?
<64bit>
(not equal? 0 -9223372036854775808)
<64bit>
(not equal? 0 -9223372036854775807)
(not equal? 0 -1)
(not equal? 0 1)
<64bit>
(not equal? 0 9223372036854775806)
<64bit>
(not equal? 0 9223372036854775807)
; 1 not equal?
<64bit>
(not equal? 1 -9223372036854775808)
<64bit>
(not equal? 1 -9223372036854775807)
(not equal? 1 -1)
(not equal? 1 0)
<64bit>
(not equal? 1 9223372036854775806)
<64bit>
(not equal? 1 9223372036854775807)
; 9223372036854775806 not equal?
<64bit>
(not equal? 9223372036854775806 -9223372036854775808)
<64bit>
(not equal? 9223372036854775806 -9223372036854775807)
<64bit>
(not equal? 9223372036854775806 -1)
<64bit>
(not equal? 9223372036854775806 0)
<64bit>
(not equal? 9223372036854775806 1)
<64bit>
(not equal? 9223372036854775806 9223372036854775807)
; 9223372036854775807 not equal?
<64bit>
(not equal? 9223372036854775807 -9223372036854775808)
<64bit>
(not equal? 9223372036854775807 -9223372036854775807)
<64bit>
(not equal? 9223372036854775807 -1)
<64bit>
(not equal? 9223372036854775807 0)
<64bit>
(not equal? 9223372036854775807 1)
<64bit>
(not equal? 9223372036854775807 9223372036854775806)

; datatype differences
(not equal? 0 0.0)
(not equal? 0.0 0%)

; symmetry
(equal? equal? 1 1.0 equal? 1.0 1)
; symmetry
(equal? equal? 1 100% equal? 100% 1)

; symmetry
(equal? equal? 1.0 100% equal? 100% 1.0)

; approximate equality
(not equal? 10% + 10% + 10% 30%)
; symmetry
(equal? equal? 10% + 10% + 10% 30% equal? 30% 10% + 10% + 10%)

(not equal? 2-Jul-2009 2-Jul-2009/22:20)
(
    equal? equal? 2-Jul-2009 2-Jul-2009/22:20 equal? 2-Jul-2009/22:20 2-Jul-2009
)
(
    not equal? 2-Jul-2009 2-Jul-2009/00:00:00+00:00
)
(
    equal? equal? 2-Jul-2009 2-Jul-2009/00:00 equal? 2-Jul-2009/00:00 2-Jul-2009
)
(
    not equal? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00
)

; time!
(equal? 00:00 00:00)
; char?/ symmetry
(equal? equal? #"a" 97 equal? 97 #"a")
; symmetry
(equal? equal? #"a" 97.0 equal? 97.0 #"a")
; case
(not equal? #"a" #"A")
; case
(not equal? "a" "A")
; words; reflexivity
(equal? 'a 'a)
; aliases
(not equal? 'a 'A)
; symmetry
(equal? equal? 'a 'A equal? 'A 'a)
; binding not checked by STRICT-EQUAL? in Ren-C (only casing and type)
(equal? 'a use [a] ['a])
; symmetry
(equal? equal? 'a use [a] ['a] equal? use [a] ['a] 'a)
; different word types
(not equal? 'a first [:a])
; symmetry
(equal? equal? 'a first [:a] equal? first [:a] 'a)
; different word types
(not equal? 'a first ['a])
; symmetry
(equal? equal? 'a first ['a] equal? first ['a] 'a)
; different word types
(not equal? 'a '/a)
; symmetry
(equal? equal? 'a '/a equal? '/a 'a)
; different word types
(not equal? 'a first [a:])
; symmetry
(equal? equal? 'a first [a:] equal? first [a:] 'a)
; reflexivity
(equal? first [:a] first [:a])
; different word types
(not equal? first [:a] first ['a])
; symmetry
(equal? equal? first [:a] first ['a] equal? first ['a] first [:a])
; different word types
(not equal? first [:a] '/a)
; symmetry
(equal? equal? first [:a] '/a equal? '/a first [:a])
; different word types
(not equal? first [:a] first [a:])
; symmetry
(equal? equal? first [:a] first [a:] equal? first [a:] first [:a])
; reflexivity
(equal? first ['a] first ['a])
; different word types
(not equal? first ['a] '/a)
; symmetry
(equal? equal? first ['a] '/a equal? '/a first ['a])
; different word types
(not equal? first ['a] first [a:])
; symmetry
(equal? equal? first ['a] first [a:] equal? first [a:] first ['a])
; reflexivity
(equal? '/a '/a)
; different word types
(not equal? '/a first [a:])
; symmetry
(equal? equal? '/a first [a:] equal? first [a:] '/a)
; reflexivity
(equal? first [a:] first [a:])
; logic? values
(equal? okay okay)
(equal? null null)
(not equal? okay null)
(not equal? null okay)
; port! values; reflexivity; in this case the error should not be generated, I think
(
    p: make port! http://
    any [
        warning? rescue [equal? p p]
        equal? p p
    ]
)
