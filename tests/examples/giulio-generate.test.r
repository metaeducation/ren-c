; %guilio-generate.test.r
;
; This was some generator code from @giuliolunati which pre-dated the creation
; of stackless YIELDER and GENERATOR.
;
; It is preserved as a test (it had to be changed to use the DONE ERROR! signal
; instead of NULL, because functions like FOR-EACH and MAP-EACH now treat
; null as an ordinary generated state, not the end of the generator.)

[(
    giulio-generate: func [
        "Make a generator"
        return: [action!]
        init [block!] "Init code"
        condition [<opt> block!] "While condition"
        iteration [block!] "Step code"
    ][
        let words: make block! 2
        reduce-each 'x [init condition iteration] [
            if not x [continue]
            let w: collect-words:deep:set x
            if not empty? intersect w [count result] [ panic [
                "count: and result: set-words aren't allowed in" mold x
            ]]
            append words spread w
        ]
        let spec: compose [:reset [block!]]
        let obj: construct compose [
            (spread map-each 'w unique words [setify w]) count: ~
        ]
        let body: bind obj compose:deep [
            if reset [count: bind (obj) reset, return ~]
            if block? count [  ; we'll turn the variable into an integer...
                let result: bind @count count  ; and make block use that var
                count: 1  ; see, now it's an integer!
                return eval result
            ]
            count: me + 1
            let result: (bind obj as group! iteration)
            (spread either not condition
                [[ return result ]]
                [compose [
                    return either (bind obj as group! condition) [result] [done]
                ]]
            )
        ]
        let f: func spec body
        f:reset bind obj init
        return f/
    ]
    ok
)


(
    === GENERATE ===
    ; Start with 1 then double while x < 100

    sequence: giulio-generate [x: 1] [x < 100] [x: 2 * x]
    [1 2 4 8 16 32 64] = map-each 'x sequence/ [x]
)(
    === GENERATE:RESET ===
    ; restart sequence from 5

    sequence:reset [x: 5]
    [5 10 20 40 80] = map-each 'x sequence/ [x]
)(
    === GENERATE, use COUNT ===
    ; Start with 1, step 2, 3 terms

    sequence: giulio-generate [i: count] [count <= 4] [i: i + count]
    [1 3 6 10] = map-each 'x sequence/ [x]
)(
    === GENERATE, no stop ===
    ; Fibonacci numbers, forever

    sequence: giulio-generate [a: b: 1] ^ghost [c: a + b a: b b: c]
    [1 2 3 5 8 13] = collect [for-each 'x sequence/ [
        keep x
        if x >= 10 [break]  ;  <- manual break
    ]]
)
(
    === GENERATE, 20 prime numbers ===

    sequence: giulio-generate [primes: mutable [2] n: 2] [count <= 20] [
        cycle [
            n: n + 1
            let nop: 'yes
            for-each 'p primes [
                if (n mod p = 0) [break]
                if (p * p > n) [nop: 'no, break]
            ]
            if no? nop [break]
        ]
        append primes n
        n
    ]
    [2 3 5 7 11 13 17 19 23 29 31 37 41 43 47 53 59 61 67 71] = (
        map-each 'x sequence/ [x]
    )
)]
