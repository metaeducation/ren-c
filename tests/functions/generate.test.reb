; GENERATE

( { GENERATE } 
    { Start with 1 then double while x < 100 }
    {  => 1 2 4 8 16 32 64  }
    for-each x sequence: generate [x: 1] [x < 100] [x: 2 * x] [t: x]
    t = 64
)
( { GENERATE/RESET }
    { restart sequence from 5}
    { => 5, 10, 20, 40, 80 }
    sequence/reset [x: 5]
    for-each x :sequence [t: x]
    t = 80
)( { GENERATE, use COUNT }
    { Start with 1, step 2, 3 terms }
    { => 1, 3, 6, 10 }
    for-each x sequence: generate [i: count] [count <= 4] [i: i + count] [t: x]
    t = 10
)
( { GENERATE, no stop }
    { Fibonacci numbers, forever }
    for-each x generate
        [a: b: 1]
        _
        [c: a + b a: b b: c]
    [
        t: x
        if x >= 10 [break] { <- manual break }
    ]
    t = 13
)
( { GENERATE, 20 prime numbers }
    for-each x generate [primes: mutable [2] n: 2] [count <= 20] [
        forever [n: n + 1 nop: true for-each p primes [
            if (n mod p = 0) [break]
            if (p * p > n) [nop: false break]
        ] if not nop [break]]
        append primes n
        n
    ] [ t: x ]
    t = 71
)
