; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(chain! = type of first [:a])

; R3-Alpha and Red permit GET-WORD! access to subvert unsetness.  Rebol2 did
; not permit this.  At one point Ren-C limited GET-WORD! use to functions, but
; the rise of more antiform forms leans to doing it like R3-Alpha and Red.
(
    a: ~
    e: sys.util/recover [a]
    all [
       'bad-word-get = e.id
       'a = e.arg1
       '~ = e.arg2
    ]
)
(
    a: ~
    trash? ^a
)

[#1477
    (
        x: transcode:one ":/"
        all [
            ':/ = x
            get-word? x
        ]
    )

    (
        x: transcode:one "://"
        all [
            ':// = x
            get-word? x
        ]
    )

    (
        x: transcode:one ":///"
        all [
            ':/// = x
            get-word? x
        ]
    )
]

; Terminal dotted access inhibits action invocation, while slashed access
; enforces action invocation.
;
; !!! This set of features is currently under review.
[(
  comment [
    (did a: <inert>)

    (<inert> = a)
    (/a = /a)
    (.a = .a)

    (<inert> = a.)
    (/a. = /a.)
    (.a. = .a.)

    ('inert-with-slashed = pick rescue [ a/ ] 'id)
    (/a/ = /a/)
    (.a/ = .a/)

    (<inert> = :a)
    (<inert> = :.a)

    (<inert> = get $a)
    (<inert> = get $/a)
    (<inert> = get $.a)

    (<inert> = get $a.)
    ; (<inert> = get $/a.)  ; !!! Not working ATM, needs path overhaul
    ; (<inert> = get $.a.)  ; !!! Not working ATM, needs path overhaul

    ('inert-with-slashed = pick rescue [ get $a/ ] 'id)
    ('inert-with-slashed = pick rescue [ get $/a/ ] 'id)
    ('inert-with-slashed = pick rescue [ get $.a/ ] 'id)
  ]
  ok
)]

; Terminal slash does the opposite of terminal dot, by enforcing that the
; thing fetched is an action.
;
; !!! This set of features is currently under review.
[(
  comment [
    (did a: does ["active"])

    ("active" = a)
    (/a = /a)
    (.a = .a)

    ('action-with-dotted = pick rescue [ a. ] 'id)
    (/a. = /a.)
    (.a. = .a.)

    ("active" = a/)
    (/a/ = /a/)
    (.a/ = .a/)

    (action? :a)
    (action? :.a)

    (action? get $a)
    (action? get $/a)
    (action? get $.a)

    ('action-with-dotted = pick rescue [ get $a. ] 'id)
    ; (<inert> = get $/a.)  ; !!! Not working ATM, needs path overhaul
    ; (<inert> = get $.a.)  ; !!! Not working ATM, needs path overhaul

    (action? get $a/)
    (action? get $/a/)
    (action? get $.a/)
  ]
  ok
)]

; GET of a GROUP! can handle everything except producing another group.

(
    name: 10
    10 = get:groups $($name)
)
(
    obj: make object! [field: pack [10 20]]  ; decayed assign
    (the '10) = lift get:groups $($obj.field)  ; plain get
)
(
    obj: make object! [field: pack [10 20]]  ; decayed assign
    (the '10) = lift get:groups $(meta $obj.field)  ; meta get
)
(
    obj: make object! [^field: pack [10 20]]  ; meta assign
    '~('10 '20)~ = lift get:groups $(meta $obj.field)  ; meta get
)
~???~ !! (
    obj: make object! [^field: pack [10 20]]  ; meta assign
    '~('10 '20)~ = lift get:groups $($obj.field)  ; plain get
)
~???~ !! (
    name: 10
    code: $($($name))
    get:groups $(code)
)
(null? get:groups $())
(null? get:groups ())
(null? get:groups ^ghost)
(null? get:groups (^ghost))

('~('1 '2)~ = lift get ['1 '2])
(
    a: 10
    b: 20
    '~('10 '20)~ = lift get [a b]
)
