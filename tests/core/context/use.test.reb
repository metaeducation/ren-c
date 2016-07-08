; functions/context/use.r
; local word test
[
    a: 1
    use [a] [a: 2]
    a = 1
]
[
    a: 1
    error? try [use 'a [a: 2]]
    a = 1
]
; initialization (lack of)
[a: 10 all [use [a] [void? :a] a = 10]]
; BREAK out of USE
[
    1 = loop 1 [
        use [a] [break/return 1]
        2
    ]
]
; THROW out of USE
[
    1 = catch [
        use [a] [throw 1]
        2
    ]
]
; "error out" of USE
[
    error? try [
        use [a] [1 / 0]
        2
    ]
]
; bug#539
; RETURN out of USE
[
    f: func [] [
        use [a] [return 1]
        2
    ]
    1 = f
]
