; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! == type of first [:a])
(
    ; context-less get-word
    e: trap [do make block! ":a"]
    e/id is 'Not-Bound
)
(
    unset 'a
    null? :a
)

[#1477 (
    e: trap [load ":/"]
    error? e and [e/id is 'Scan-Invalid]
)]
(
    e: trap [load "://"]
    error? e and [e/id is 'Scan-Invalid]
)
(
    e: trap [load ":///"]
    error? e and [e/id is 'Scan-Invalid]
)
