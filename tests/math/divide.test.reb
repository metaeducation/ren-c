; functions/math/divide.r
(1 == divide -2147483648 -2147483648)
(2 == divide -2147483648 -1073741824)
(1073741824 == divide -2147483648 -2)
<32bit>
(error? trap [divide -2147483648 -1])
(error? trap [divide -2147483648 0])
(-2147483648 == divide -2147483648 1)
(-1073741824 == divide -2147483648 2)
(-2 == divide -2147483648 1073741824)
(0.5 == divide -1073741824 -2147483648)
(1 == divide -1073741824 -1073741824)
(536870912 == divide -1073741824 -2)
(1073741824 == divide -1073741824 -1)
(error? trap [divide -1073741824 0])
(-1073741824 == divide -1073741824 1)
(-536870912 == divide -1073741824 2)
(-1 == divide -1073741824 1073741824)
(1 == divide -2 -2)
(2 == divide -2 -1)
(error? trap [divide -2 0])
(-2 == divide -2 1)
(-1 == divide -2 2)
(0.5 == divide -1 -2)
(1 == divide -1 -1)
(error? trap [divide -1 0])
(-1 == divide -1 1)
(-0.5 == divide -1 2)
(0 == divide 0 -2147483648)
(0 == divide 0 -1073741824)
(0 == divide 0 -2)
(0 == divide 0 -1)
(error? trap [divide 0 0])
(0 == divide 0 1)
(0 == divide 0 2)
(0 == divide 0 1073741824)
(0 == divide 0 2147483647)
(-0.5 == divide 1 -2)
(-1 == divide 1 -1)
(error? trap [divide 1 0])
(1 == divide 1 1)
(0.5 == divide 1 2)
(-1 == divide 2 -2)
(-2 == divide 2 -1)
(error? trap [divide 2 0])
(2 == divide 2 1)
(1 == divide 2 2)
(-0.5 == divide 1073741824 -2147483648)
(-1 == divide 1073741824 -1073741824)
(-536870912 == divide 1073741824 -2)
(-1073741824 == divide 1073741824 -1)
(error? trap [divide 1073741824 0])
(1073741824 == divide 1073741824 1)
(536870912 == divide 1073741824 2)
(1 == divide 1073741824 1073741824)
(-1 == divide 2147483647 -2147483647)
(-1073741823.5 == divide 2147483647 -2)
(-2147483647 == divide 2147483647 -1)
(error? trap [divide 2147483647 0])
(2147483647 == divide 2147483647 1)
(1073741823.5 == divide 2147483647 2)
(1 == divide 2147483647 2147483647)
(10.0 == divide 1 0.1)
(10.0 == divide 1.0 0.1)
(10x10 == divide 1x1 0.1)
[#1974
    (10.10.10 == divide 1.1.1 0.1)
]

; division uses "full precision"
("$1.0000000000000000000000000" = mold $1 / $1)
("$1.0000000000000000000000000" = mold $1 / $1.0)
("$1.0000000000000000000000000" = mold $1 / $1.000)
("$1.0000000000000000000000000" = mold $1 / $1.000000)
("$1.0000000000000000000000000" = mold $1 / $1.000000000)
("$1.0000000000000000000000000" = mold $1 / $1.000000000000)
("$1.0000000000000000000000000" = mold $1 / $1.0000000000000000000000000)
("$0.10000000000000000000000000" = mold $1 / $10)
("$0.33333333333333333333333333" = mold $1 / $3)
("$0.66666666666666666666666667" = mold $2 / $3)


; Configurability of the `/` is special, because it is a PATH! and not a WORD!
; The synonym -SLASH-1- is used to refer to it in "word-space", and it can
; be bound using this.  Note that because it is a PATH!, `/` won't get
; collected by routines like COLLECT-WORDS or similar...so this applies to
; specific intents for overloading, vs. thinking of `/` as being truly
; "word-equivalent"
;
[#2516 (
    code: [1 / 2]
    obj: make object! [
        -slash-1-: enfix func [a b] [
            return reduce '(b a)
        ]
    ]
    0.5 = do code
    bind code obj
    '(2 1) = do code
)]
