(
    success: <bad>
    if 1 > 2 [success: false] else [success: true]
    success
)
(
    success: <bad>
    if 1 < 2 [success: true] else [success: false]
    success
)
(
    success: <bad>
    if not 1 > 2 [success: true] else [success: false]
    success
)
(
    success: <bad>
    if not 1 < 2 [success: false] else [success: true]
    success
)
(
    success: <bad>
    if true (does [success: true])
    success
)
(
    success: true
    if false (does [success: false])
    success
)

[https://github.com/metaeducation/ren-c/issues/510 (
    c: func [return: [integer!] i] [
        return if i < 15 [30] else [4]
    ]

    d: func [return: [integer!] i] [
        return (if i < 15 [30] else [4])
    ]

    all [
        30 = c 10
        4 = c 20
        30 = d 10
        4 = d 20
    ]
)]

; Hard quotes need to account for enfix deferral
(
    foo: func [return: [integer!] y] [return the 1 then (x -> [x + y])]
    bar: func [return: [integer!] y] [return 1 then (x -> [x + y])]
    all [
        11 = foo 10
        1 = bar 10
    ]
)

; The above should work whether you use a GROUP! or not (-> quote left wins)
(
    foo: func [return: [integer!] y] [return the 1 then x -> [x + y]]
    bar: func [return: [integer!] y] [return 1 then x -> [x + y]]
    all [
        11 = foo 10
        1 = bar 10
    ]
)

; This is a by-product of the wish to make it so (@ <QUASIFORM!>) can produce
; a true NULL, for use with the API...and that this provoking quasivalue be
; something the API puts in automatically when a nullptr is given as input.
(
    null? (~null~ then [fail ~unreachable~])
)

(~ then [true])

; Although branches can be triggered by heavy null, if functions ask to
; receive the value it is decayed, so they do not have to be ^META.  But if
; they *are* meta then the true state is passed through.
[
    (~[~null~]~ then x -> [x = null])
    (~[~null~]~ then ^x -> [x = the ~[~null~]~])
    ('~[~null~]~ then x -> [x = the ~[~null~]~])
    ('~[~null~]~ then ^x -> [x = the '~[~null~]~])

    (catch [~[~null~]~ also x -> [throw (x = null)]])
    (catch [~[~null~]~ also ^x -> [throw (x = the ~[~null~]~)]])
    (catch ['~null~ also ^x -> [throw (x = the '~null~)]])

    ~???~ !! (~[]~ *else x -> [1020])
    ~???~ !! (~[]~ *else ^x -> [1020])

    (null *else ^x -> [null' = x])
]

; Variant forms react to heavy null as if they were null.  This can be
; useful in chaining scenarios.
;
; https://forum.rebol.info/t/why-then-and-else-are-mutually-exclusive/1080/9
[
    (~null~ *then [fail "shouldn't run"] else [true])
    (~null~ *also [fail "shouldn't run"] *else [true])
    (~null~ *else [true])
]

[
    ~no-arg~ !! (else [~unused~])
    ~???~ !! (() else [true])  ; should NIHIL with enfix look like no value?
    ~???~ !! (1000 + 20 () then [fail ~unreachable~])

    (void else [true])
    (1020 = (1000 + 20 elide-if-void (void then [fail ~unreachable~])))

    ((void) else [true])
    (void? (1000 + 20 ((void) then [fail ~unreachable~])))

    (eval [] else [true])
    (void? eval [] then [fail ~reachable~])
]

[
    (foo: func [] [return void], true)
    (foo else [true])
    (1020 = (1000 + 20 elide-if-void (foo then [fail [~unreachable~]])))
]

[
    (foo: lambda [] [if false [~ignore~]], true)
    (foo else [true])
    (void? (1000 + 20 foo then [fail [~unreachable~]]))
]

[
    (foo: lambda [] [], true)
    (foo else [true])
    (1020 = (1000 + 20 elide-if-void (foo then [fail [~unreachable~]])))
]

; https://forum.rebol.info/t/2176
[
    (null = (null else (void)))
    (void = (null else (void)))

    (3 = (if true [1 + 2] then (void)))
]
