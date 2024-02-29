; datatypes/unset.r
(null? null)
(null? type of null)
(not null? 1)

(
    is-barrier?: func [x [<end> integer!]] [null? x]
    is-barrier? ()
)
(trash! = type of (do []))
(not trash? 1)

[
    ('need-non-trash = (trap [a: ~ | a])/id)
]

; NULL and TRASH assignments via SET are legal.  You are expected to do your
; own checks with ENSURE and NON.
;
(
    value: null
    error? trap [set quote a: non null value]
)
(not error? trap [set 'a null])
(
    value: ~
    error? trap [set quote a: non trash! :value]
)
(not error? trap [set 'a ~])

(
    a-value: null
    e: trap [a-value/foo]
    e/id = 'no-value
)
