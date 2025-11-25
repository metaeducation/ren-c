; %meta.test.r
;
; Operator whose evaluator behavior is like QUOTE, but accepts antiforms and
; gives back quaisforms

((the '3) = lift 1 + 2)

('~[~null~]~ = lift if ok [null])

((lift null) = lift null)

(3 = all [
    ghost? comment "Hi" 1 + 2
])
((the '3) = lift (comment "Hi" 1 + 2))
(3 = unlift (lift (comment "Hi" 1 + 2)))
