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
    ('no-value = (sys/util/rescue [a: ~ | a])/id)
]

; NULL and NOTHING assignments via SET are legal.  You are expected to do your
; own checks with ENSURE and NON.
;
(
    value: null
    error? sys/util/rescue [set the a: non null value]
)
(not error? sys/util/rescue [set 'a null])
(
    value: ~
    error? sys/util/rescue [set the a: non nothing! :value]
)
(not error? sys/util/rescue [set 'a ~])

(
    a-value: null
    e: sys/util/rescue [a-value/foo]
    e/id = 'no-value
)
