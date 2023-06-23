; %parse-void.test.reb
;
; While nulls in UPARSE generate errors (whether retrieved from GET-GROUP! or
; fetched from words), voids have no-op behavior...leaving the parse position
; alone and succeeding, evaluating to void.

; Quoted voids are just skipped, whether literal or in a variable.

('b = parse [a b] ['a ' 'b])
(
    var: the '
    'b = parse [a b] ['a var 'b]
)

; Voids evaporate, leaving the previous result...

('b = parse [a b] ['a 'b '])
(
    var: the '
    'b = parse [a b] ['a 'b var]
)

; ...however, if used as arguments in combinators (including the SET-WORD!
; combinator) they are treated as values assigned to variables

(
    test: ~
    did all [
        'b = parse [a b] ['a test: ' 'b]
        void? test  ; notably *not* b
    ]
)
(
    test: ~
    var: the '
    did all [
       'b = parse [a b] ['a test: var 'b]
        void? test  ; notably *not* b
    ]
)

; Notice functionally, a void rule acts the same as an empty block rule

('b = parse [a b] ['a 'b []])
(
    var: []
    'b = parse [a b] ['a 'b var]
)
('b = parse [a b] ['a 'b :(if true '[])])

; Voided expressions work in GET-GROUP! substitutions

('b = parse [a b] ['a 'b :(if false [[some 'c]])])
('c = parse [a b c c c] ['a 'b :(if true [[some 'c]])])

; Liberal policy of letting voids opt-out is convenient to use void as a
; state equivalent to no-op...if you are willing to deal with the possible
; slipperiness of such values, e.g. consider:
;
;    c-rule-copy: all [1 = 1, c-rule]  ; won't act like you expect
;
; This may suggest a naming convention for variables which can be void,
; such as *c-rule, to draw attention to the issue.

(
    c-rule: if false [[some 'c]]
    'b = parse [a b] ['a 'b c-rule]
)
(
    c-rule: if true [[some 'c]]
    'c = parse [a b c c c] ['a 'b c-rule]
)

; A null combinator does not make sense, and a combinator which would quote
; a WORD! to fetch it from the rules and void it would probably cause more
; confusion than anything.  Using a GET-GROUP! and calling the MAYBE function
; through the normal evaluator avoids the convolutedness.

(
    c-rule: null
    'b = parse [a b] ['a 'b :(maybe c-rule)]
)
(
    c-rule: [some 'c]
    'c = parse [a b c c c] ['a 'b :(maybe c-rule)]
)

; Rules that may have a behavior -or- don't advance and always succeed are
; tricky to use.  But UPARSE does have some tools for it.  Here's a sample of
; how you might mix MAYBE, FURTHER, SOME, and OPT.

(
    prefix: null
    suffix: ")"

    ")" = parse "aaa)))" [
        opt some further :(maybe prefix)
        some "a"
        opt some further :(maybe suffix)
     ]
)

; Important to remember that a synthesized void from a plain GROUP! will
; effectively vanish.  Getting around this might be accomplished via a
; substitution of some kind, or speaking in terms of meta values.

(3 = parse [x] ['x (1 + 2) | 'y (10 + 20)])
('x = parse [x] ['x (void) | 'y (10 + 20)])  ; can't map x to void like this
(
    result: parse [x] ['x (<void>) | 'y (10 + 20)]
    if result = <void> [result: void]  ; sample workaround
    void? result
)
(void? unmeta parse [x] ['x ^(void) | 'y ^(10 + 20)])  ; alternative
