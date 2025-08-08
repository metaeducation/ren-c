(
    success: ~
    if 1 > 2 [success: 'false] else [success: 'true]
    true? success
)
(
    success: ~
    if 1 < 2 [success: 'true] else [success: 'false]
    true? success
)
(
    success: ~
    if not 1 > 2 [success: 'true] else [success: 'false]
    true? success
)
(
    success: ~
    if not 1 < 2 [success: 'false] else [success: 'true]
    true? success
)
(
    success: ~
    if ok (does [success: 'true])
    true? success
)
(
    success: 'true
    if null (does [success: 'false])
    true? success
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

; Hard quotes need to account for infix deferral
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
; a pure NULL, for use with the API...and that this provoking quasivalue be
; something the API puts in automatically when a nullptr is given as input.
(
    null? (~null~ then [panic ~#unreachable~])
)

(~ then [okay])

; Although branches can be triggered by heavy null, if functions ask to
; receive the value it is decayed, so they do not have to be ^META.  But if
; they *are* lifted then the true state is passed through.
[
    (~[~null~]~ then x -> [x = null])
    (~[~null~]~ then ^x -> [x = the ~[~null~]~])
    ('~[~null~]~ then x -> [x = the ~[~null~]~])
    ('~[~null~]~ then ^x -> [x = the '~[~null~]~])

    (catch [~[~null~]~ also x -> [throw (x = null)]])
    (catch [~[~null~]~ also ^x -> [throw (x = the ~[~null~]~)]])
    (catch ['~null~ also ^x -> [throw (x = the '~null~)]])
]

[
    ~no-arg~ !! (else [~unused~])
    ~???~ !! (() else [okay])  ; should VOID with infix look like no value?
    ~???~ !! (1000 + 20 () then [panic ~#unreachable~])

    (^void then [okay])
    (1020 = (1000 + 20 elide-if-void (^void else [panic ~#unreachable~])))

    ((^void) then [okay])
    (void? (1000 + 20 ((^void) else [panic ~#unreachable~])))

    (eval [] then [okay])
    (void? eval [] else [panic ~#unreachable~])
]

[
    (foo: func [] [return ^void], ok)
    (foo then [okay])
    (1020 = (1000 + 20 elide-if-void (foo else [panic ~#unreachable~])))
]

[
    (foo: lambda [] [if null [panic ~#unreachable~]], ok)
    (foo else [okay])
    (nulld? (1000 + 20 foo then [panic ~#unreachable~]))
]

[
    (foo: lambda [] [], ok)
    (foo then [okay])
    (1020 = (1000 + 20 elide-if-void (foo else [panic ~#unreachable~])))
]

; https://forum.rebol.info/t/2176
[
    (null = (null else (^void)))
    (^void = (^void else (^void)))

    (3 = (if ok [1 + 2] then (^void)))
]
