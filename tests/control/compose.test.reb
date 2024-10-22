; %compose.test.reb
;
; Ren-C's COMPOSE has many more features than historical Rebol.  These features
; range from having two types of slots: (single) and ((spliced)), to being
; able to put sigils or quotes on the spliced material.

; Splicing vs. non
;
([[a b] * a b] = compose [([a b]) * (spread [a b])])

; Preserve one element rule vs. tolerate vaporization.
;
([~null~ *] = compose [(reify null) * (maybe null)])
([~void~ *] = compose [(meta void) * (void)])

; Voids vaporize regardless of form.

([*] = compose [(comment "single") * ((comment "spliced"))])
([* <ok>] = compose [(void) * <ok>])
([<ok> *] = compose [<ok> * ((void))])


~bad-antiform~ !! (
    compose [(~<bad>~) * <ok>]
)
~need-non-null~ !! (
    compose [(null) * <ok>]
)
~need-non-null~ !! (
    compose [(~null~) * <ok>]
)
([~false~] = compose [('~false~)])

([_ * _] = compose [('_) * ('_)])
([a * 'a] = compose [(the a) * (the 'a)])
([1020 * 304] = compose [(1020) * ((304))])
([@ae * @ae] = compose [(@ae) * ((@ae))])

([(group) * <good>] = compose [(the (group)) * <good>])


(
    num: 1
    [1 num] = compose [(num) num]
)
([] = compose [])
(
    blk: []
    append blk [trap [1 / 0]]
    blk = compose blk
)
; RETURN stops the evaluation
(
    /f1: func [return: [integer!]] [compose [(return 1)] 2]
    1 = f1
)
; THROW stops the evaluation
(1 = catch [compose [(throw 1 2)] 2])
; BREAK stops the evaluation
(null? repeat 1 [compose [(break 2)] 2])
; Test that errors do not stop the evaluation:
(block? compose [(trap [1 / 0])])
(
    blk: []
    not same? blk compose blk
)
(
    blk: [[]]
    same? first blk first compose blk
)
(
    blk: []
    same? blk first compose [(spread reduce [blk])]
)
(
    blk: []
    same? blk first compose [(blk)]
)
; recursion
(
    num: 1
    [num 1] = compose [num (spread compose [(num)])]
)
; infinite recursion
(
    <deep-enough> = catch wrap [
        x: 0
        blk: [(x: x + 1, if x = 5000 [throw <deep-enough>]) (compose blk)]
        eval blk
    ]
)

; #1906
(
    b: copy [] insert:dup b 1 32768 compose b
    sum: 0
    for-each 'i b [sum: me + i]
    sum = 32768
)

; COMPOSE with implicit /ONLY-ing

(
    block: [a b c]
    [splice-me: a b c only: [a b c]] = compose [
        splice-me: (spread block)
        only: (block)
    ]
)

; COMPOSE with pattern, beginning tests

(
    [(1 + 2) 3] = compose:label [(1 + 2) (<*> 1 + 2)] <*>
)(
    [(1 + 2)] = compose:label [(1 + 2) (<*>)] <*>
)(
    'a/(b)/3/c = compose:label @ a/(b)/(<?> 1 + 2)/c <?>
)(
    [(a b c) [((d) 1 + 2)]] = compose:deep:label [
        (a (</> 'b) c) [((d) 1 + 2)]
    ] </>
)

(
    [(left alone) [c b a] c b a ((left alone))]
    = compose:label [
        (left alone)
        (<$> reverse copy [a b c])
        (<$> spread reverse copy [a b c])
        ((left alone))
    ] <$>
)


; While some proposals for COMPOSE handling of QUOTED! would knock one quote
; level off a group, protecting groups from composition is better done with
; labeled compose...saving it for quoting composed material.

([3 '3 ''3] == compose [(1 + 2) '(1 + 2) ''(1 + 2)])
~???~ !! (compose ['(if null [<cant-vanish-with-quote>])])

; Quoting should be preserved by deep composition

([a ''[b 3 c] d] == compose:deep [a ''[b (1 + 2) c] d])


; COMPOSE no longer tries to convert set-forms

([x:] = compose [('x):])
~bad-sequence-item~ !! ([x:] = compose [('x:):])
~bad-sequence-item~ !! ([x:] = compose [(':x):])

; Running code during SETIFY/GETIFY internally was dropped, because the
; scanner was using it...and it had PUSH()es extant.  The feature is still
; possible, but it's not clear it's a great idea.  Punt on letting you
; getify or setify things that aren't guaranteed to succeed (e.g. a string
; might have spaces in it, and can't be turned into a SET-WORD!)
;
([#x:] = compose [(#x):])
((reduce [to chain! ["x" _]]) = compose [("x"):])

; Can't put colons "on top" of paths
~bad-sequence-item~ !!  (compose [( 'x/y ):])

([(x y):] = compose [( '(x y) ):])
~bad-sequence-item~ !! (compose [( '(x y): ):])
~bad-sequence-item~ !! (compose [( ':(x y) ):])

([[x y]:] = compose [( '[x y] ):])
~bad-sequence-item~ !! (compose [( '[x y]: ):])
~bad-sequence-item~ !! (compose [( ':[x y] ):])


; Note: string conversions to unbound words were done at one point, but have
; been dropped, at least for the moment:
;
;    ([:x] = compose [:(#x)])
;    ([:x] = compose [:("x")])
;
; They may be worth considering for the future.

([:x] = compose [:('x)])
~bad-sequence-item~ !! (compose [:('x:)])
~bad-sequence-item~ !! (compose [:(':x)])

; Can't put colons on top of paths
~bad-sequence-item~ !! (compose [:( 'x/y )])

([:(x y)] = compose [:( '(x y) )])
~bad-sequence-item~ !! (compose [:( '(x y): )])
~bad-sequence-item~ !! (compose [:( ':(x y) )])

([:[x y]] = compose [:( '[x y] )])
~bad-sequence-item~ !! (compose [:( '[x y]: )])
~bad-sequence-item~ !! (compose [:( ':[x y] )])

; !!! This was an interesting concept, but now that REFINEMENT and PATH! are
; unified it can't be done with PATH!, as you might say `compose obj/block`
; and mean that.  The notation for predicates have to be rethought.
;
; ([a b c d e f] = compose /identity [([a b c]) (([d e f]))])
; ([[a b c] d e f] = compose /enblock [([a b c]) (([d e f]))])
; ([-30 70] = compose /negate [(10 + 20) ((30 + 40))])


; antiforms besides splices are not legal in compose, but you can reify them
[
    ([<a> ~null~ <b>] = compose // [
        [<a> (if ok [null]) <b>]
        :predicate cascade [eval/ reify/]
    ])
    ([<a>] = compose [<a> (~()~)])
    ([<a>] = compose [<a> (~void~)])  ; exception made for pure void
]

[
    ([a :a a: @a ^a] = compose [('a) :('a) ('a): @('a) ^('a)])

    ([[a] :[a] [a]: @[a] ^[a]] = compose [
        ([a]) :([a]) ([a]): @([a]) ^([a])
    ])

    ([(a) :(a) (a): @(a) ^(a)] = compose [
        ('(a)) :('(a)) ('(a)): @('(a)) ^('(a))
    ])

    ([a/b @a/b ^a/b] = compose [
        ('a/b) @('a/b) ^('a/b)
    ])

    ([a.b :a.b a.b: @a.b ^a.b] = compose [
        ('a.b) :('a.b) ('a.b): @('a.b) ^('a.b)
    ])
]

; More tests of crazy quoting depths
[
    ~???~ !! (compose ['''''''(if null [<a>])])
]

; You can apply quasiforms just like other quoting levels, but the value
; must not be already quoted.
[
    ([1 ~[2]~ 3] = compose [1 ~([2])~ 3])
    ([1 ''~[2]~ 3] = compose [1 ''~([2])~ 3])
    ~???~ !! (compose [1 ''~(quote [2])~ 3])
]

; We allow the reduced case of `eval []` or `eval [comment "hi"]` to be VOID,
; and this is an example of why we choose that instead of NOTHING.
[
    (
        condition: 1 = 2
        messages: []
        /log: func [msg] [append messages msg]
        ok
    )
    (
        ; kind of lame
        condition: 1 = 2
        [a c] = compose [a (either condition ['b] [log "skipping" void]) c]
    )
    (
        ; clearer but still lame
        condition: 1 = 2
        [a c] = compose [a (either condition ['b] [log "skipping", void]) c]
    )
    (
        ; best rhythm
        [a c] = compose [a (either condition ['b] [elide log "skipping"]) c]
    )
]

; COMPOSE is by default not willing to decay to other types than what's given.
; Can override with /CONFLATE
[
    ~conflated-sequence~ !! (compose $(blank)/(blank))
    ~conflated-sequence~ !! (compose $(blank).(blank))
    ~conflated-sequence~ !! (compose $(void).(void))

    (the / = compose:conflate $(blank)/(blank))
    (the . = compose:conflate $(blank).(blank))
    (null? compose:conflate $(void).(void))

    (_ = compose:conflate $(void).(_))
    (_ = compose:conflate $(_).(void))
    ('a = compose:conflate $(void).('a))
    ('a = compose:conflate $('a).(void))
    (@a = compose:conflate the @('a).(void))
]
