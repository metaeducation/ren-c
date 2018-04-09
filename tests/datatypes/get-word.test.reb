; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! = type of first [:a])
(
    ; context-less get-word
    e: try [do to block! ":a"]
    e/id = 'not-bound
)
(
    unset 'a
    void? :a
)

[#1477 (
    e: trap [load ":/"]
    error? e and (e/id = 'scan-invalid)
)]
(
    e: trap [load "://"]
    error? e and (e/id = 'scan-invalid)
)
(
    e: trap [load ":///"]
    error? e and (e/id = 'scan-invalid)
)
