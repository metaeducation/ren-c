; datatypes/unset.r
(void? ())
(blank? type of ())
(not void? 1)

[#68
    (void? trap/with [a: ()] [_])
]

(error? trap [a: () a])
(not error? trap [set/only 'a ()])

(
    a-value: 10
    unset 'a-value
    e: trap [a-value]
    e/id = 'no-value
)
