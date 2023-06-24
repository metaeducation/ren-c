; %meta.test.reb
;
; Operator whose evaluator behavior is like QUOTE, but accepts isotopes/void

((the '3) = ^ 1 + 2)

('~[~null~]~ = ^ if true [null])

(null' = ^ null)

; The comment "Hi" does not vanish and result in a meta of 3...only one step
; is taken in evaluating arguments to functions.  You must put invisibles in
; a group if you wish their invisibility to be subsumed in the argument.
;
(3 = all [
    nihil? comment "Hi" 1 + 2
])
((the '3) = ^ (comment "Hi" 1 + 2))
((the '3) = ^(comment "Hi" 1 + 2))

; !!! Is this the best behavior, or should it return META VOID ?
;
~no-arg~ !! (^)
