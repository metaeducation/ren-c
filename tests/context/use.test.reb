; functions/context/use.r

; local word test
(
    a: 1
    use [a] [a: 2]
    a == 1
)
(
    a: 1
    error? trap [use 'a [a: 2]]
    a == 1
)

; initialization (lack of)
(a: 10 all [use [a] [null? :a] a == 10])
(use [a] [not set? 'a])

; BREAK out of USE
(
    null? loop 1 [
        use [a] [break]
        2
    ]
)
; THROW out of USE
(
    1 == catch [
        use [a] [throw 1]
        2
    ]
)
; "error out" of USE
(
    error? trap [
        use [a] [1 / 0]
        2
    ]
)
; RETURN out of USE
[#539 (
    f: func [] [
        use [a] [return 1]
        2
    ]
    1 == f
)]

; USE shares mechanics with FOR-EACH and hence does not allow expansion.
; This particular nuance with 'SELF from #2076 thus no longer arises
(
    o: binding of use [x] ['x]
    e: trap [append o 'self]
    error? e and [e/id is 'Locked-Series]
)
