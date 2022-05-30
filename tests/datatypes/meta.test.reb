; %meta.test.reb
;
; Operator whose evaluator behavior is like QUOTE, but it distinguishes
; the internal NULL isotope state from plain NULL (which quote does not)

((the '3) = ^ 1 + 2)

('~null~ = ^ if true [null])

(null = ^ null)

; The comment "Hi" does not vanish and result in a meta of 3...only one step
; is taken in evaluating arguments to functions.  You must put invisibles in
; a group if you wish their invisibility to be subsumed in the argument.
;
(3 = all [
    invisible? comment "Hi" 1 + 2
])
(3 = all [
    invisible? maybe comment "Hi" 1 + 2
])
((the '3) = ^ (comment "Hi" 1 + 2))
((the '3) = ^(comment "Hi" 1 + 2))

; !!! Is this the best behavior, or should it say "need-non-end"?
;
(null = trap [^])
