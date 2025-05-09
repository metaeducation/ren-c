; datatypes/unset.r
(null? null)
(null? try type of null)
(not null? 1)

(null = type of (eval []))
(not trash? 1)

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
    error? sys/util/rescue [set the a: non trash! :value]
)
(not error? sys/util/rescue [set 'a ~])

(
    a-value: null
    e: sys/util/rescue [a-value/foo]
    e/id = 'no-value
)
