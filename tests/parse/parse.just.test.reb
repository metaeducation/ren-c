; %parse-just.test.reb
;
; Firstly, consider that there are two competing potential needs for a
; literal construct like JUST.  One is to generate a value out of the rule
; stream and not use it to match:
;
;     >> parse [2] [collect [keep integer!, keep just hearts]]
;     == [2 hearts]
;
;     >> parse [2] [collect [keep integer!, keep ('hearts)]]  ; equivalent
;     == [2 hearts]
;
; The other would be to turn the literal value from the rule stream into
; something you match in the input (this is similar to R3-Alpha's "QUOTE")
;
;     >> parse [''x] [just ''x]
;     == ''x
;
;     >> parse [''x] ['''x]  ; equivalent
;     == ''x
;
; It might seem like the second is more appealing, because it can help you
; see the number of quotes more clearly.  If quotes are part of the name of
; the variable, it might read better--e.g.
;
;     parse [x: result'] [&set-word? just result']
;
; However, the first case actually has important uses under composition.
; Not needing to introduce a GROUP! to get a literal value has turned out to
; be very helpful (for an example, see the implementation of the Whitespace
; Interpreter Dialect).
;
; Since both cases are important, the one that doesn't match is called JUST,
; because it goes along well with "Just synthesize this value, don't match it"
; (whereas "Just match this value, don't synthesize it" doesn't make sense.)
;
; The second form is called LIT for LITERAL.

('a = parse [] [just a])
('a = parse [] [' a])  ; shorthand
