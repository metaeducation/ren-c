; %the.test.reb
;
; @ (which is of type THE!) is a special operator 
;
; The other THE-XXX! types are pretty simple since they are inert.
;

(@word = first [@word])
(@tu.p.le = first [@tu.p.le])
(@pa/th = first [@pa/th])
(@[bl o ck] = first [@[bl o ck]])
(@(gr o up) = first [@(gr o up)])

; THE! has special case behavior that ~null~ becomes plain null
[
   ('x = @ x)
   ('(a b c) = @ (a b c))
   (null = @ ~null~)
] 
