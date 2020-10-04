; functions/comparison/equalq.r
; reflexivity test for native
(equal? :abs :abs)
(not equal? :abs :add)
(equal? :all :all)
(not equal? :all :any)
; reflexivity test for infix
(equal? :+ :+)
(not equal? :+ :-)
; reflexivity test for action!
(equal? a-value: func [] [] :a-value)
; No structural equivalence for action!
(not equal? func [] [] func [] [])
(equal? a-value: #{00} a-value)
; binary!
; Same contents
(equal? #{00} #{00})
; Different contents
(not equal? #{00} #{01})
; Offset + similar contents at reference
(equal? #{00} #[binary! [#{0000} 2]])
; Offset + similar contents at reference
(equal? #{00} #[binary! [#{0100} 2]])
(equal? equal? #{00} #[binary! [#{0100} 2]] equal? #[binary! [#{0100} 2]] #{00})
; No binary! padding
(not equal? #{00} #{0000})
(equal? equal? #{00} #{0000} equal? #{0000} #{00})
; Empty binary! not blank
(not equal? #{} blank)
(equal? equal? #{} blank equal? blank #{})
; case sensitivity
[#1459
    (not-equal? #{0141} #{0161})
]
; email! vs. text!
; RAMBO #3518
(
    a-value: to email! ""
    equal? a-value to text! a-value
)
; email! vs. text! symmetry
(
    a-value: to email! ""
    equal? equal? to text! a-value a-value equal? a-value to text! a-value
)
; file! vs. text!
; RAMBO #3518
(
    a-value: %""
    equal? a-value to text! a-value
)
; file! vs. text! symmetry
(
    a-value: %""
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)
; image! same contents
(equal? a-value: #[image! [1x1 #{000000FF}]] a-value)
(equal? #[image! [1x1 #{000000FF}]] #[image! [1x1 #{000000FF}]])

; Literal offset not supported in R2.
(equal? #[image! [1x1 #{000000FF} 2]] #[image! [1x1 #{000000FF} 2]])
; Literal offset not supported in R2.
(not equal? #[image! [1x1 #{000000FF} 2]] #[image! [1x1 #{000000FF}]])

; !!! The IMAGE! data type is being moved to an extension, but NEXT is a
; specialization of SKIP and doesn't go through the GENERIC mechanism.  This
; can be revisited and addressed a number of ways, but IMAGE! is not a
; Beta/One feature.  However loading/encoding/decoding are kept working.
;

[(true comment [
    (
        a-value: #[image! [1x1 #{000000FF}]]
        not equal? a-value next a-value
    )
    (equal? #[image! [0x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (equal? #[image! [1x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (equal? #[image! [0x1 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [0x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [1x0 #{}]] next #[image! [1x1 #{000000FF}]])
    (not equal? #[image! [0x1 #{}]] next #[image! [1x1 #{000000FF}]])
])]

; No implicit to binary! from image!
(not equal? #{00} #[image! [1x1 #{000000FF}]])
; No implicit to binary! from image!
(not equal? #{00000000} #[image! [1x1 #{000000FF}]])
; No implicit to binary! from image!
(not equal? #{0000000000} #[image! [1x1 #{000000FF}]])
(equal? equal? #{00} #[image! [1x1 #{00000000}]] equal? #[image! [1x1 #{00000000}]] #{00})
; No implicit to binary! from integer!
(not equal? #{00} to integer! #{00})
(equal? equal? #{00} to integer! #{00} equal? to integer! #{00} #{00})
; issue! vs. text!
; RAMBO #3518
(equal? a-value: #a to text! a-value)
(
    a-value: #a
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)
; No implicit to binary! from text!
(not equal? a-value: "" to binary! a-value)
(
    a-value: ""
    equal? equal? a-value to binary! a-value equal? to binary! a-value a-value
)
; tag! vs. text!
; RAMBO #3518
(equal? a-value: to tag! "" to text! a-value)
(
    a-value: to tag! ""
    equal? equal? a-value to text! a-value equal? to text! a-value a-value
)
(equal? #[bitset! #{00}] #[bitset! #{00}])
; bitset! with no bits set does not equal empty bitset
; This is because of the COMPLEMENT problem: bug#1085.
(not equal? #[bitset! #{}] #[bitset! #{00}])
; No implicit to binary! from bitset!
(not equal? #{00} #[bitset! #{00}])
(equal? equal? #[bitset! #{00}] #{00} equal? #{00} #[bitset! #{00}])
(equal? [] [])
(equal? a-value: [] a-value)
; Reflexivity for past-tail blocks
; Error in R2.
(
    a-value: tail of [1]
    clear head of a-value
    equal? a-value a-value
)
; Reflexivity for cyclic blocks
(
    a-value: copy []
    insert/only a-value a-value
    equal? a-value a-value
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
    error? trap [equal? a-value b-value]
    true
)]
(not equal? [] blank)
(equal? equal? [] blank equal? blank [])
; block! vs. group!
(not equal? [] first [()])
; block! vs. group! symmetry
(equal? equal? [] first [()] equal? first [()] [])
; block! vs. path!
(not equal? [a b] 'a/b)
; block! vs. path! symmetry
(
    a-value: 'a/b
    b-value: [a b]
    equal? equal? :a-value :b-value equal? :b-value :a-value
)
; block! vs. lit-path!
(not equal? [a b] first ['a/b])
; block! vs. lit-path! symmetry
(
    a-value: first ['a/b]
    b-value: [a b]
    equal? equal? :a-value :b-value equal? :b-value :a-value
)
; block! vs. set-path!
(not equal? [a b] first [a/b:])
; block! vs. set-path! symmetry
(
    a-value: first [a/b:]
    b-value: [a b]
    equal? equal? :a-value :b-value equal? :b-value :a-value
)
; block! vs. get-path!
(not equal? [a b] first [:a/b])
; block! vs. get-path! symmetry
(
    a-value: first [:a/b]
    b-value: [a b]
    equal? equal? :a-value :b-value equal? :b-value :a-value
)
(equal? decimal! decimal!)
(not equal? decimal! integer!)
(equal? equal? decimal! integer! equal? integer! decimal!)
; datatype! vs. typeset!
(not equal? any-number! integer!)
; datatype! vs. typeset! symmetry
(equal? equal? any-number! integer! equal? integer! any-number!)
; datatype! vs. typeset!
(not equal? integer! make typeset! [integer!])
; datatype! vs. typeset!
(not equal? integer! to typeset! [integer!])
; datatype! vs. typeset!
; Supported by R2/Forward.
(not equal? integer! to-typeset [integer!])
; typeset! (or pseudo-type in R2)
(equal? any-number! any-number!)
; typeset! (or pseudo-type in R2)
(not equal? any-number! any-series!)
(equal? make typeset! [integer!] make typeset! [integer!])
(equal? to typeset! [integer!] to typeset! [integer!])
; Supported by R2/Forward.
(equal? to-typeset [integer!] to-typeset [integer!])
(equal? -1 -1)
(equal? 0 0)
(equal? 1 1)
(equal? 0.0 0.0)
(equal? 0.0 -0.0)
(equal? 1.0 1.0)
(equal? -1.0 -1.0)
<64bit>
(equal? -9223372036854775808 -9223372036854775808)
<64bit>
(equal? -9223372036854775807 -9223372036854775807)
<64bit>
(equal? 9223372036854775807 9223372036854775807)
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
; decimal! approximate equality
(equal? 0.3 0.1 + 0.1 + 0.1)
; decimal! approximate equality symmetry
(equal? equal? 0.3 0.1 + 0.1 + 0.1 equal? 0.1 + 0.1 + 0.1 0.3)
(equal? 0.15 - 0.05 0.1)
(equal? equal? 0.15 - 0.05 0.1 equal? 0.1 0.15 - 0.05)
(equal? -0.5 cosine 120)
(equal? equal? -0.5 cosine 120 equal? cosine 120 -0.5)
(equal? 0.5 * square-root 2.0 sine 45)
(equal? equal? 0.5 * square-root 2.0 sine 45 equal? sine 45 0.5 * square-root 2.0)
(equal? 0.5 sine 30)
(equal? equal? 0.5 sine 30 equal? sine 30 0.5)
(equal? 0.5 cosine 60)
(equal? equal? 0.5 cosine 60 equal? cosine 60 0.5)
(equal? 0.5 * square-root 3.0 sine 60)
(equal? equal? 0.5 * square-root 3.0 sine 60 equal? sine 60 0.5 * square-root 3.0)
(equal? 0.5 * square-root 3.0 cosine 30)
(equal? equal? 0.5 * square-root 3.0 cosine 30 equal? cosine 30 0.5 * square-root 3.0)
(equal? square-root 3.0 tangent 60)
(equal? equal? square-root 3.0 tangent 60 equal? tangent 60 square-root 3.0)
(equal? (square-root 3.0) / 3.0 tangent 30)
(equal? equal? (square-root 3.0) / 3.0 tangent 30 equal? tangent 30 (square-root 3.0) / 3.0)
(equal? 1.0 tangent 45)
(equal? equal? 1.0 tangent 45 equal? tangent 45 1.0)
(
    num: square-root 2.0
    equal? 2.0 num * num
)
(
    num: square-root 2.0
    equal? equal? 2.0 num * num equal? num * num 2.0
)
(
    num: square-root 3.0
    equal? 3.0 num * num
)
(
    num: square-root 3.0
    equal? equal? 3.0 num * num equal? num * num 3.0
)
; integer! vs. decimal!
(equal? 0 0.0)
; integer! vs. money!
(equal? 0 $0)
; integer! vs. percent!
(equal? 0 0%)
; decimal! vs. money!
(equal? 0.0 $0)
; decimal! vs. percent!
(equal? 0.0 0%)
; money! vs. percent!
(equal? $0 0%)
; integer! vs. decimal! symmetry
(equal? equal? 1 1.0 equal? 1.0 1)
; integer! vs. money! symmetry
(equal? equal? 1 $1 equal? $1 1)
; integer! vs. percent! symmetry
(equal? equal? 1 100% equal? 100% 1)
; decimal! vs. money! symmetry
(equal? equal? 1.0 $1 equal? $1 1.0)
; decimal! vs. percent! symmetry
(equal? equal? 1.0 100% equal? 100% 1.0)
; money! vs. percent! symmetry
(equal? equal? $1 100% equal? 100% $1)
; percent! approximate equality
(equal? 10% + 10% + 10% 30%)
; percent! approximate equality symmetry
(equal? equal? 10% + 10% + 10% 30% equal? 30% 10% + 10% + 10%)
(equal? 2-Jul-2009 2-Jul-2009)
; date! doesn't ignore time portion
(not equal? 2-Jul-2009 2-Jul-2009/22:20)
(equal? equal? 2-Jul-2009 2-Jul-2009/22:20 equal? 2-Jul-2009/22:20 2-Jul-2009)

; R3-Alpha considered date! missing time and zone = 00:00:00+00:00.  But
; in Ren-C, dates without a time are semantically distinct from a date with
; a time at midnight.
;
(not equal? 2-Jul-2009 2-Jul-2009/00:00:00+00:00)

(equal? equal? 2-Jul-2009 2-Jul-2009/00:00 equal? 2-Jul-2009/00:00 2-Jul-2009)
; Timezone math in date!
(equal? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00)
(equal? 00:00 00:00)
; time! missing components are 0
(equal? 0:0 00:00:00.0000000000)
(equal? equal? 0:0 00:00:00.0000000000 equal? 00:00:00.0000000000 0:0)
; time! vs. integer!
[#1103
    (not equal? 0:00 0)
]
; integer! vs. time!
[#1103
    (not equal? 0 00:00)
]
(equal? #"a" #"a")
; char! vs. integer!
; No implicit to char! from integer! in R3.
(not equal? #"a" 97)
; char! vs. integer! symmetry
(equal? equal? #"a" 97 equal? 97 #"a")
; char! vs. decimal!
; No implicit to char! from decimal! in R3.
(not equal? #"a" 97.0)
; char! vs. decimal! symmetry
(equal? equal? #"a" 97.0 equal? 97.0 #"a")
; char! case
(equal? #"a" #"A")
; text! case
(equal? "a" "A")
; issue! case
(equal? #a #A)
; tag! case
(equal? <a a="a"> <A A="A">)
; url! case
(equal? http://a.com httP://A.coM)
; email! case
(equal? a@a.com A@A.Com)
(equal? 'a 'a)
(equal? 'a 'A)
(equal? equal? 'a 'A equal? 'A 'a)
; word binding
(equal? 'a use [a] ['a])
; word binding symmetry
(equal? equal? 'a use [a] ['a] equal? use [a] ['a] 'a)
; word! vs. get-word!
(equal? 'a first [:a])
; word! vs. get-word! symmetry
(equal? equal? 'a first [:a] equal? first [:a] 'a)
; {word! vs. lit-word!
(equal? 'a first ['a])
; word! vs. lit-word! symmetry
(equal? equal? 'a first ['a] equal? first ['a] 'a)
; word! vs. refinement! (changed in Ren-C)
(not equal? 'a /a)
(equal? 'a second /a)
; word! vs. refinement! symmetry
(equal? equal? 'a /a equal? /a 'a)
; word! vs. set-word!
(equal? 'a first [a:])
; word! vs. set-word! symmetry
(equal? equal? 'a first [a:] equal? first [a:] 'a)
; get-word! reflexivity
(equal? first [:a] first [:a])
; get-word! vs. lit-word!
(equal? first [:a] first ['a])
; get-word! vs. lit-word! symmetry
(equal? equal? first [:a] first ['a] equal? first ['a] first [:a])
; get-word! vs. refinement!
(not equal? first [:a] /a)
; get-word! vs. refinement! symmetry
(equal? equal? first [:a] /a equal? /a first [:a])
; get-word! vs. set-word!
(equal? first [:a] first [a:])
; get-word! vs. set-word! symmetry
(equal? equal? first [:a] first [a:] equal? first [a:] first [:a])
; lit-word! reflexivity
(equal? first ['a] first ['a])
; lit-word! vs. refinement!
(not equal? first ['a] /a)
; lit-word! vs. refinement! symmetry
(equal? equal? first ['a] /a equal? /a first ['a])
; lit-word! vs. set-word!
(equal? first ['a] first [a:])
; lit-word! vs. set-word! symmetry
(equal? equal? first ['a] first [a:] equal? first [a:] first ['a])
; refinement! reflexivity
(equal? /a /a)
; refinement! vs. set-word!
(not equal? /a first [a:])
; refinement! vs. set-word! symmetry
(equal? equal? /a first [a:] equal? first [a:] /a)
; set-word! reflexivity
(equal? first [a:] first [a:])
(equal? true true)
(equal? false false)
(not equal? true false)
(not equal? false true)
; object! reflexivity
(equal? a-value: make object! [a: 1] a-value)
; object! simple structural equivalence
(equal? make object! [a: 1] make object! [a: 1])
; object! different values
(not equal? make object! [a: 1] make object! [a: 2])
; object! different words
(not equal? make object! [a: 1] make object! [b: 1])
(not equal? make object! [a: 1] make object! [])

; object! complex structural equivalence
; Slight differences.
; Structural equality requires equality of the object's fields.
;
[#1133 (
    a-value: construct/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    equal? a-value b-value
)(
    a-value: construct/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :equal?
    equal?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)(
    a-value: construct/only [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [
        a: 1.0 b: $1 c: 100% d: 0.01
        e: [/a a 'a :a a: #"A" #[binary! [#{0000} 2]]]
        f: [#a <A> http://A a@A.com "A"]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :equal?
    equal?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)]

; VOID is legal to test with equality (as is UNSET! in R3-Alpha/Red)
[
    (equal? void void)
    (not-equal? void blank)
    (not-equal? blank void)
    (equal? (equal? blank void) (equal? void blank))
    (not (void = blank))
    (void <> blank)
    (not (blank = void))
    (blank != void)
    (void = void)
    (not (void != void))
    (equal? (blank = void) (void = blank))
]

; NULL is legal to test with equality (as is UNSET! in R3-Alpha/Red)
[
    (equal? null null)
    (not-equal? null blank)
    (not-equal? blank null)
    (equal? (equal? blank null) (equal? null blank))
    (not (null = blank))
    (null <> blank)
    (not (blank = null))
    (blank != null)
    (null = null)
    (not (null != null))
    (equal? (blank = null) (null = blank))
]


; error! reflexivity
; Evaluates (trap [1 / 0]) to get error! value.
(
    a-value: blank
    set 'a-value (trap [1 / 0])
    equal? a-value a-value
)
; error! structural equivalence
; Evaluates (trap [1 / 0]) to get error! value.
(equal? (trap [1 / 0]) (trap [1 / 0]))
; error! structural equivalence
(equal? (make error! "hello") (make error! "hello"))
; error! difference in code
(not equal? (trap [1 / 0]) (make error! "hello"))
; error! difference in data
(not equal? (make error! "hello") (make error! "there"))
; error! basic comparison
(not equal? (trap [1 / 0]) blank)
; error! basic comparison
(not equal? blank (trap [1 / 0]))
; error! basic comparison symmetry
(equal? equal? (trap [1 / 0]) blank equal? blank (trap [1 / 0]))
; error! basic comparison with = op
(not ((trap [1 / 0]) = blank))
; error! basic comparison with != op
((trap [1 / 0]) != blank)
; error! basic comparison with = op
(not (blank = (trap [1 / 0])))
; error! basic comparison with != op
(blank != (trap [1 / 0]))
; error! symmetry with = op
(equal? not ((trap [1 / 0]) = blank) not (blank = (trap [1 / 0])))
; error! symmetry with != op
(equal? (trap [1 / 0]) != blank blank != (trap [1 / 0]))
; port! reflexivity
; Error in R2 (could be fixed).
(equal? p: make port! http:// p)
; No structural equivalence for port!
; Error in R2 (could be fixed).
(not equal? make port! http:// make port! http://)
[#859 (
    a: copy lit ()
    insert/only a a
    error? trap [do a]
)]
