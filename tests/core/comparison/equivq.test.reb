; functions/comparison/equivq.r
; reflexivity test for native
[equiv? :abs :abs]
[equiv? :all :all]
[not equiv? :all :any]
; reflexivity test for infix
[equiv? :+ :+]
[not equiv? :+ :-]
; reflexivity test for function!
; Uses func instead of make function! so the test is compatible.
[equiv? a-value: func [] [] :a-value]
; no structural equivalence for function!
[not equiv? func [] [] func [] []]
; reflexivity test for closure!
; Uses CLOSURE to make the test compatible.
[equiv? a-value: closure [] [] :a-value]
; No structural equivalence for closure!
; Uses CLOSURE to make the test compatible.
[not equiv? closure [] [] closure [] []]
; binary!
; Same contents
[equiv? #{00} #{00}]
; Different contents
[not equiv? #{00} #{01}]
; Offset + similar contents at reference
[equiv? #{00} #[binary! [#{0000} 2]]]
; Offset + similar contents at reference
[equiv? #{00} #[binary! [#{0100} 2]]]
[equal? equiv? #{00} #[binary! [#{0100} 2]] equiv? #[binary! [#{0100} 2]] #{00}]
; No binary! padding
[not equiv? #{00} #{0000}]
[equal? equiv? #{00} #{0000} equiv? #{0000} #{00}]
; Empty binary! not blank
[not equiv? #{} blank]
; case sensitivity
; bug#1459
[not-equiv? #{0141} #{0161}]
; email versus string; RAMBO #3518
[
    a-value: to email! ""
    equiv? a-value to string! a-value
]
; symmetry
[
    a-value: to email! ""
    equal? equiv? to string! a-value a-value equiv? a-value to string! a-value
]
; file! vs. string!
; RAMBO #3518
[
    a-value: %""
    equiv? a-value to string! a-value
]
; symmetry
[
    a-value: %""
    equal? equiv? a-value to string! a-value equiv? to string! a-value a-value
]
; image! same contents
[equiv? a-value: #[image! [1x1 #{000000}]] a-value]
[equiv? #[image! [1x1 #{000000}]] #[image! [1x1 #{000000}]]]
[equiv? #[image! [1x1 #{}]] #[image! [1x1 #{000000}]]]
[not equiv? #{00} #[image! [1x1 #{00}]]]
; symmetry
[equal? equiv? #{00} #[image! [1x1 #{00}]] equiv? #[image! [1x1 #{00}]] #{00}]
[not equiv? #{00} to integer! #{00}]
; symmetry
[equal? equiv? #{00} to integer! #{00} equiv? to integer! #{00} #{00}]
; RAMBO #3518
[
    a-value: #a
    not-equiv? a-value to string! a-value
]
; symmetry
[
    a-value: #a
    equal? equiv? a-value to string! a-value equiv? to string! a-value a-value
]
; symmetry
[equal? equiv? #{} blank equiv? blank #{}]
[
    a-value: ""
    not equiv? a-value to binary! a-value
]
; symmetry
[
    a-value: ""
    equal? equiv? a-value to binary! a-value equiv? to binary! a-value a-value
]
; RAMBO #3518
[
    a-value: to tag! ""
    equiv? a-value to string! a-value
]
; symmetry
[
    a-value: to tag! ""
    equal? equiv? a-value to string! a-value equiv? to string! a-value a-value
]
[
    a-value: 0.0.0.0
    not equiv? to binary! a-value a-value
]
; symmetry
[
    a-value: 0.0.0.0
    equal? equiv? to binary! a-value a-value equiv? a-value to binary! a-value
]
[equiv? #[bitset! #{00}] #[bitset! #{00}]]
[not equiv? #[bitset! #{}] #[bitset! #{00}]]
; block!
[equiv? [] []]
; reflexivity
[
    a-value: []
    equiv? a-value a-value
]
; reflexivity for past-tail blocks
[
    a-value: tail [1]
    clear head a-value
    equiv? a-value a-value
]
; reflexivity for cyclic blocks
[
    a-value: copy []
    insert/only a-value a-value
    equiv? a-value a-value
]
; comparison of cyclic blocks
; bug#1049
[
    a-value: copy []
    insert/only a-value a-value
    b-value: copy []
    insert/only b-value b-value
    error? try [equiv? a-value b-value]
    true
]
[not equiv? [] blank]
[equal? equiv? [] blank equiv? blank []]
; block! vs. group!
[not equiv? [] first [()]]
; block! vs. group! symmetry
[equal? equiv? [] first [()] equiv? first [()] []]
; block! vs. path!
[not equiv? [a b] 'a/b]
; block! vs. path! symmetry
[
    a-value: 'a/b
    b-value: [a b]
    equal? equiv? :a-value :b-value equiv? :b-value :a-value
]
; block! vs. lit-path!
[not equiv? [a b] first ['a/b]]
; block! vs. lit-path! symmetry
[
    a-value: first ['a/b]
    b-value: [a b]
    equal? equiv? :a-value :b-value equiv? :b-value :a-value
]
; block! vs. set-path!
[not equiv? [a b] first [a/b:]]
; block! vs. set-path! symmetry
[
    a-value: first [a/b:]
    b-value: [a b]
    equal? equiv? :a-value :b-value equiv? :b-value :a-value
]
; block! vs. get-path!
[not equiv? [a b] first [:a/b]]
; block! vs. get-path! symmetry
[
    a-value: first [:a/b]
    b-value: [a b]
    equal? equiv? :a-value :b-value equiv? :b-value :a-value
]
[equiv? decimal! decimal!]
[not equiv? decimal! integer!]
[equal? equiv? decimal! integer! equiv? integer! decimal!]
[not equiv? any-number! integer!]
; symmetry
[equal? equiv? any-number! integer! equiv? integer! any-number!]
[not equiv? integer! make typeset! [integer!]]
[equal? equiv? integer! make typeset! [integer!] equiv? make typeset! [integer!] integer!]
; reflexivity
[equiv? -1 -1]
; reflexivity
[equiv? 0 0]
; reflexivity
[equiv? 1 1]
; reflexivity
[equiv? 0.0 0.0]
[equiv? 0.0 -0.0]
; reflexivity
[equiv? 1.0 1.0]
; reflexivity
[equiv? -1.0 -1.0]
; reflexivity
#64bit
[equiv? -9223372036854775808 -9223372036854775808]
; reflexivity
#64bit
[equiv? -9223372036854775807 -9223372036854775807]
; reflexivity
#64bit
[equiv? 9223372036854775807 9223372036854775807]
; -9223372036854775808 not equiv?
#64bit
[not equiv? -9223372036854775808 -9223372036854775807]
#64bit
[not equiv? -9223372036854775808 -1]
#64bit
[not equiv? -9223372036854775808 0]
#64bit
[not equiv? -9223372036854775808 1]
#64bit
[not equiv? -9223372036854775808 9223372036854775806]
#64bit
[not equiv? -9223372036854775808 9223372036854775807]
; -9223372036854775807 not equiv?
#64bit
[not equiv? -9223372036854775807 -9223372036854775808]
#64bit
[not equiv? -9223372036854775807 -1]
#64bit
[not equiv? -9223372036854775807 0]
#64bit
[not equiv? -9223372036854775807 1]
#64bit
[not equiv? -9223372036854775807 9223372036854775806]
#64bit
[not equiv? -9223372036854775807 9223372036854775807]
; -1 not equiv?
#64bit
[not equiv? -1 -9223372036854775808]
#64bit
[not equiv? -1 -9223372036854775807]
[not equiv? -1 0]
[not equiv? -1 1]
#64bit
[not equiv? -1 9223372036854775806]
#64bit
[not equiv? -1 9223372036854775807]
; 0 not equiv?
#64bit
[not equiv? 0 -9223372036854775808]
#64bit
[not equiv? 0 -9223372036854775807]
[not equiv? 0 -1]
[not equiv? 0 1]
#64bit
[not equiv? 0 9223372036854775806]
#64bit
[not equiv? 0 9223372036854775807]
; 1 not equiv?
#64bit
[not equiv? 1 -9223372036854775808]
#64bit
[not equiv? 1 -9223372036854775807]
[not equiv? 1 -1]
[not equiv? 1 0]
#64bit
[not equiv? 1 9223372036854775806]
#64bit
[not equiv? 1 9223372036854775807]
; 9223372036854775806 not equiv?
#64bit
[not equiv? 9223372036854775806 -9223372036854775808]
#64bit
[not equiv? 9223372036854775806 -9223372036854775807]
#64bit
[not equiv? 9223372036854775806 -1]
#64bit
[not equiv? 9223372036854775806 0]
#64bit
[not equiv? 9223372036854775806 1]
#64bit
[not equiv? 9223372036854775806 9223372036854775807]
; 9223372036854775807 not equiv?
#64bit
[not equiv? 9223372036854775807 -9223372036854775808]
#64bit
[not equiv? 9223372036854775807 -9223372036854775807]
#64bit
[not equiv? 9223372036854775807 -1]
#64bit
[not equiv? 9223372036854775807 0]
#64bit
[not equiv? 9223372036854775807 1]
#64bit
[not equiv? 9223372036854775807 9223372036854775806]
; "decimal tolerance"
; symmetry
[
    equal? equiv? to decimal! #{3FD3333333333333} to decimal! #{3FD3333333333334}
        equiv? to decimal! #{3FD3333333333334} to decimal! #{3FD3333333333333}
]
; symmetry
[
    equal? equiv? to decimal! #{3FB9999999999999} to decimal! #{3FB999999999999A}
        equiv? to decimal! #{3FB999999999999A} to decimal! #{3FB9999999999999}
]
; ignores datatype differences
[equiv? 0 0.0]
; ignores datatype differences
[equiv? 0 $0]
; ignores datatype differences
[equiv? 0 0%]
; ignores datatype differences
[equiv? 0.0 $0]
; ignores datatype differences
[equiv? 0.0 0%]
; ignores datatype differences
[equiv? $0 0%]
; symmetry
[equal? equiv? 1 1.0 equiv? 1.0 1]
; symmetry
[equal? equiv? 1 $1 equiv? $1 1]
; symmetry
[equal? equiv? 1 100% equiv? 100% 1]
; symmetry
[equal? equiv? 1.0 $1 equiv? $1 1.0]
; symmetry
[equal? equiv? 1.0 100% equiv? 100% 1.0]
; symmetry
[equal? equiv? $1 100% equiv? 100% $1]
; approximate equality
[equiv? 10% + 10% + 10% 30%]
; symmetry
[equal? equiv? 10% + 10% + 10% 30% equiv? 30% 10% + 10% + 10%]
; date!; approximate equality
[not equiv? 2-Jul-2009 2-Jul-2009/22:20]
; symmetry
[equal? equiv? 2-Jul-2009 2-Jul-2009/22:20 equiv? 2-Jul-2009/22:20 2-Jul-2009]
; missing time = 00:00:00+00:00, by time compatibility standards
[equiv? 2-Jul-2009 2-Jul-2009/00:00:00+00:00]
; symmetry
[equal? equiv? 2-Jul-2009 2-Jul-2009/00:00 equiv? 2-Jul-2009/00:00 2-Jul-2009]
; timezone in date!
[equiv? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00]
; time!; reflexivity
[equiv? 00:00 00:00]
; char!; symmetry
[equal? equiv? #"a" 97 equiv? 97 #"a"]
; symmetry
[equal? equiv? #"a" 97.0 equiv? 97.0 #"a"]
; case
[equiv? #"a" #"A"]
; case
[equiv? "a" "A"]
; words; reflexivity
[equiv? 'a 'a]
; aliases
[equiv? 'a 'A]
; symmetry
[equal? equiv? 'a 'A equiv? 'A 'a]
; symmetry
[equal? equiv? 'a use [a] ['a] equiv? use [a] ['a] 'a]
; different word types
[equiv? 'a first [:a]]
; symmetry
[equal? equiv? 'a first [:a] equiv? first [:a] 'a]
; different word types
[equiv? 'a first ['a]]
; symmetry
[equal? equiv? 'a first ['a] equiv? first ['a] 'a]
; different word types
[equiv? 'a /a]
; symmetry
[equal? equiv? 'a /a equiv? /a 'a]
; different word types
[equiv? 'a first [a:]]
; symmetry
[equal? equiv? 'a first [a:] equiv? first [a:] 'a]
; reflexivity
[equiv? first [:a] first [:a]]
; different word types
[equiv? first [:a] first ['a]]
; symmetry
[equal? equiv? first [:a] first ['a] equiv? first ['a] first [:a]]
; different word types
[equiv? first [:a] /a]
; symmetry
[equal? equiv? first [:a] /a equiv? /a first [:a]]
; different word types
[equiv? first [:a] first [a:]]
; symmetry
[equal? equiv? first [:a] first [a:] equiv? first [a:] first [:a]]
; reflexivity
[equiv? first ['a] first ['a]]
; different word types
[equiv? first ['a] /a]
; symmetry
[equal? equiv? first ['a] /a equiv? /a first ['a]]
; different word types
[equiv? first ['a] first [a:]]
; symmetry
[equal? equiv? first ['a] first [a:] equiv? first [a:] first ['a]]
; reflexivity
[equiv? /a /a]
; different word types
[equiv? /a first [a:]]
; symmetry
[equal? equiv? /a first [a:] equiv? first [a:] /a]
; reflexivity
[equiv? first [a:] first [a:]]
; logic! values
[equiv? true true]
[equiv? false false]
[not equiv? true false]
[not equiv? false true]
; port! values; reflexivity; in this case the error should not be generated, I think
[
    p: make port! http://
    any [
        error? try [equiv? p p]
        equiv? p p
    ]
]
