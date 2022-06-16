; %lambda.test.reb
;
; Lambdas are ACTION! generators that are lighter-weight than FUNC.  They do
; not have definitional returns, and at time of writing they leverage virtual
; binding instead of copying relativized versions of their bodies.


; Nested virtual binds were triggering an assertion.
;
(3 = (1 then x -> [2 also y -> [3]]))

; Meta parameters are allowed, and accept voids by default`
[
    ('~baddie~ = if true [~baddie~] then ^x -> [x])

    (
        tester: ^x -> [if x = '~void~ [<void>] else [<nonvoid>]]
        <void> = tester comment "this should work"
    )

    ; this is true of funcs with no type specs on arguments as well
    (
        tester: func [^x] [if x = '~void~ [<void>] else [<nonvoid>]]
        <void> = tester comment "this should work"
    )
]

; Quoted parameters are allowed
[
    (quoter: 'x -> [x], (the a) = quoter a)
]

; Lambdas can be invisible
[
    (lammy: x -> [], void? lammy 1)
    (lammy: lambda '[x y] [elide x + y], void? lammy 1 2)
    (lammy: lambda [x y <local> z] [elide x + y], void? lammy 1 2)
]
