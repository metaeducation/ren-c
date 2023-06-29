; %parse-void.test.reb
;
; While nulls in UPARSE generate errors (whether retrieved from GET-GROUP! or
; fetched from words), voids have no-op behavior...leaving the parse position
; alone and succeeding, evaluating to void.

; Quoted voids are just skipped, and skipped if hit in a variable

('b = parse [a b] ['a ' 'b])
(
    var: void
    'b = parse [a b] ['a var 'b]
)

; Voids evaporate, leaving the previous result...

('b = parse [a b] ['a 'b '])
(
    var: void
    'b = parse [a b] ['a 'b var]
)

; Given that they evaporate, they can't be assigned to variables...

(
    test: ~
    did all [
        'b = parse [a b] ['a test: ^['] 'b]
        nihil' = test
    ]
)
(
    test: ~
    var: void
    did all [
       'b = parse [a b] ['a test: ^[var] 'b]
        nihil' = test
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

; Void rules or GET-GROUP! are distinct from synthesized voids in plain GROUP!
; These are values that do not vanish.

(3 = parse [x] ['x (1 + 2) | 'y (10 + 20)])
('~[']~ = ^ parse [x] ['x (void) | 'y (10 + 20)])
