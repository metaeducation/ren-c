; %compose.test.reb

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
    f1: func [] [compose [(return 1)] 2]
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
    same? blk first compose [((reduce [blk]))]
)
(
    blk: []
    same? blk first compose/only [(blk)]
)
; recursion
(
    num: 1
    [num 1] = compose [num ((compose [(num)]))]
)
; infinite recursion
(
    blk: [(compose blk)]
    error? trap blk
)

; #1906
(
    b: copy [] insert/dup b 1 32768 compose b
    sum: 0
    for-each i b [sum: me + i]
    sum = 32768
)

; COMPOSE with implicit /ONLY-ing

(
    block: [a b c]
    [splice: a b c only: [a b c]] = compose [splice: ((block)) only: (block)]
)

; COMPOSE with pattern, beginning tests

(
    [(1 + 2) 3] = compose <*> [(1 + 2) (<*> 1 + 2)]
)(
    [(1 + 2)] = compose <*> [(1 + 2) (<*>)]
)(
    'a/(b)/3/c = compose <?> 'a/(b)/(<?> 1 + 2)/c
)(
    [(a b c) [((d) 1 + 2)]] = compose/deep </> [(a (</> 'b) c) [((d) 1 + 2)]]
)

(
    [(left alone) [c b a] c b a ((left alone))]
    = compose <$> [
        (left alone) (<$> reverse copy [a b c]) ((<$> reverse copy [a b c]))
        ((left alone))
    ]
)


; While some proposals for COMPOSE handling of QUOTED! would knock one quote
; level off a group, protecting groups from composition is better done with
; labeled compose...saving it for quoting composed material.

([3 '3 ''3] == compose [(1 + 2) '(1 + 2) ''(1 + 2)])
(['] = compose ['(if false [<vanish>])])

; Quoting should be preserved by deep composition

([a ''[b 3 c] d] == compose/deep [a ''[b (1 + 2) c] d])


; Using a SET-GROUP! will *try* to convert the composed value to a set form

([x:] = compose [('x):])
([x:] = compose [('x:):])
([x:] = compose [(':x):])

; Running code during SETIFY/GETIFY internally was dropped, because the
; scanner was using it...and it had DS_PUSH()es extant.  The feature is still
; possible, but it's not clear it's a great idea.  Punt on letting you
; getify or setify things that aren't guaranteed to succeed (e.g. a string
; might have spaces in it, and can't be turned into a SET-WORD!)
;
(error? trap [[x:] = compose [(#x):]])
(error? trap [[x:] = compose [("x"):]])

([x/y:] = compose [( 'x/y ):])
([x/y:] = compose [( 'x/y: ):])
([x/y:] = compose [( ':x/y ):])

([(x y):] = compose [( '(x y) ):])
([(x y):] = compose [( '(x y): ):])
([(x y):] = compose [( ':(x y) ):])

([[x y]:] = compose [( '[x y] ):])
([[x y]:] = compose [( '[x y]: ):])
([[x y]:] = compose [( ':[x y] ):])


; Using a GET-GROUP! will *try* to convert the composed value to a get form
;
; Note: string conversions to unbound words were done at one point, but have
; been dropped, at least for the moment:
;
;    ([:x] = compose [:(#x)])
;    ([:x] = compose [:("x")])
;
; They may be worth considering for the future.

([:x] = compose [:('x)])
([:x] = compose [:('x:)])
([:x] = compose [:(':x)])

([:x/y] = compose [:( 'x/y )])
([:x/y] = compose [:( 'x/y: )])
([:x/y] = compose [:( ':x/y )])

([:(x y)] = compose [:( '(x y) )])
([:(x y)] = compose [:( '(x y): )])
([:(x y)] = compose [:( ':(x y) )])

([:[x y]] = compose [:( '[x y] )])
([:[x y]] = compose [:( '[x y]: )])
([:[x y]] = compose [:( ':[x y] )])

; !!! This was an interesting concept, but now that REFINEMENT! and PATH! are
; unified it can't be done with PATH!, as you might say `compose obj/block`
; and mean that.  The notation for predicates have to be rethought.
;
; ([a b c d e f] = compose /identity [([a b c]) (([d e f]))])
; ([[a b c] d e f] = compose /enblock [([a b c]) (([d e f]))])
; ([-30 70] = compose /negate [(10 + 20) ((30 + 40))])


; BAD-WORD! isotopes are not legal in compose, except for the null isotope.
[
    ([<a> <b>] = compose [<a> (if true [null]) <b>])
    (error? trap [compose [<a> (~unset~)]])
]
 