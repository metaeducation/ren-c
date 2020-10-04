; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! = type of first [:a])
(
    ; context-less get-word
    e: trap [do make block! ":a"]
    e/id = 'not-bound
)
(
    unset 'a
    null? :a
)

[#1477
    ((match get-path! lit :/) = (load ":/"))

    ((match get-path! lit ://) = (load "://"))

    ((match get-path! lit :///) = (load ":///"))
]

; Terminal dotted access inhibits action invocation, while slashed access
; enforces action invocation.
[
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

; Terminal slash does the opposite of terminal dot, by enforcing that the
; thing fetched is an action.
[
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
