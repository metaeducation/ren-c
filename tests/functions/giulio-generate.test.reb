; %guilio-generate.test.reb
;
; This was some generator code from @giuliolunati which pre-dated the creation
; of stackless YIELDER and GENERATOR.
;
[(
    /giulio-generate: func [
        "Make a generator"
        return: [action?]
        init [block!] "Init code"
        condition [block! blank!] "While condition"
        iteration [block!] "Step code"
    ][
        let words: make block! 2
        for-each 'x reduce [init condition iteration] [
            if not block? x [continue]
            let w: collect-words:deep:set x
            if not empty? intersect w [count result] [ fail [
                "count: and result: set-words aren't allowed in" mold x
            ]]
            append words w
        ]
        let spec: compose [:reset [block!] <static> (spread unique words) count]
        let body: compose:deep [
            if reset [count: reset return]
            if block? count [
                let result: bind $count count
                count: 1
                return eval result
            ]
            count: me + 1
            let result: (to group! iteration)
            (spread either empty? condition
                [[ return result ]]
                [compose [ return if (to group! condition) [result] ]]
            )
        ]
        let /f: func spec body
        f/reset init
        return :f
    ]

    /read-lines: func [
        "Makes a generator that yields lines from a file or port"
        return: [action?]
        src [~null~ port! file!]
        :delimiter [blob! char? text! bitset!]
        :keep "Don't remove delimiter"
        :binary "Return BINARY instead of TEXT"
    ][
        if null? src [src: system.ports.input]
        if file? src [src: open src]

        let pos
        let rule: compose:deep either delimiter [
            either keep
            [ [thru (delimiter) pos: <here>] ]
            [ [to (delimiter) remove (delimiter) pos: <here>] ]
        ][
            [
                to crlf opt some [
                    ["^M" and not "^/"]
                    to crlf
                ] (if not keep ['remove]) ["^/" | "^M^/"] pos: <here>
            ]
        ]

        return func compose [
            <static> buffer (to group! [make blob! 4096])
            <static> port (groupify src)
        ] compose:deep [
            let crlf: charset "^/^M"
            let data: null
            let eof: 'false
            cycle [
                pos: null
                parse3 buffer (rule)
                if pos [break]
                (spread if same? src system.ports.input
                    '[data: read port]
                    else
                    '[data: read:part port 4096]
                )
                if empty? data [
                    eof: 'true
                    pos: tail of buffer
                    break
                ]
                append buffer data
            ]
            if all [true? eof, empty? buffer] [return null]
            (maybe spread if not binary '[to text!]) take:part buffer pos
        ]
    ]

    input-lines: redescribe [
        {Makes a generator that yields lines from system.ports.input.}
    ](
        specialize read-lines/ [src: null]
    )
)


( { GENERATE }
    { Start with 1 then double while x < 100 }
    {  => 1 2 4 8 16 32 64  }
    for-each 'x sequence: giulio-generate [x: 1] [x < 100] [x: 2 * x] [t: x]
    t = 64
)
( { GENERATE/RESET }
    { restart sequence from 5}
    { => 5, 10, 20, 40, 80 }
    sequence/reset [x: 5]
    for-each 'x :sequence [t: x]
    t = 80
)( { GENERATE, use COUNT }
    { Start with 1, step 2, 3 terms }
    { => 1, 3, 6, 10 }
    for-each 'x sequence: (
        giulio-generate [i: count] [count <= 4] [i: i + count] [t: x]
    )
    t = 10
)
( { GENERATE, no stop }
    { Fibonacci numbers, forever }
    for-each 'x giulio-generate
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
    for-each 'x giulio-generate [primes: mutable [2] n: 2] [count <= 20] [
        cycle [n: n + 1 nop: 'yes for-each 'p primes [
            if (n mod p = 0) [break]
            if (p * p > n) [nop: 'false, break]
        ] if no? nop [break]]
        append primes n
        n
    ] [ t: x ]
    t = 71
)]
