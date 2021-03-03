; functions/comparison/strict-equalq.r
(strict-equal? :abs :abs)
; reflexivity test for native
(strict-equal? :all :all)
; reflexivity test for infix
(strict-equal? :+ :+)
; reflexivity test for action!
(
    a-value: func [] []
    strict-equal? :a-value :a-value
)
; no structural equality for action!
(not strict-equal? func [] [] func [] [])
; binary!
(strict-equal? #{00} #{00})
; binary versus bitset
(not strict-equal? #{00} #[bitset! #{00}])
; symmetry
(equal? strict-equal? #[bitset! #{00}] #{00} strict-equal? #{00} #[bitset! #{00}])
; email versus text
(
    a-value: to email! ""
    not strict-equal? a-value to text! a-value
)
; symmetry
(
    a-value: to email! ""
    equal? strict-equal? to text! a-value a-value strict-equal? a-value to text! a-value
)
(
    a-value: %""
    not strict-equal? a-value to text! a-value
)
; symmetry
(
    a-value: %""
    equal? strict-equal? a-value to text! a-value strict-equal? to text! a-value a-value
)
(not strict-equal? #{00} #[image! [1x1 #{00000000}]])
; symmetry
(equal? strict-equal? #{00} #[image! [1x1 #{00000000}]] strict-equal? #[image! [1x1 #{00000000}]] #{00})
(not strict-equal? #{00} to integer! #{00})
; symmetry
(equal? strict-equal? #{00} to integer! #{00} strict-equal? to integer! #{00} #{00})
(
    a-value: #a
    not strict-equal? a-value to text! a-value
)
; symmetry
(
    a-value: #a
    equal? strict-equal? a-value to text! a-value strict-equal? to text! a-value a-value
)
(not strict-equal? #{} blank)
; symmetry
(equal? strict-equal? #{} blank strict-equal? blank #{})
(
    a-value: ""
    not strict-equal? a-value to binary! a-value
)
; symmetry
(
    a-value: ""
    equal? strict-equal? a-value to binary! a-value strict-equal? to binary! a-value a-value
)
(
    a-value: to tag! ""
    not strict-equal? a-value to text! a-value
)
; symmetry
(
    a-value: to tag! ""
    equal? strict-equal? a-value to text! a-value strict-equal? to text! a-value a-value
)
(
    a-value: 0.0.0.0
    not strict-equal? to binary! a-value a-value
)
; symmetry
(
    a-value: 0.0.0.0
    equal? strict-equal? to binary! a-value a-value strict-equal? a-value to binary! a-value
)
(strict-equal? #[bitset! #{00}] #[bitset! #{00}])
(not strict-equal? #[bitset! #{}] #[bitset! #{00}])
; block!
(strict-equal? [] [])
; reflexivity
(
    a-value: []
    strict-equal? a-value a-value
)

; error for past-tail blocks
(
    a-value: tail of [1]
    clear head of a-value
    e: trap [strict-equal? a-value a-value]
    e/id = 'index-out-of-range
)

; reflexivity for cyclic blocks
(
    a-value: copy []
    insert/only a-value a-value
    strict-equal? a-value a-value
)
; comparison of cyclic blocks
[#1049 (
    a-value: copy []
    insert/only a-value a-value
    b-value: copy []
    insert/only b-value b-value
    error? trap [strict-equal? a-value b-value]
    true
)]
(not strict-equal? [] blank)
; symmetry
(equal? strict-equal? [] blank strict-equal? blank [])
[#1068 #1066 (
    a-value: first [()]
    parse a-value [b-value: here, end]
    strict-equal? a-value b-value
)]
(not strict-equal? any-number! integer!)
; symmetry
(equal? strict-equal? any-number! integer! strict-equal? integer! any-number!)
; reflexivity
(strict-equal? -1 -1)
; reflexivity
(strict-equal? 0 0)
; reflexivity
(strict-equal? 1 1)
; reflexivity
(strict-equal? 0.0 0.0)
(strict-equal? 0.0 -0.0)
; reflexivity
(strict-equal? 1.0 1.0)
; reflexivity
(strict-equal? -1.0 -1.0)
; reflexivity
<64bit>
(strict-equal? -9223372036854775808 -9223372036854775808)
; reflexivity
<64bit>
(strict-equal? -9223372036854775807 -9223372036854775807)
; reflexivity
<64bit>
(strict-equal? 9223372036854775807 9223372036854775807)
; -9223372036854775808 not strict-equal?
<64bit>
(not strict-equal? -9223372036854775808 -9223372036854775807)
<64bit>
(not strict-equal? -9223372036854775808 -1)
<64bit>
(not strict-equal? -9223372036854775808 0)
<64bit>
(not strict-equal? -9223372036854775808 1)
<64bit>
(not strict-equal? -9223372036854775808 9223372036854775806)
<64bit>
(not strict-equal? -9223372036854775808 9223372036854775807)
; -9223372036854775807 not strict-equal?
<64bit>
(not strict-equal? -9223372036854775807 -9223372036854775808)
<64bit>
(not strict-equal? -9223372036854775807 -1)
<64bit>
(not strict-equal? -9223372036854775807 0)
<64bit>
(not strict-equal? -9223372036854775807 1)
<64bit>
(not strict-equal? -9223372036854775807 9223372036854775806)
<64bit>
(not strict-equal? -9223372036854775807 9223372036854775807)
; -1 not strict-equal?
<64bit>
(not strict-equal? -1 -9223372036854775808)
<64bit>
(not strict-equal? -1 -9223372036854775807)
(not strict-equal? -1 0)
(not strict-equal? -1 1)
<64bit>
(not strict-equal? -1 9223372036854775806)
<64bit>
(not strict-equal? -1 9223372036854775807)
; 0 not strict-equal?
<64bit>
(not strict-equal? 0 -9223372036854775808)
<64bit>
(not strict-equal? 0 -9223372036854775807)
(not strict-equal? 0 -1)
(not strict-equal? 0 1)
<64bit>
(not strict-equal? 0 9223372036854775806)
<64bit>
(not strict-equal? 0 9223372036854775807)
; 1 not strict-equal?
<64bit>
(not strict-equal? 1 -9223372036854775808)
<64bit>
(not strict-equal? 1 -9223372036854775807)
(not strict-equal? 1 -1)
(not strict-equal? 1 0)
<64bit>
(not strict-equal? 1 9223372036854775806)
<64bit>
(not strict-equal? 1 9223372036854775807)
; 9223372036854775806 not strict-equal?
<64bit>
(not strict-equal? 9223372036854775806 -9223372036854775808)
<64bit>
(not strict-equal? 9223372036854775806 -9223372036854775807)
<64bit>
(not strict-equal? 9223372036854775806 -1)
<64bit>
(not strict-equal? 9223372036854775806 0)
<64bit>
(not strict-equal? 9223372036854775806 1)
<64bit>
(not strict-equal? 9223372036854775806 9223372036854775807)
; 9223372036854775807 not strict-equal?
<64bit>
(not strict-equal? 9223372036854775807 -9223372036854775808)
<64bit>
(not strict-equal? 9223372036854775807 -9223372036854775807)
<64bit>
(not strict-equal? 9223372036854775807 -1)
<64bit>
(not strict-equal? 9223372036854775807 0)
<64bit>
(not strict-equal? 9223372036854775807 1)
<64bit>
(not strict-equal? 9223372036854775807 9223372036854775806)
; #1134 "decimal tolerance"
(not strict-equal? to decimal! #{3FD3333333333333} to decimal! #{3FD3333333333334})
; symmetry
(
    equal? strict-equal? to decimal! #{3FD3333333333333} to decimal! #{3FD3333333333334}
        strict-equal? to decimal! #{3FD3333333333334} to decimal! #{3FD3333333333333}
)
(not strict-equal? to decimal! #{3FB9999999999999} to decimal! #{3FB999999999999A})
; symmetry
(
    equal? strict-equal? to decimal! #{3FB9999999999999} to decimal! #{3FB999999999999A}
        strict-equal? to decimal! #{3FB999999999999A} to decimal! #{3FB9999999999999}
)
; datatype differences
(not strict-equal? 0 0.0)
; datatype differences
(not strict-equal? 0 $0)
; datatype differences
(not strict-equal? 0 0%)
; datatype differences
(not strict-equal? 0.0 $0)
; datatype differences
(not strict-equal? 0.0 0%)
; datatype differences
(not strict-equal? $0 0%)
; symmetry
(equal? strict-equal? 1 1.0 strict-equal? 1.0 1)
; symmetry
(equal? strict-equal? 1 $1 strict-equal? $1 1)
; symmetry
(equal? strict-equal? 1 100% strict-equal? 100% 1)
; symmetry
(equal? strict-equal? 1.0 $1 strict-equal? $1 1.0)
; symmetry
(equal? strict-equal? 1.0 100% strict-equal? 100% 1.0)
; symmetry
(equal? strict-equal? $1 100% strict-equal? 100% $1)
; approximate equality
(not strict-equal? 10% + 10% + 10% 30%)
; symmetry
(equal? strict-equal? 10% + 10% + 10% 30% strict-equal? 30% 10% + 10% + 10%)
; date!; approximate equality
(not strict-equal? 2-Jul-2009 2-Jul-2009/22:20)
; symmetry
(equal? strict-equal? 2-Jul-2009 2-Jul-2009/22:20 strict-equal? 2-Jul-2009/22:20 2-Jul-2009)
; missing time = 00:00:00+00:00, by time compatibility standards
(not strict-equal? 2-Jul-2009 2-Jul-2009/00:00:00+00:00)
; symmetry
(equal? strict-equal? 2-Jul-2009 2-Jul-2009/00:00 strict-equal? 2-Jul-2009/00:00 2-Jul-2009)
; no timezone math in date!
(not strict-equal? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00)
; time!
(strict-equal? 00:00 00:00)
; char!; symmetry
(equal? strict-equal? #"a" 97 strict-equal? 97 #"a")
; symmetry
(equal? strict-equal? #"a" 97.0 strict-equal? 97.0 #"a")
; case
(not strict-equal? #"a" #"A")
; case
(not strict-equal? "a" "A")
; words; reflexivity
(strict-equal? 'a 'a)
; aliases
(not strict-equal? 'a 'A)
; symmetry
(equal? strict-equal? 'a 'A strict-equal? 'A 'a)
; binding not checked by STRICT-EQUAL? in Ren-C (only casing and type)
(strict-equal? 'a use [a] ['a])
; symmetry
(equal? strict-equal? 'a use [a] ['a] strict-equal? use [a] ['a] 'a)
; different word types
(not strict-equal? 'a first [:a])
; symmetry
(equal? strict-equal? 'a first [:a] strict-equal? first [:a] 'a)
; different word types
(not strict-equal? 'a first ['a])
; symmetry
(equal? strict-equal? 'a first ['a] strict-equal? first ['a] 'a)
; different word types
(not strict-equal? 'a /a)
; symmetry
(equal? strict-equal? 'a /a strict-equal? /a 'a)
; different word types
(not strict-equal? 'a first [a:])
; symmetry
(equal? strict-equal? 'a first [a:] strict-equal? first [a:] 'a)
; reflexivity
(strict-equal? first [:a] first [:a])
; different word types
(not strict-equal? first [:a] first ['a])
; symmetry
(equal? strict-equal? first [:a] first ['a] strict-equal? first ['a] first [:a])
; different word types
(not strict-equal? first [:a] /a)
; symmetry
(equal? strict-equal? first [:a] /a strict-equal? /a first [:a])
; different word types
(not strict-equal? first [:a] first [a:])
; symmetry
(equal? strict-equal? first [:a] first [a:] strict-equal? first [a:] first [:a])
; reflexivity
(strict-equal? first ['a] first ['a])
; different word types
(not strict-equal? first ['a] /a)
; symmetry
(equal? strict-equal? first ['a] /a strict-equal? /a first ['a])
; different word types
(not strict-equal? first ['a] first [a:])
; symmetry
(equal? strict-equal? first ['a] first [a:] strict-equal? first [a:] first ['a])
; reflexivity
(strict-equal? /a /a)
; different word types
(not strict-equal? /a first [a:])
; symmetry
(equal? strict-equal? /a first [a:] strict-equal? first [a:] /a)
; reflexivity
(strict-equal? first [a:] first [a:])
; logic! values
(strict-equal? true true)
(strict-equal? false false)
(not strict-equal? true false)
(not strict-equal? false true)
; port! values; reflexivity; in this case the error should not be generated, I think
(
    p: make port! http://
    any [
        error? trap [strict-equal? p p]
        strict-equal? p p
    ]
)
