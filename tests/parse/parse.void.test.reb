; %parse-void.test.reb
;
; While nulls in UPARSE generate errors (whether retrieved from GET-GROUP! or
; fetched from words), voids have no-op behavior...leaving the parse position
; alone and succeeding, evaluating to void.

; meta voids are just skipped, and skipped if hit in a variable

('b = parse [a b] ['a ~[]~ 'b])
(
    ^var: void
    'b = parse [a b] ['a ^var 'b]
)

; Voids synthesize void

(void? parse [a b] ['a 'b ~[]~])
(
    ^var: void
    void? parse [a b] ['a 'b ^var]
)
(
    test: ~
    all [
        'b = parse [a b] ['a ^test: [~[]~] 'b]
        void? ^test
    ]
)
(
    test: ~
    ^var: void
    all [
       'b = parse [a b] ['a ^test: [^var] 'b]
        void? ^test
    ]
)

; Voided expressions work in GET-GROUP! substitutions

(void? parse [a b] ['a 'b :(opt if null [[some 'c]])])
('c = parse [a b c c c] ['a 'b :(opt if ok [[some 'c]])])

; Liberal policy of letting voids skip ahead is convenient to use void as a
; state equivalent to no-op...if you are willing to deal with the possible
; slipperiness of such values, e.g. consider:
;
;    c-rule-copy: all [1 = 1, c-rule]  ; won't act like you expect
;
; If this is not what you want, then ~okay~ is a better fit.

(
    c-rule: if null [[some 'c]]
    void = parse [a b] ['a 'b c-rule]
)
(
    c-rule: if ok [[some 'c]]
    'c = parse [a b c c c] ['a 'b c-rule]
)

; A null combinator does not make sense, and a combinator which would quote
; a WORD! to fetch it from the rules and void it would probably cause more
; confusion than anything.  Using a GET-GROUP! and calling the OPT function
; through the normal evaluator avoids the convolutedness.

(
    c-rule: null
    void = parse [a b] ['a 'b :(opt c-rule)]
)
(
    c-rule: [some 'c]
    'c = parse [a b c c c] ['a 'b :(opt c-rule)]
)

; Rules that may have a behavior -or- don't advance and always succeed are
; tricky to use.  But UPARSE does have some tools for it.  Here's a sample of
; how you might mix FURTHER, SOME, and OPT.

(
    prefix: null
    suffix: ")"

    ")" = parse "aaa)))" [
        opt some further :(opt prefix)
        some "a"
        opt some further :(opt suffix)
     ]
)

; Voids synthesized from plain GROUP! also do not vanish

(3 = parse [x] ['x (1 + 2) | 'y (10 + 20)])
(void? parse [x] ['x (void) | 'y (10 + 20)])
