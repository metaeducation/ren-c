; %the.test.reb
;
; @ (which is of type SYMBOL!) is a special operator
;
; The other THE-XXX! types are pretty simple since they are inert.
;

(@word = first [@word])
(@tu.p.le = first [@tu.p.le])
(@pa/th = first [@pa/th])
(@[bl o ck] = first [@[bl o ck]])
(@(gr o up) = first [@(gr o up)])

; Plain THE always gives back the argument as-is.  It can look nicer than
; quoting, e.g. if the thing you're quoting has quotes in it.
;
;      word = the didn't
;      word = 'didn't
;
; It also makes it clearer if you are dealing with something that is quoted
; multiple times.
;
;      value = '''10       ; test if value has two quotes on it
;      value = the ''10    ; but this makes the "two" more obvious.
[
   ('x = the x)
   ('(a b c) = the (a b c))
   ('~null~ = the ~null~)
]

; @ runs the action THE*, and has special case behavior that ~null~ becomes
; plain null.  Other BAD-WORD!s are errors.  This is useful in the API as it
; splices plain ~NULL~ BAD-WORD! into slots where nullptr was passed, so you
; can avoid having to use rebQ() on arguments (so long as you know they are
; not generally BAD-WORD!s otherwise, in which case you have to.)
[
   ('x = @ x)
   ('(a b c) = @ (a b c))
   (null = @ ~null~)
   (error? trap [@ ~something~])
]
