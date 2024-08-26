; datatypes/unset.r
(null? null)
(null? type of null)
(not null? 1)

(
    is-barrier?: func [x [<end> integer!]] [null? x]
    is-barrier? ()
)
(void! = type of (do []))
(not nothing? 1)

[
    ('no-value = (trap [a: ~ | a])/id)
]

; NULL and NOTHING assignments via SET are legal.  You are expected to do your
; own checks with ENSURE and NON.
;
(
    value: null
    error? trap [set the a: non null value]
)
(not error? trap [set 'a null])
(
    value: ~
    error? trap [set the a: non nothing! :value]
)
(not error? trap [set 'a ~])

(
    a-value: null
    e: trap [a-value/foo]
    e/id = 'no-value
)
