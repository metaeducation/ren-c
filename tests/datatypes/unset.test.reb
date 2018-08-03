; datatypes/unset.r
(null? null)
(null? type of null)
(not null? 1)

(void? ())
(void! = type of ())
(not void? 1)

[#68
    ('need-value = (trap [a: ()])/id)
]

(error? trap [set* quote a: null a])
(not error? trap [set* 'a null])

(not error? trap [set* quote a: () a])
(not error? trap [set* 'a ()])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value]
    e/id = 'no-value
)
