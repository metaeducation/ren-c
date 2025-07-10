; %parse-skip.test.r
;
; UPARSE SKIP comes across as more literate, as SKIP 2 can be written where the
; historical Redbol parse would do 2 SKIP.  That was convoluted, in the sense
; that SKIP was an arity-0 function that skipped one unit...being applied
; twice by an implicit repeat rule.  :-/  Having it be a combinator that takes
; an argument of how much to skip makes more sense, and is further justification
; of INTEGER! just being a combinator that evaluates to itself.
;

("Much more literate!" = parse [a b "Much more literate!"] [skip 2, text!])
("SKIP returns void!!!" = parse ["SKIP returns void!!!" a b] [text!, skip 2])
