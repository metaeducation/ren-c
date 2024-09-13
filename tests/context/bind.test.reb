; functions/context/bind.r

~not-bound~ !! (eval make block! ":a")

[#50
    (null? binding of to word! "zzz")
]
; BIND works 'as expected' in object spec
[#1549 (
    b1: [self]
    ob: make object! [
        b2: [self]
        set $a same? first b2 first bind/copy b1 binding of $b2
    ]
    a
)]
; BIND works 'as expected' in function body
[#1549 (
    b1: [self]
    f: lambda [<local> b2] [
        b2: [self]
        same? first b2 first bind/copy b1 binding of $b2
    ]
    f
)]
; BIND works 'as expected' in REPEAT body
[#1549 (
    b1: [self]
    count-up i 1 [
        b2: [self]
        same? first b2 first bind/copy b1 binding of $i
    ]
)]
[#892 #216
    (y: 'x reeval unrun lambda [<local> x] [x: okay get bind y binding of $x])
]

[#2086 (
    bind next block: [a a] binding of use [a] [$a]
    same? $a first block
)]

[#1893 (
    word: reeval unrun lambda [x] [$x] 1
    same? word bind 'x binding of word
)]
