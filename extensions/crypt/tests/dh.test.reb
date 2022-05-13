; Diffie-Hellman Smoke Test
;
; Because key exchange involves random numbers, testing is a difficult besides
; testing for whether shared secret computation comes out right.  But any test
; is better than none.


; Byte size primes are a good example to study and have work for instructional
; purposes, for those who are trying to understand.  There are no byte-sized
; prime combinations which seem to pass the security issue described here when
; iterated enough times:
;
; http://www.cl.cam.ac.uk/~rja14/Papers/psandqs.pdf
;
; However, the /INSECURE refinement causes DH-GENERATE-KEYPAIR to keep trying
; until it gets a private/public keypair that is deemed "secure" (in as much
; as you can ever get "secure" in 8 bits...so *relative* security!)
(
    byte-primes: [
      2      3      5      7     11     13     17     19     23     29
     31     37     41     43     47     53     59     61     67     71
     73     79     83     89     97    101    103    107    109    113
    127    131    137    139    149    151    157    163    167    173
    179    181    191    193    197    199    211    223    227    229
    233    239    241    251]

    random/seed "Deterministic!"
    repeat 1000 [
        until [
            g: random/only byte-primes
            p: random/only byte-primes
            not any [
               p = 2
               p = 3
               g >= p
            ]
        ]

        modulus: enbin [be + 1] p
        base: enbin [be + 1] g

        repeat 1 [
            mine: dh-generate-keypair/insecure modulus base
            mine.modulus = modulus
            (length of modulus) = length of mine.public-key
            (length of modulus) = length of mine.private-key

            theirs: dh-generate-keypair/insecure modulus base
            theirs.modulus = modulus
            (length of modulus) = length of theirs.public-key
            (length of modulus) = length of theirs.private-key

            my-shared: dh-compute-secret mine theirs.public-key
            their-shared: dh-compute-secret theirs mine.public-key

            if my-shared != their-shared [
                fail [
                    "Key exchange for"
                    mold base mold modulus
                    "did not arrive at the same shared secret"
                ]
            ]
         ]
    ]
    true
)

; Coming up with meaningful further tests is a work in progress, but learning
; at the most basic level of one byte provided many insights...
;
; (ECDHE is a higher priority at this time)
