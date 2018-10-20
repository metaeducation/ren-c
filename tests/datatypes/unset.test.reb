; datatypes/unset.r
(null? null)
(null? type of null)
(not null? 1)

(
    is-barrier?: func [x [<end> integer!]] [unset? 'x]
    is-barrier? ()
)
(void! == type of (do []))
(not void? 1)

[#68
    ('Need-Value is (trap [a: ()])/id)
]

(error? trap [set* quote a: null a])
(not error? trap [set* 'a null])

(not error? trap [set* quote a: void a])
(not error? trap [set* 'a void])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value]
    e/id is 'No-Value
)
