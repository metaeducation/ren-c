; %lambda.test.r
;
; Lambdas are ACTION! generators that are lighter-weight than FUNC.  They do
; not have definitional returns, and at time of writing they leverage virtual
; binding instead of copying relativized versions of their bodies.


; Nested virtual binds were triggering an assertion.
;
(3 = (1 then (x -> [2 also (y -> [3])])))

; Meta parameters are allowed, and accept voids by default`
[
    ('~baddie~ = if ok [~baddie~] then ^x -> [x])

    (
        tester: ^x -> [if x = '~()~ [<void>] else [<nonvoid>]]
        <void> = tester comment "this should work"
    )

    ; this is true of funcs with no type specs on arguments as well
    (
        tester: func [^x] [if x = '~()~ [<void>] else [<nonvoid>]]
        <void> = tester comment "this should work"
    )
]

; Quoted parameters are allowed
[
    (quoter: 'x -> [x], (the a) = quoter a)
]

; Lambdas are void by default, for invisibility you need explicit return
[
    (lammy: x -> [], ghost? lammy 1)
    (lammy: lambda '[x y] [elide x + y], ghost? lammy 1 2)
    (lammy: lambda [x y {z}] [elide x + y], ghost? lammy 1 2)
]

(
    test: /x -> [x]
    all [
        null = test
        # = test/x
    ]
)

(
    test: [@(x)] -> [x]  ; ':x -> [x] subverts *lambda's* parameter convention!
    3 = test (1 + 2)
)

(
    test: return -> [return + 1]
    2 = test 1
)

(
    test: ["More complex" /return] -> [if return [<yes>] else [<no>]]
    all [
        <no> = test
        <yes> = test/return
    ]
)

(
    x: -> [1 + 2]
    3 = x
)
(
    x: (-> [1 + 2])
    3 = x
)

; Lambdas have type checking, but no RETURN word.  Use []: to give hint of
; what is missing.
[(
    foo: lambda [[]: [integer!]] [1020]
    1020 = foo
)
~bad-return-type~ !! (
    foo: lambda [[]: [tag!]] [304]
    foo
)]

; With lambdas, []: [] doesn't discard result, it typechecks it as trash
[(
    foo: lambda [[]: []] [~]
    tripwire? foo
)
~bad-return-type~ !! (
    foo: lambda [[]: []] [1020]
    foo
)]

; Alternative formulation for construction via code: LAMBDA BLOCK!
[
    (
        negatory: lambda block! [[x] reverse [x negate]]
        -1020 = negatory 1020
    )
]
