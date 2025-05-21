; %meta.test.reb
;
; Operator whose evaluator behavior is like QUOTE, but accepts antiforms and
; gives back quaisforms

((the '3) = ^ 1 + 2)

('~[~null~]~ = lift if ok [null])

((lift null) = lift null)

; The comment "Hi" does not vanish and result in a meta of 3...only one step
; is taken in evaluating arguments to functions.  You must put invisibles in
; a group if you wish their invisibility to be subsumed in the argument.
;
(3 = all [
    void? comment "Hi" 1 + 2
])
((the '3) = lift (comment "Hi" 1 + 2))
(3 = ^(lift (comment "Hi" 1 + 2)))
