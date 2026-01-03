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
    foo: func [return: [integer!] y] [return the 1 then (x -> [x + y])]
    bar: func [return: [integer!] y] [return 1 then (x -> [x + y])]
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
; they *are* ^META then the true state is passed through.
[
    (~(~null~)~ then (x -> [(lift ^x) = '~null~]))
    (~(~null~)~ then (^x -> [(lift ^x) = '~(~null~)~]))
    ('~(~null~)~ then (x -> [^x = '~(~null~)~]))
    ('~(~null~)~ then (^x -> [^x = '~(~null~)~]))

    (catch [~(~null~)~ also (x -> [throw ((lift ^x) = '~null~)])])
    (catch [~(~null~)~ also (^x -> [throw ((lift ^x) = '~(~null~)~)])])
    (catch ['~null~ also (^x -> [throw (^x = '~null~)])])
]

[
    ~no-arg~ !! (else [~unused~])
    (() then [okay])  ; GHOST! with THEN is legal...
    (1000 + 20 () then [okay])

    (^ghost then [okay])
    (1020 = (1000 + 20 elide-if-void (^ghost else [panic ~#unreachable~])))

    ((^ghost) then [okay])
    (ghost? (1000 + 20 ((^ghost) else [panic ~#unreachable~])))

    (eval [] then [okay])
    (ghost? eval [] else [panic ~#unreachable~])
]

[
    (foo: func [] [return ^ghost], ok)
    (foo then [okay])
    (1020 = (1000 + 20 elide-if-void (foo else [panic ~#unreachable~])))
]

[
    (foo: lambda [] [if null [panic ~#unreachable~]], ok)
    (foo else [okay])
    (null? (1000 + 20 foo then [panic ~#unreachable~]))
]

[
    (foo: lambda [] [], ok)
    (foo then [okay])
    (1020 = (1000 + 20 elide-if-void (foo else [panic ~#unreachable~])))
]

; https://forum.rebol.info/t/2176
[
    (null = (null else (^ghost)))
    (^ghost = (^ghost else (^ghost)))

    (3 = (if ok [1 + 2] then (^ghost)))
]

; THENCE is prefix reversed THEN
[
    (3 = thence [1 + 2] okay)
    (null? thence [1 + 2] null)
    (ghost? thence [1 + 2] ())
]
