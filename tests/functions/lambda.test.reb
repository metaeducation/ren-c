; %lambda.test.reb
;
; Lambdas are ACTION! generators that are lighter-weight than FUNC.  They do
; not have definitional returns, and at time of writing they leverage virtual
; binding instead of copying relativized versions of their bodies.


; Nested virtual binds were triggering an assertion.
;
(3 = (1 then x -> [2 also y -> [3]]))

; Meta parameters are allowed
[
    ('~baddie~ = if true [~baddie~] then ^x -> [x])
]

; Quoted parameters are allowed
[
    (quoter: 'x -> [x], (the a) = quoter a)
]
