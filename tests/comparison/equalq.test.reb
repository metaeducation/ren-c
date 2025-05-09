; functions/comparison/equalq.r
; reflexivity test for native
(lax-equal? :abs :abs)
(not lax-equal? :abs :add)
(lax-equal? :all :all)
(not lax-equal? :all :any)
; reflexivity test for infix
(lax-equal? :+ :+)
(not lax-equal? :+ :-)
; reflexivity test for action!
(lax-equal? a-value: func [] [] :a-value)
; No structural equivalence for action!
(not lax-equal? func [] [] func [] [])
(lax-equal? a-value: #{00} a-value)
; binary!
; Same contents
(lax-equal? #{00} #{00})
; Different contents
(not lax-equal? #{00} #{01})
; Offset + similar contents at reference
(lax-equal? #{00} #[binary! [#{0000} 2]])
; Offset + similar contents at reference
(lax-equal? #{00} #[binary! [#{0100} 2]])
(lax-equal? lax-equal? #{00} #[binary! [#{0100} 2]] lax-equal? #[binary! [#{0100} 2]] #{00})
; No binary! padding
(not lax-equal? #{00} #{0000})
(lax-equal? lax-equal? #{00} #{0000} lax-equal? #{0000} #{00})
; Empty binary! not blank
(not lax-equal? #{} blank)
(lax-equal? lax-equal? #{} blank lax-equal? blank #{})
; case sensitivity
[#1459
    (lax-not-equal? #{0141} #{0161})
]
; email! vs. text!
; RAMBO #3518
(
    a-value: to email! ""
    lax-equal? a-value to text! a-value
)
; email! vs. text! symmetry
(
    a-value: to email! ""
    lax-equal? lax-equal? to text! a-value a-value lax-equal? a-value to text! a-value
)
; file! vs. text!
; RAMBO #3518
(
    a-value: %""
    lax-equal? a-value to text! a-value
)
; file! vs. text! symmetry
(
    a-value: %""
    lax-equal? lax-equal? a-value to text! a-value lax-equal? to text! a-value a-value
)

; No implicit to binary! from integer!
(not lax-equal? #{00} to integer! #{00})
(lax-equal? lax-equal? #{00} to integer! #{00} lax-equal? to integer! #{00} #{00})
; issue! vs. text!
; RAMBO #3518
(lax-not-equal? a-value: #a to text! a-value)
(
    a-value: #a
    lax-equal? lax-equal? a-value to text! a-value lax-equal? to text! a-value a-value
)
; No implicit to binary! from text!
(not lax-equal? a-value: "" to binary! a-value)
(
    a-value: ""
    lax-equal? lax-equal? a-value to binary! a-value lax-equal? to binary! a-value a-value
)
; tag! vs. text!
; RAMBO #3518
(lax-equal? a-value: to tag! "" to text! a-value)
(
    a-value: to tag! ""
    lax-equal? lax-equal? a-value to text! a-value lax-equal? to text! a-value a-value
)
(lax-equal? 0.0.0 0.0.0)
(not lax-equal? 0.0.1 0.0.0)
; tuple! right-pads with 0
(lax-equal? 1.0.0 1.0.0.0.0.0.0)
; tuple! right-pads with 0
(lax-equal? 1.0.0.0.0.0.0 1.0.0)
; No implicit to binary! from tuple!
(
    a-value: 0.0.0.0
    not lax-equal? to binary! a-value a-value
)
(
    a-value: 0.0.0.0
    lax-equal? lax-equal? to binary! a-value a-value lax-equal? a-value to binary! a-value
)
(lax-equal? #[bitset! #{00}] #[bitset! #{00}])
; bitset! with no bits set does not equal empty bitset
; This is because of the COMPLEMENT problem: bug#1085.
(not lax-equal? #[bitset! #{}] #[bitset! #{00}])
; No implicit to binary! from bitset!
(not lax-equal? #{00} #[bitset! #{00}])
(lax-equal? lax-equal? #[bitset! #{00}] #{00} lax-equal? #{00} #[bitset! #{00}])
(lax-equal? [] [])
(lax-equal? a-value: [] a-value)
; Reflexivity for past-tail blocks
; Error in R2.
(
    a-value: tail of [1]
    clear head of a-value
    lax-equal? a-value a-value
)
; Reflexivity for cyclic blocks
(
    a-value: copy []
    insert/only a-value a-value
    lax-equal? a-value a-value
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
    error? sys/util/rescue [lax-equal? a-value b-value]
    okay
)]
(not lax-equal? [] blank)
(lax-equal? lax-equal? [] blank lax-equal? blank [])
; block! vs. group!
(not lax-equal? [] first [()])
; block! vs. group! symmetry
(lax-equal? lax-equal? [] first [()] lax-equal? first [()] [])
; block! vs. path!
(not lax-equal? [a b] 'a/b)
; block! vs. path! symmetry
(
    a-value: 'a/b
    b-value: [a b]
    lax-equal? lax-equal? :a-value :b-value lax-equal? :b-value :a-value
)
; block! vs. lit-path!
(not lax-equal? [a b] first ['a/b])
; block! vs. lit-path! symmetry
(
    a-value: first ['a/b]
    b-value: [a b]
    lax-equal? lax-equal? :a-value :b-value lax-equal? :b-value :a-value
)
; block! vs. set-path!
(not lax-equal? [a b] first [a/b:])
; block! vs. set-path! symmetry
(
    a-value: first [a/b:]
    b-value: [a b]
    lax-equal? lax-equal? :a-value :b-value lax-equal? :b-value :a-value
)
; block! vs. get-path!
(not lax-equal? [a b] first [:a/b])
; block! vs. get-path! symmetry
(
    a-value: first [:a/b]
    b-value: [a b]
    lax-equal? lax-equal? :a-value :b-value lax-equal? :b-value :a-value
)
(lax-equal? decimal! decimal!)
(not lax-equal? decimal! integer!)
(lax-equal? lax-equal? decimal! integer! lax-equal? integer! decimal!)
; datatype! vs. typeset!
(not lax-equal? any-number! integer!)
; datatype! vs. typeset! symmetry
(lax-equal? lax-equal? any-number! integer! lax-equal? integer! any-number!)
; datatype! vs. typeset!
(not lax-equal? integer! make typeset! [integer!])
; datatype! vs. typeset!
(not lax-equal? integer! to typeset! [integer!])
; datatype! vs. typeset!
; Supported by R2/Forward.
(not lax-equal? integer! to-typeset [integer!])
; typeset! (or pseudo-type in R2)
(lax-equal? any-number! any-number!)
; typeset! (or pseudo-type in R2)
(not lax-equal? any-number! any-series!)
(lax-equal? make typeset! [integer!] make typeset! [integer!])
(lax-equal? to typeset! [integer!] to typeset! [integer!])
; Supported by R2/Forward.
(lax-equal? to-typeset [integer!] to-typeset [integer!])
(lax-equal? -1 -1)
(lax-equal? 0 0)
(lax-equal? 1 1)
(lax-equal? 0.0 0.0)
(lax-equal? 0.0 -0.0)
(lax-equal? 1.0 1.0)
(lax-equal? -1.0 -1.0)
<64bit>
(lax-equal? -9223372036854775808 -9223372036854775808)
<64bit>
(lax-equal? -9223372036854775807 -9223372036854775807)
<64bit>
(lax-equal? 9223372036854775807 9223372036854775807)
<64bit>
(not lax-equal? -9223372036854775808 -9223372036854775807)
<64bit>
(not lax-equal? -9223372036854775808 -1)
<64bit>
(not lax-equal? -9223372036854775808 0)
<64bit>
(not lax-equal? -9223372036854775808 1)
<64bit>
(not lax-equal? -9223372036854775808 9223372036854775806)
<64bit>
(not lax-equal? -9223372036854775807 -9223372036854775808)
<64bit>
(not lax-equal? -9223372036854775807 -1)
<64bit>
(not lax-equal? -9223372036854775807 0)
<64bit>
(not lax-equal? -9223372036854775807 1)
<64bit>
(not lax-equal? -9223372036854775807 9223372036854775806)
<64bit>
(not lax-equal? -9223372036854775807 9223372036854775807)
<64bit>
(not lax-equal? -1 -9223372036854775808)
<64bit>
(not lax-equal? -1 -9223372036854775807)
(not lax-equal? -1 0)
(not lax-equal? -1 1)
<64bit>
(not lax-equal? -1 9223372036854775806)
<64bit>
(not lax-equal? -1 9223372036854775807)
<64bit>
(not lax-equal? 0 -9223372036854775808)
<64bit>
(not lax-equal? 0 -9223372036854775807)
(not lax-equal? 0 -1)
(not lax-equal? 0 1)
<64bit>
(not lax-equal? 0 9223372036854775806)
<64bit>
(not lax-equal? 0 9223372036854775807)
<64bit>
(not lax-equal? 1 -9223372036854775808)
<64bit>
(not lax-equal? 1 -9223372036854775807)
(not lax-equal? 1 -1)
(not lax-equal? 1 0)
<64bit>
(not lax-equal? 1 9223372036854775806)
<64bit>
(not lax-equal? 1 9223372036854775807)
<64bit>
(not lax-equal? 9223372036854775806 -9223372036854775808)
<64bit>
(not lax-equal? 9223372036854775806 -9223372036854775807)
<64bit>
(not lax-equal? 9223372036854775806 -1)
<64bit>
(not lax-equal? 9223372036854775806 0)
<64bit>
(not lax-equal? 9223372036854775806 1)
<64bit>
(not lax-equal? 9223372036854775806 9223372036854775807)
<64bit>
(not lax-equal? 9223372036854775807 -9223372036854775808)
<64bit>
(not lax-equal? 9223372036854775807 -9223372036854775807)
<64bit>
(not lax-equal? 9223372036854775807 -1)
<64bit>
(not lax-equal? 9223372036854775807 0)
<64bit>
(not lax-equal? 9223372036854775807 1)
<64bit>
(not lax-equal? 9223372036854775807 9223372036854775806)
; decimal! approximate equality
(lax-equal? 0.3 0.1 + 0.1 + 0.1)
; decimal! approximate equality symmetry
(lax-equal? lax-equal? 0.3 0.1 + 0.1 + 0.1 lax-equal? 0.1 + 0.1 + 0.1 0.3)
(lax-equal? 0.15 - 0.05 0.1)
(lax-equal? lax-equal? 0.15 - 0.05 0.1 lax-equal? 0.1 0.15 - 0.05)
(lax-equal? -0.5 cosine 120)
(lax-equal? lax-equal? -0.5 cosine 120 lax-equal? cosine 120 -0.5)
(lax-equal? 0.5 * square-root 2.0 sine 45)
(lax-equal? lax-equal? 0.5 * square-root 2.0 sine 45 lax-equal? sine 45 0.5 * square-root 2.0)
(lax-equal? 0.5 sine 30)
(lax-equal? lax-equal? 0.5 sine 30 lax-equal? sine 30 0.5)
(lax-equal? 0.5 cosine 60)
(lax-equal? lax-equal? 0.5 cosine 60 lax-equal? cosine 60 0.5)
(lax-equal? 0.5 * square-root 3.0 sine 60)
(lax-equal? lax-equal? 0.5 * square-root 3.0 sine 60 lax-equal? sine 60 0.5 * square-root 3.0)
(lax-equal? 0.5 * square-root 3.0 cosine 30)
(lax-equal? lax-equal? 0.5 * square-root 3.0 cosine 30 lax-equal? cosine 30 0.5 * square-root 3.0)
(lax-equal? square-root 3.0 tangent 60)
(lax-equal? lax-equal? square-root 3.0 tangent 60 lax-equal? tangent 60 square-root 3.0)
(lax-equal? (square-root 3.0) / 3.0 tangent 30)
(lax-equal? lax-equal? (square-root 3.0) / 3.0 tangent 30 lax-equal? tangent 30 (square-root 3.0) / 3.0)
(lax-equal? 1.0 tangent 45)
(lax-equal? lax-equal? 1.0 tangent 45 lax-equal? tangent 45 1.0)
(
    num: square-root 2.0
    lax-equal? 2.0 num * num
)
(
    num: square-root 2.0
    lax-equal? lax-equal? 2.0 num * num lax-equal? num * num 2.0
)
(
    num: square-root 3.0
    lax-equal? 3.0 num * num
)
(
    num: square-root 3.0
    lax-equal? lax-equal? 3.0 num * num lax-equal? num * num 3.0
)
; integer! vs. decimal!
(lax-equal? 0 0.0)
; integer! vs. percent!
(lax-equal? 0 0%)

; decimal! vs. percent!
(lax-equal? 0.0 0%)

; integer! vs. decimal! symmetry
(lax-equal? lax-equal? 1 1.0 lax-equal? 1.0 1)

; integer! vs. percent! symmetry
(lax-equal? lax-equal? 1 100% lax-equal? 100% 1)

; decimal! vs. percent! symmetry
(lax-equal? lax-equal? 1.0 100% lax-equal? 100% 1.0)

; percent! approximate equality
(lax-equal? 10% + 10% + 10% 30%)
; percent! approximate equality symmetry
(lax-equal? lax-equal? 10% + 10% + 10% 30% lax-equal? 30% 10% + 10% + 10%)
(lax-equal? 2-Jul-2009 2-Jul-2009)
; date! doesn't ignore time portion
(not lax-equal? 2-Jul-2009 2-Jul-2009/22:20)
(lax-equal? lax-equal? 2-Jul-2009 2-Jul-2009/22:20 lax-equal? 2-Jul-2009/22:20 2-Jul-2009)

; R3-Alpha considered date! missing time and zone = 00:00:00+00:00.  But
; in Ren-C, dates without a time are semantically distinct from a date with
; a time at midnight.
;
(not lax-equal? 2-Jul-2009 2-Jul-2009/00:00:00+00:00)

(lax-equal? lax-equal? 2-Jul-2009 2-Jul-2009/00:00 lax-equal? 2-Jul-2009/00:00 2-Jul-2009)
; Timezone math in date!
(lax-equal? 2-Jul-2009/22:20 2-Jul-2009/20:20-2:00)
(lax-equal? 00:00 00:00)
; time! missing components are 0
(lax-equal? 0:0 00:00:00.0000000000)
(lax-equal? lax-equal? 0:0 00:00:00.0000000000 lax-equal? 00:00:00.0000000000 0:0)
; time! vs. integer!
[#1103
    (not lax-equal? 0:00 0)
]
; integer! vs. time!
[#1103
    (not lax-equal? 0 00:00)
]
(lax-equal? #"a" #"a")
; char! vs. integer!
; No implicit to char! from integer! in R3.
(not lax-equal? #"a" 97)
; char! vs. integer! symmetry
(lax-equal? lax-equal? #"a" 97 lax-equal? 97 #"a")
; char! vs. decimal!
; No implicit to char! from decimal! in R3.
(not lax-equal? #"a" 97.0)
; char! vs. decimal! symmetry
(lax-equal? lax-equal? #"a" 97.0 lax-equal? 97.0 #"a")
; char! case
(lax-equal? #"a" #"A")
; text! case
(lax-equal? "a" "A")
; issue! case
(lax-equal? #a #A)
; tag! case
(lax-equal? <a a="a"> <A A="A">)
; url! case
(lax-equal? http://a.com httP://A.coM)
; email! case
(lax-equal? a@a.com A@A.Com)
(lax-equal? 'a 'a)
(lax-equal? 'a 'A)
(lax-equal? lax-equal? 'a 'A lax-equal? 'A 'a)
; word binding
(lax-equal? 'a use [a] ['a])
; word binding symmetry
(lax-equal? lax-equal? 'a use [a] ['a] lax-equal? use [a] ['a] 'a)
; word! vs. get-word!
(lax-equal? 'a first [:a])
; word! vs. get-word! symmetry
(lax-equal? lax-equal? 'a first [:a] lax-equal? first [:a] 'a)
; {word! vs. lit-word!
(lax-equal? 'a first ['a])
; word! vs. lit-word! symmetry
(lax-equal? lax-equal? 'a first ['a] lax-equal? first ['a] 'a)
; word! vs. refinement!
(lax-equal? 'a /a)
; word! vs. refinement! symmetry
(lax-equal? lax-equal? 'a /a lax-equal? /a 'a)
; word! vs. set-word!
(lax-equal? 'a first [a:])
; word! vs. set-word! symmetry
(lax-equal? lax-equal? 'a first [a:] lax-equal? first [a:] 'a)
; get-word! reflexivity
(lax-equal? first [:a] first [:a])
; get-word! vs. lit-word!
(lax-equal? first [:a] first ['a])
; get-word! vs. lit-word! symmetry
(lax-equal? lax-equal? first [:a] first ['a] lax-equal? first ['a] first [:a])
; get-word! vs. refinement!
(lax-equal? first [:a] /a)
; get-word! vs. refinement! symmetry
(lax-equal? lax-equal? first [:a] /a lax-equal? /a first [:a])
; get-word! vs. set-word!
(lax-equal? first [:a] first [a:])
; get-word! vs. set-word! symmetry
(lax-equal? lax-equal? first [:a] first [a:] lax-equal? first [a:] first [:a])
; lit-word! reflexivity
(lax-equal? first ['a] first ['a])
; lit-word! vs. refinement!
(lax-equal? first ['a] /a)
; lit-word! vs. refinement! symmetry
(lax-equal? lax-equal? first ['a] /a lax-equal? /a first ['a])
; lit-word! vs. set-word!
(lax-equal? first ['a] first [a:])
; lit-word! vs. set-word! symmetry
(lax-equal? lax-equal? first ['a] first [a:] lax-equal? first [a:] first ['a])
; refinement! reflexivity
(lax-equal? /a /a)
; refinement! vs. set-word!
(lax-equal? /a first [a:])
; refinement! vs. set-word! symmetry
(lax-equal? lax-equal? /a first [a:] lax-equal? first [a:] /a)
; set-word! reflexivity
(lax-equal? first [a:] first [a:])
(lax-equal? okay okay)
(lax-equal? null null)
(not lax-equal? okay null)
(not lax-equal? null okay)
; object! reflexivity
(lax-equal? a-value: make object! [a: 1] a-value)
; object! simple structural equivalence
(lax-equal? make object! [a: 1] make object! [a: 1])
; object! different values
(not lax-equal? make object! [a: 1] make object! [a: 2])
; object! different words
(not lax-equal? make object! [a: 1] make object! [b: 1])
(not lax-equal? make object! [a: 1] make object! [])

; object! complex structural equivalence
; Slight differences.
; Structural equality requires equality of the object's fields.
;
[#1133 (
    a-value: construct/only [] [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [] [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    lax-equal? a-value b-value
)(
    a-value: construct/only [] [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [] [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :lax-equal?
    lax-equal?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)(
    a-value: construct/only [] [
        a: 1 b: 1.0 c: $1 d: 1%
        e: [a 'a :a a: /a #"a" #{00}]
        f: ["a" #a http://a a@a.com <a>]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    b-value: construct/only [] [
        a: 1.0 b: $1 c: 100% d: 0.01
        e: [/a a 'a :a a: #"A" #[binary! [#{0000} 2]]]
        f: [#a <A> http://A a@A.com "A"]
        g: :a/b/(c: 'd/e/f)/(b/d: [:f/g h/i])
    ]
    test: :lax-equal?
    lax-equal?
        test a-value b-value
        not null? for-each [w v] a-value [
            if not test :v select b-value w [break]
        ]
)]

; TRASH is illegal to test with equality (as is UNSET! in Rebol2)
[
    ('expect-arg = (sys.util/rescue [lax-equal? ~ ~]).id)
    ('expect-arg = (sys.util/rescue [lax-not-equal? ~ blank]).id)
    ('expect-arg = (sys.util/rescue [~ <> blank]).id)
]

; NULL is legal to test with equality (as is NONE! in R3-Alpha/Red)
[
    (lax-equal? null null)
    (lax-not-equal? null blank)
    (lax-not-equal? blank null)
    (lax-equal? (lax-equal? blank null) (lax-equal? null blank))
    (not (null = blank))
    (null <> blank)
    (not (blank = null))
    (blank != null)
    (null = null)
    (not (null != null))
    (lax-equal? (blank = null) (null = blank))
]


; port! reflexivity
; Error in R2 (could be fixed).
(lax-equal? p: make port! http:// p)
; No structural equivalence for port!
; Error in R2 (could be fixed).
(not lax-equal? make port! http:// make port! http://)
[#859 (
    a: copy the ()
    insert/only a a
    error? sys/util/rescue [eval a]
)]
