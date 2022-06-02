; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! = type of first [:a])
(
    ; context-less get-word
    e: trap [do make block! ":a"]
    e.id = 'not-bound
)

; R3-Alpha and Red permit GET-WORD! access to subvert unsetness.  But Ren-C
; sides with Rebol2, making it clearer that GET-WORD!'s main use is to
; suppress the execution of functions.
(
    unset 'a
    e: trap [:a]
    did all [
       'bad-word-get = e.id
       ':a = e.arg1
       '~ = e.arg2
    ]
)

[#1477
    ((match get-word! the :/) = (load-value ":/"))

    ((match get-path! the ://) = (load-value "://"))

    ((match get-path! the :///) = (load-value ":///"))
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

    ('inert-with-slashed = pick trap [ a/ ] 'id)
    (/a/ = /a/)
    (.a/ = .a/)

    (<inert> = :a)
    (<inert> = :/a)
    (<inert> = :.a)

    (<inert> = get 'a)
    (<inert> = get '/a)
    (<inert> = get '.a)

    (<inert> = get 'a.)
    ; (<inert> = get '/a.)  ; !!! Not working ATM, needs path overhaul
    ; (<inert> = get '.a.)  ; !!! Not working ATM, needs path overhaul

    ('inert-with-slashed = pick trap [ :a/ ] 'id)
    ('inert-with-slashed = pick trap [ :/a/ ] 'id)
    ('inert-with-slashed = pick trap [ :.a/ ] 'id)

    ('inert-with-slashed = pick trap [ get 'a/ ] 'id)
    ('inert-with-slashed = pick trap [ get '/a/ ] 'id)
    ('inert-with-slashed = pick trap [ get '.a/ ] 'id)

    ('inert-with-slashed = pick trap [ get ':a/ ] 'id)
    ('inert-with-slashed = pick trap [ get ':/a/ ] 'id)
    ('inert-with-slashed = pick trap [ get ':.a/ ] 'id)

    ('inert-with-slashed = pick trap [ get 'a/: ] 'id)
    ('inert-with-slashed = pick trap [ get '/a/: ] 'id)
    ('inert-with-slashed = pick trap [ get '.a/: ] 'id)
  ]
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

    ('action-with-dotted = pick trap [ a. ] 'id)
    (/a. = /a.)
    (.a. = .a.)

    ("active" = a/)
    (/a/ = /a/)
    (.a/ = .a/)

    (action? :a)
    (action? :/a)
    (action? :.a)

    (action? get 'a)
    (action? get '/a)
    (action? get '.a)

    ('action-with-dotted = pick trap [ get 'a. ] 'id)
    ; (<inert> = get '/a.)  ; !!! Not working ATM, needs path overhaul
    ; (<inert> = get '.a.)  ; !!! Not working ATM, needs path overhaul

    (action? :a/)
    (action? :/a/)
    (action? :.a/)

    (action? get 'a/)
    (action? get '/a/)
    (action? get '.a/)

    (action? get ':a/)
    (action? get ':/a/)
    (action? get ':.a/)

    (action? get 'a/:)
    (action? get '/a/:)
    (action? get '.a/:)
  ]
)]
