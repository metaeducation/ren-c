; functions/comparison/equalq.r
; reflexivity test for native
(is? :abs :abs)
(not is? :abs :add)
(is? :all :all)
(not is? :all :any)
; reflexivity test for infix
(is? :+ :+)
(not is? :+ :-)
; reflexivity test for action!
(is? a-value: func [] [] :a-value)
; No structural equivalence for action!
(not is? func [] [] func [] [])
(is? a-value: #{00} a-value)
; binary!
; Same contents
(is? #{00} #{00})
; Different contents
(not is? #{00} #{01})
; Offset + similar contents at reference
(is? #{00} #[binary! [#{0000} 2]])
; Offset + similar contents at reference
(is? #{00} #[binary! [#{0100} 2]])
(is? is? #{00} #[binary! [#{0100} 2]] is? #[binary! [#{0100} 2]] #{00})
; No binary! padding
(not is? #{00} #{0000})
(is? is? #{00} #{0000} is? #{0000} #{00})
; Empty binary! not blank
(not is? #{} blank)
(is? is? #{} blank is? blank #{})
; case sensitivity
[#1459
    (isn't? #{0141} #{0161})
]

[#3518 {RAMBO} (
    s: ""
    e: to email! s
    did all [
        s == to text! e
        e == to email! to text! e
    ]
)(
    f: %""
    e: to email! f
    did all [
        f == to file! e
        e == to email! to file! e
    ]
)]

; image! same contents
(is? a-value: #[image! [1x1 #{000000}]] a-value)
(is? #[image! [1x1 #{000000}]] #[image! [1x1 #{000000}]])
(is? #[image! [1x1 #{}]] #[image! [1x1 #{000000}]])
; image! different size
(not is? #[image! [1x2 #{000000}]] #[image! [1x1 #{000000}]])
; image! different size
(not is? #[image! [2x1 #{000000}]] #[image! [1x1 #{000000}]])
; image! different rgb
(not is? #[image! [1x1 #{000001}]] #[image! [1x1 #{000000}]])
; image! alpha not specified = ff
(is? #[image! [1x1 #{000000} #{ff}]] #[image! [1x1 #{000000}]])
; image! alpha different
(not is? #[image! [1x1 #{000000} #{01}]] #[image! [1x1 #{000000} #{00}]])
; Literal offset not supported in R2.
(is? #[image! [1x1 #{000000} 2]] #[image! [1x1 #{000000} 2]])
; Literal offset not supported in R2.
(not is? #[image! [1x1 #{000000} 2]] #[image! [1x1 #{000000}]])
(
    a-value: #[image! [1x1 #{000000}]]
    not is? a-value next a-value
)
; image! offset + structural equivalence
(is? #[image! [0x0 #{}]] next #[image! [1x1 #{000000}]])
; image! offset + structural equivalence
(is? #[image! [1x0 #{}]] next #[image! [1x1 #{000000}]])
; image! offset + structural equivalence
(is? #[image! [0x1 #{}]] next #[image! [1x1 #{000000}]])
<r2>
; image! offset + structural equivalence
(not is? #[image! [0x0 #{}]] next #[image! [1x1 #{000000}]])
<r2>
; image! offset + structural equivalence
(not is? #[image! [1x0 #{}]] next #[image! [1x1 #{000000}]])
<r2>
; image! offset + structural equivalence
(not is? #[image! [0x1 #{}]] next #[image! [1x1 #{000000}]])
; No implicit to binary! from image!
(not is? #{00} #[image! [1x1 #{000000}]])
; No implicit to binary! from image!
(not is? #{00000000} #[image! [1x1 #{000000}]])
; No implicit to binary! from image!
(not is? #{0000000000} #[image! [1x1 #{000000}]])
(is? is? #{00} #[image! [1x1 #{00}]] is? #[image! [1x1 #{00}]] #{00})
; No implicit to binary! from integer!
(not is? #{00} to integer! #{00})
(is? is? #{00} to integer! #{00} is? to integer! #{00} #{00})
; issue! vs. text!
; RAMBO #3518
(isn't? a-value: #a to text! a-value)
(
    a-value: #a
    is? is? a-value to text! a-value is? to text! a-value a-value
)
; No implicit to binary! from text!
(not is? a-value: "" to binary! a-value)
(
    a-value: ""
    is? is? a-value to binary! a-value is? to binary! a-value a-value
)

[RAMBO/#3518 (
    (a-value: to tag! "") isn't (to text! a-value)
)]

(
    a-value: to tag! ""
    is? is? a-value to text! a-value is? to text! a-value a-value
)
(is? 0.0.0 0.0.0)
(not is? 0.0.1 0.0.0)
; tuple! right-pads with 0
(is? 1.0.0 1.0.0.0.0.0.0)
; tuple! right-pads with 0
(is? 1.0.0.0.0.0.0 1.0.0)
; No implicit to binary! from tuple!
(
    a-value: 0.0.0.0
    not is? to binary! a-value a-value
)
(
    a-value: 0.0.0.0
    is? is? to binary! a-value a-value is? a-value to binary! a-value
)
(is? #[bitset! #{00}] #[bitset! #{00}])
; bitset! with no bits set does not equal empty bitset
; This is because of the COMPLEMENT problem: bug#1085.
(not is? #[bitset! #{}] #[bitset! #{00}])
; No implicit to binary! from bitset!
(not is? #{00} #[bitset! #{00}])
(is? is? #[bitset! #{00}] #{00} is? #{00} #[bitset! #{00}])
(is? [] [])
(is? a-value: [] a-value)
; Reflexivity for past-tail blocks
; Error in R2.
(
    a-value: tail of [1]
    clear head of a-value
    is? a-value a-value
)
; Reflexivity for cyclic blocks
(
    a-value: copy []
    insert/only a-value a-value
    is? a-value a-value
)
; Comparison of cyclic blocks
; NOTE: The stackoverflow will likely trigger in valgrind an error such as:
; "Warning: client switching stacks?  SP change: 0xffec17f68 --> 0xffefff860"
; "         to suppress, use: --max-stackframe=4094200 or greater"
[#1049 (
    a-value: copy []
    insert/only a-value a-value
    b-value: copy []
    insert/only b-value b-value
    error? trap [is? a-value b-value]
    true
)]
(not is? [] blank)
(is? is? [] blank is? blank [])
; block! vs. group!
(not is? [] first [()])
; block! vs. group! symmetry
(is? is? [] first [()] is? first [()] [])
; block! vs. path!
(not is? [a b] 'a/b)
; block! vs. path! symmetry
(
    a-value: 'a/b
    b-value: [a b]
    is? is? :a-value :b-value is? :b-value :a-value
)
; block! vs. lit-path!
(not is? [a b] first ['a/b])
; block! vs. lit-path! symmetry
(
    a-value: first ['a/b]
    b-value: [a b]
    is? is? :a-value :b-value is? :b-value :a-value
)
; block! vs. set-path!
(not is? [a b] first [a/b:])
; block! vs. set-path! symmetry
(
    a-value: first [a/b:]
    b-value: [a b]
    is? is? :a-value :b-value is? :b-value :a-value
)
; block! vs. get-path!
(not is? [a b] first [:a/b])
; block! vs. get-path! symmetry
(
    a-value: first [:a/b]
    b-value: [a b]
    is? is? :a-value :b-value is? :b-value :a-value
)
(is? decimal! decimal!)
(not is? decimal! integer!)
(is? is? decimal! integer! is? integer! decimal!)
; datatype! vs. typeset!
(not is? any-number! integer!)
; datatype! vs. typeset! symmetry
(is? is? any-number! integer! is? integer! any-number!)
; datatype! vs. typeset!
(not is? integer! make typeset! [integer!])
; datatype! vs. typeset!
(not is? integer! to typeset! [integer!])
; datatype! vs. typeset!
; Supported by R2/Forward.
(not is? integer! to-typeset [integer!])
; typeset! (or pseudo-type in R2)
(is? any-number! any-number!)
; typeset! (or pseudo-type in R2)
(not is? any-number! any-series!)
(is? make typeset! [integer!] make typeset! [integer!])
(is? to typeset! [integer!] to typeset! [integer!])
; Supported by R2/Forward.
(is? to-typeset [integer!] to-typeset [integer!])
(is? -1 -1)
(is? 0 0)
(is? 1 1)
(is? 0.0 0.0)
(is? 0.0 -0.0)
(is? 1.0 1.0)
(is? -1.0 -1.0)
<64bit>
(is? -9223372036854775808 -9223372036854775808)
<64bit>
(is? -9223372036854775807 -9223372036854775807)
<64bit>
(is? 9223372036854775807 9223372036854775807)
<64bit>
(not is? -9223372036854775808 -9223372036854775807)
<64bit>
(not is? -9223372036854775808 -1)
<64bit>
(not is? -9223372036854775808 0)
<64bit>
(not is? -9223372036854775808 1)
<64bit>
(not is? -9223372036854775808 9223372036854775806)
<64bit>
(not is? -9223372036854775807 -9223372036854775808)
<64bit>
(not is? -9223372036854775807 -1)
<64bit>
(not is? -9223372036854775807 0)
<64bit>
(not is? -9223372036854775807 1)
<64bit>
(not is? -9223372036854775807 9223372036854775806)
<64bit>
(not is? -9223372036854775807 9223372036854775807)
<64bit>
(not is? -1 -9223372036854775808)
<64bit>
(not is? -1 -9223372036854775807)
(not is? -1 0)
(not is? -1 1)
<64bit>
(not is? -1 9223372036854775806)
<64bit>
(not is? -1 9223372036854775807)
<64bit>
(not is? 0 -9223372036854775808)
<64bit>
(not is? 0 -9223372036854775807)
(not is? 0 -1)
(not is? 0 1)
<64bit>
(not is? 0 9223372036854775806)
<64bit>
(not is? 0 9223372036854775807)
<64bit>
(not is? 1 -9223372036854775808)
<64bit>
(not is? 1 -9223372036854775807)
(not is? 1 -1)
(not is? 1 0)
<64bit>
(not is? 1 9223372036854775806)
<64bit>
(not is? 1 9223372036854775807)
<64bit>
(not is? 9223372036854775806 -9223372036854775808)
<64bit>
(not is? 9223372036854775806 -9223372036854775807)
<64bit>
(not is? 9223372036854775806 -1)
<64bit>
(not is? 9223372036854775806 0)
<64bit>
(not is? 9223372036854775806 1)
<64bit>
(not is? 9223372036854775806 9223372036854775807)
<64bit>
(not is? 9223372036854775807 -9223372036854775808)
<64bit>
(not is? 9223372036854775807 -9223372036854775807)
<64bit>
(not is? 9223372036854775807 -1)
<64bit>
(not is? 9223372036854775807 0)
<64bit>
(not is? 9223372036854775807 1)
<64bit>
(not is? 9223372036854775807 9223372036854775806)
; decimal! approximate equality
(is? 0.3 0.1 + 0.1 + 0.1)
; decimal! approximate equality symmetry
(is? is? 0.3 0.1 + 0.1 + 0.1 is? 0.1 + 0.1 + 0.1 0.3)
(is? 0.15 - 0.05 0.1)
(is? is? 0.15 - 0.05 0.1 is? 0.1 0.15 - 0.05)
(is? -0.5 cosine 120)
(is? is? -0.5 cosine 120 is? cosine 120 -0.5)
(is? 0.5 * square-root 2.0 sine 45)
(is? is? 0.5 * square-root 2.0 sine 45 is? sine 45 0.5 * square-root 2.0)
(is? 0.5 sine 30)
(is? is? 0.5 sine 30 is? sine 30 0.5)
(is? 0.5 cosine 60)
(is? is? 0.5 cosine 60 is? cosine 60 0.5)
(is? 0.5 * square-root 3.0 sine 60)
(is? is? 0.5 * square-root 3.0 sine 60 is? sine 60 0.5 * square-root 3.0)
(is? 0.5 * square-root 3.0 cosine 30)
(is? is? 0.5 * square-root 3.0 cosine 30 is? cosine 30 0.5 * square-root 3.0)
(is? square-root 3.0 tangent 60)
(is? is? square-root 3.0 tangent 60 is? tangent 60 square-root 3.0)
(is? (square-root 3.0) / 3.0 tangent 30)
(is? is? (square-root 3.0) / 3.0 tangent 30 is? tangent 30 (square-root 3.0) / 3.0)
(is? 1.0 tangent 45)
(is? is? 1.0 tangent 45 is? tangent 45 1.0)
(
    num: square-root 2.0
    is? 2.0 num * num
)
(
    num: square-root 2.0
    is? is? 2.0 num * num is? num * num 2.0
)
(
    num: square-root 3.0
    is? 3.0 num * num
)
(
    num: square-root 3.0
    is? is? 3.0 num * num is? num * num 3.0
)
; integer! vs. decimal!
(is? 0 0.0)
; integer! vs. money!
(is? 0 $0)
; integer! vs. percent!
(is? 0 0%)
; decimal! vs. money!
(is? 0.0 $0)
; decimal! vs. percent!
(is? 0.0 0%)
; money! vs. percent!
(is? $0 0%)
; integer! vs. decimal! symmetry
(is? is? 1 1.0 is? 1.0 1)
; integer! vs. money! symmetry
(is? is? 1 $1 is? $1 1)
; integer! vs. percent! symmetry
(is? is? 1 100% is? 100% 1)
; decimal! vs. money! symmetry
(is? is? 1.0 $1 is? $1 1.0)
; decimal! vs. percent! symmetry
(is? is? 1.0 100% is? 100% 1.0)
; money! vs. percent! symmetry
(is? is? $1 100% is? 100% $1)
; percent! approximate equality
(is? 10% + 10% + 10% 30%)
; percent! approximate equality symmetry
(is? is? 10% + 10% + 10% 30% is? 30% 10% + 10% + 10%)
(is? 2-Jul-2009 2-Jul-2009)
; date! doesn't ignore time portion
(not is? 2-Jul-2009 2-Jul-2009/22:20)
(is? is? 2-Jul-2009 2-Jul-2009/22:20 is? 2-Jul-2009/22:20 2-Jul-2009)

; R3-Alpha considered date! missing time and zone = 00:00:00+00:00.  But
; in Ren-C, dates without a time are semantically distinct from a date with
; a time at midnight.
;
(not is? 2-Jul-2009 2-Jul-2009/00:00:00+00:00)

(is? is? 2-Jul-2009 2-Jul-2009/00:00 is? 2-Jul-2009/00:00 2-Jul-2009)
; Timezone math in date!
(is? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00)
(is? 00:00 00:00)
; time! missing components are 0
(is? 0:0 00:00:00.0000000000)
(is? is? 0:0 00:00:00.0000000000 is? 00:00:00.0000000000 0:0)
; time! vs. integer!
[#1103
    (not is? 0:00 0)
]
; integer! vs. time!
[#1103
    (not is? 0 00:00)
]
(is? #"a" #"a")
; char! vs. integer!
; No implicit to char! from integer! in R3.
(not is? #"a" 97)
; char! vs. integer! symmetry
(is? is? #"a" 97 is? 97 #"a")
; char! vs. decimal!
; No implicit to char! from decimal! in R3.
(not is? #"a" 97.0)
; char! vs. decimal! symmetry
(is? is? #"a" 97.0 is? 97.0 #"a")
; char! case
(is? #"a" #"A")
; text! case
(is? "a" "A")
; issue! case
(is? #a #A)
; tag! case
(is? <a a="a"> <A A="A">)
; url! case
(is? http://a.com httP://A.coM)
; email! case
(is? a@a.com A@A.Com)
(is? 'a 'a)
(is? 'a 'A)
(is? is? 'a 'A is? 'A 'a)
; word binding
(is? 'a use [a] ['a])
; word binding symmetry
(is? is? 'a use [a] ['a] is? use [a] ['a] 'a)

; word! vs. get-word!
('a isn't first [:a])
(quote :a is first [:a])
(is? is? 'a first [:a] is? first [:a] 'a)

; {word! vs. lit-word!
('a isn't first ['a])
(quote 'a is first ['a])
(is? is? 'a first ['a] is? first ['a] 'a)

; word! vs. refinement!
('a isn't /a)
(as refinement! 'a is /a)
(is? is? 'a /a is? /a 'a)

; word! vs. set-word!
('a isn't first [a:])
(to set-word! 'a is first [a:])
(is? is? 'a first [a:] is? first [a:] 'a)

; get-word! reflexivity
(first [:a] is first [:a])
(first [:a] isn't first ['a])
(first [:a] is to get-word! first ['a])
(is? is? first [:a] first ['a] is? first ['a] first [:a])

; get-word! vs. refinement!
(first [:a] isn't /a)
(as refinement! first [:a] is /a)
(is? is? first [:a] /a is? /a first [:a])

; get-word! vs. set-word!
(first [:a] isn't first [a:])
(to set-word! first [:a] is first [a:])
(is? is? first [:a] first [a:] is? first [a:] first [:a])

; lit-word! reflexivity
(first ['a] is first ['a])
(first ['a] isn't /a)
(first ['a] is to lit-word! /a)
(is? is? first ['a] /a is? /a first ['a])

; lit-word! vs. set-word!
(first ['a] isn't first [a:])
(to set-word! first ['a] is first [a:])
(is? is? first ['a] first [a:] is? first [a:] first ['a])

; refinement! reflexivity
(is? /a /a)
(/a isn't first [a:])
(/a is to refinement! first [a:])
(is? is? /a first [a:] is? first [a:] /a)

; set-word! reflexivity
(is? first [a:] first [a:])
(is? true true)
(is? false false)
(not is? true false)
(not is? false true)

; object! reflexivity
(is? a-value: make object! [a: 1] a-value)
; object! simple structural equivalence
(is? make object! [a: 1] make object! [a: 1])
; object! different values
(not is? make object! [a: 1] make object! [a: 2])
; object! different words
(not is? make object! [a: 1] make object! [b: 1])
(not is? make object! [a: 1] make object! [])

; object! complex structural equivalence
; Slight differences.
; Structural equality requires equality of the object's fields.
;
[#1133 (
    a-value: has/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: has/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    is? a-value b-value
)(
    a-value: has/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: has/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :is?
    is?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)(
    a-value: has/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: has/only [
        a: 1.0 b: $1 c: 100% d: 0.01
        e: [/a a 'a :a a: #"A" #[binary! [#{0000} 2]]]
        f: [#a <A> http://A a@A.com "A"]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :is?
    is?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)]

; VOID is legal to test with equality (as is UNSET! in R3-Alpha/Red)
[
    (is? void void)
    (isn't? void blank)
    (isn't? blank void)
    (is? (is? blank void) (is? void blank))
    (not (void == blank))
    (void !== blank)
    (not (blank == void))
    (blank !== void)
    (void == void)
    (not (void !== void))
    (is? (blank == void) (void == blank))
]

; NULL is legal to test with equality (as is UNSET! in R3-Alpha/Red)
[
    (is? null null)
    (isn't? null blank)
    (isn't? blank null)
    (is? (is? blank null) (is? null blank))
    (not (null == blank))
    (null !== blank)
    (not (blank == null))
    (blank !== null)
    (null == null)
    (not (null !== null))
    (is? (blank == null) (null == blank))
]


; error! reflexivity
; Evaluates (trap [1 / 0]) to get error! value.
(
    a-value: blank
    set 'a-value (trap [1 / 0])
    is? a-value a-value
)
; error! structural equivalence
; Evaluates (trap [1 / 0]) to get error! value.
(is? (trap [1 / 0]) (trap [1 / 0]))
; error! structural equivalence
(is? (make error! "hello") (make error! "hello"))
; error! difference in code
(not is? (trap [1 / 0]) (make error! "hello"))
; error! difference in data
(not is? (make error! "hello") (make error! "there"))
; error! basic comparison
(not is? (trap [1 / 0]) blank)
; error! basic comparison
(not is? blank (trap [1 / 0]))
; error! basic comparison symmetry
(is? is? (trap [1 / 0]) blank is? blank (trap [1 / 0]))
; error! basic comparison with = op
(not ((trap [1 / 0]) == blank))
; error! basic comparison with != op
((trap [1 / 0]) !== blank)
; error! basic comparison with = op
(not (blank == (trap [1 / 0])))
; error! basic comparison with != op
(blank !== (trap [1 / 0]))
; error! symmetry with == op
(is? not ((trap [1 / 0]) == blank) not (blank == (trap [1 / 0])))
; error! symmetry with != op
(is? (trap [1 / 0]) !== blank blank !== (trap [1 / 0]))
; port! reflexivity
; Error in R2 (could be fixed).
(is? p: make port! http:// p)
; No structural equivalence for port!
; Error in R2 (could be fixed).
(not is? make port! http:// make port! http://)
[#859 (
    a: copy quote ()
    insert/only a a
    error? trap [do a]
)]
