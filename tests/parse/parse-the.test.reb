; %parse-the.test.reb
;
; There are two competing potential needs for THE.  One is to generate a value
; out of the rule stream and not use it to match:
;
;     >> parse [2] [collect [keep integer!, keep the hearts]]
;     == [2 hearts]
;
;     >> parse [2] [collect [keep integer!, keep ('hearts)]]  ; equivalent
;     == [2 hearts]
;
; The other would be to turn the literal value from the rule stream into
; something you match in the input (this is similar to R3-Alpha's "QUOTE")
;
;     >> parse [''x] [the ''x]
;     == ''x
;
;     >> parse [''x] ['''x]  ; equivalent
;     == ''x
;
; It might seem like the second is more appealing, because it can help you
; see the number of quotes more clearly.  If quotes are part of the name of
; the variable, it might read better--e.g.
;
;     parse [x: result'] [set-word! the result']
;
; However, the first case actually has important uses under composition.
; Not needing to introduce a GROUP! to get a literal value has turned out to
; be very helpful (for an example, see the implementation of the Whitespace
; Interpreter Dialect).
;
; Decisions about this are still up in the air, but for the moment the second
; form is done under LIT for literal matching.

('a = parse [] [the a])  ; current behavior
('a = parse [] [@ a])  ; shorthand

(comment [  ; alternate behavior
    [
        ('wb == parse [wb] [the wb])
        (123 == parse [123] [the 123])
        (3 == parse [3 3] [2 the 3])
        ('_ == parse [blank] [the blank])
        ('some == parse [some] [the some])
    ]

    [#1314 (
        d: [a b c 1 d]
        ok
    )(
        'd = parse d [thru the 1 'd]
    )(
        1 = parse d [thru 'c the 1 elide 'd]
    )]
] ok)
