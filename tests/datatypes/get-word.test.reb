; datatypes/get-word.r
(get-word? first [:a])
(not get-word? 1)
(get-word! = type of first [:a])
(
    ; context-less get-word
    e: sys/util/rescue [do make block! ":a"]
    e/id = 'not-bound
)
(
    a: null
    null? :a
)

[#1477 (
    e: sys/util/rescue [load ":/"]
    error? e and [e/id = 'scan-invalid]
)]
(
    e: sys/util/rescue [load "://"]
    error? e and [e/id = 'scan-invalid]
)
(
    e: sys/util/rescue [load ":///"]
    error? e and [e/id = 'scan-invalid]
)
