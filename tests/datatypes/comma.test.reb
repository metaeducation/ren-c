; comma.test.reb
;
; COMMA! has the unusual rendering behavior of sticking to the thing on its
; left hand side.  It can be used in dialects for arbitrary purposes, but the
; thing it is used for in PARSE and the evaluator is to mark interstitial
; points between expressions.  This helps for readability, and it is
; enforced by errors if the comma turns out to be mid-expressions

(comma? first [,])
(comma? ',)

; Despite rendering with their left hand side, commas are part of the count.
;
(3 = length of [a, b])

; If [(#)] is valid, then [#,] needs to have parity
;
(2 = length of load "#,")

; Commas that occur at interstitials are legal
;
(7 = all [1 + 2, 3 + 4])

; Commas during argument gathering look like end of input
(
    e: trap [all [1 +, 2 3 + 4]]
    e/id = 'no-arg
)

; Commas are invisible and hence do not erase an evaluation value
;
(3 = do [1 + 2,])

; There must be space or other delimiters after commas.
(
    for-each [text] ["a,b" "a,, b" ",,"] [
        e: trap [load text]
        assert [e/id = 'scan-invalid]
    ]
    true
)

; Due to the spacing rule, the traditional "comma means decimal point" rule
; is still able to work--though it is less desirable for it to do so.
;
(1.1 = load "1,1")

; Because we're using `,` and not `|`, it can be used in the PARSE dialect.
; R3-Alpha's PARSE implementation was not particularly orderly in terms of
; holding the state of expressions, so detecting the interstitial points is
; a bit of a hack...but it's in as a proof of concept pending a better
; PARSE reorganization.
;
("aaabbb" = parse "aaabbb" [some "a", some "b"])
(
    e: trap ["aaabbb" = parse "aaabbb" [some, "a" some "b"]]
    e/id = 'expression-barrier
)

; Commas are "hard delimiters", so they won't be picked up in URL!
(
    commafied: [a, 1, <b>, #c, %def, http://example.com, "test",]
    normal: [a 1 <b> #c %def http://example.com "test"]

    remove-each x commafied [comma? x]
    commafied = normal
)
