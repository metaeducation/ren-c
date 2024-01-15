; %the.test.reb
;
; @ is a WORD! that is nominally used as a special operator
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
   ('~[]~ = the ~[]~)
]

; @ runs the action THE*, and has special case behavior that queasiforms
; become antiforms.  This is useful in the API as it splices ~null~ QUASIFORM!
; values into slots where nullptr was passed, which become their antiforms.
; So can avoid having to use rebQ() on arguments that you aren't intending as
; QUASIFORM!.  The good part is that if you do this in error, you'll probably
; find out--since the antiforms will not be silently accepted most places.
[
   ('x = @ x)
   ('(a b c) = @ (a b c))
   (
      assert ['~null~ = ^ x: @ ~null~]
      x = null
   )
   ('~something~ = ^ @ ~something~)
]
