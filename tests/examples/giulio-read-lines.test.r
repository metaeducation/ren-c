; READ-LINES
;
; This is some code written by @giuliolunati that was in the mezzanine prior to
; the existence of GENERATOR and YIELDER and actual YIELD.
;
; It was removed from the mezzanine and preserved as a test.

[
    (giulio-read-lines: func [
        "Makes a generator that yields lines from a file or port"
        return: [action!]
        src [<opt> port! file!]
        :delimiter [blob! char? text! bitset!]
        :keep "Don't remove delimiter"
        :binary "Return BINARY instead of TEXT"
    ][
        if null? src [src: system.ports.input]
        if file? src [src: open src]

        let crlf: charset "^/^M"

        let pos
        let rule: compose2:deep @{{}} either delimiter [
            either keep
            [ [[thru {{delimiter}} | accept (null)] accept pos: <here>] ]
            [ [[to {{delimiter}} | accept (null)] remove {{delimiter}} accept pos: <here>] ]
        ][
            [
                [to crlf | accept (null)] opt some [
                    ["^M" not ahead "^/"]
                    to crlf
                ] {{when not keep ['remove]}} ["^/" | "^M^/"]
                accept pos: <here>
            ]
        ]

        return func [] (bind construct [
            buffer: make blob! 4096
            port: src
        ] compose:deep [
            let data: null
            let eof: 'false
            cycle [
                pos: null
                parse buffer (rule)
                if pos [break]
                (spread if same? src system.ports.input
                    '[data: read port]
                    else
                    '[data: read:part port 4096]
                )
                if empty? opt data [
                    eof: 'true
                    pos: tail of buffer
                    break
                ]
                append buffer data
            ]
            if all [true? eof, empty? buffer] [return done]
            return (opt spread if not binary '[as text!]) take:part buffer pos
        ])
    ]

    giulio-input-lines: redescribe [
        "Makes a generator that yields lines from system.ports.input"
    ](
        specialize read-lines/ [src: null]
    )
    ok)

    (
        test-file: %crlf.txt
        write test-file as blob! --[a^Mb^/c^M^/^/d^/^M^/e^/^/f]--
        ok
    )

    ( === READ-LINES ===
        lines: collect [
            for-each 'l giulio-read-lines test-file [keep l]
        ]
        lines = [-[a^Mb]- -[c]- -[]- -[d]- -[]- -[e]- -[]- -[f]-]
    )
    ( === READ-LINES:BINARY ===
        lines: collect [
            for-each 'l giulio-read-lines:binary test-file [keep l]
        ]
        lines = map-each 'l [-[a^Mb]- -[c]- -[]- -[d]- -[]- -[e]- -[]- -[f]-] [as blob! l]
    )
    ( === READ-LINES:DELIMITER ===
        eol: "^M^/"
        lines: collect [
            for-each 'l giulio-read-lines:delimiter test-file eol [keep l]
        ]
        lines = [-[a^Mb^/c]- -[^/d^/]- -[e^/^/f]-]
    )
    ( === READ-LINES:KEEP ===
        lines: collect [
            for-each 'l giulio-read-lines:keep test-file [keep l]
        ]
        lines = [-[a^Mb^/]- -[c^M^/]- -[^/]- -[d^/]- -[^M^/]- -[e^/]- -[^/]- -[f]-]
    )
    ( === READ-LINES:DELIMITER:KEEP ===
        eol: "^M^/"
        lines: collect [
            for-each 'l giulio-read-lines:delimiter:keep test-file eol [keep l]
        ]
        lines = [-[a^Mb^/c^M^/]- -[^/d^/^M^/]- -[e^/^/f]-]
    )

    (delete test-file, ok)
]
