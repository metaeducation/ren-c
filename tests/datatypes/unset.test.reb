; datatypes/unset.r
(null? null)
(null? type of null)
(not null? 1)

(
    is-barrier?: func [x [<end> integer!]] [unset? 'x]
    is-barrier? ()
)
(void! = type of (do []))
(not void? 1)

[
    ('need-non-void = (trap [a: void | a])/id)
]

; NULL and VOID! assignments via SET are legal.  You are expected to do your
; own checks with ENSURE and NON.
;
(
    value: null
    error? trap [set quote a: non null value]
)
(not error? trap [set 'a null])
(
    value: void
    error? trap [set quote a: non void! :value]
)
(not error? trap [set 'a void])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value/foo]
    e/id = 'no-value
)
