REBOL [
    Title: "Process Extension"
    Name: Process
    Type: Module
    Options: [isolate]
    Version: 1.0.0
    License: {Apache 2.0}
]

; It's desirable to do as much usermode logic as possible, to reduce the
; amount of C code that CALL has to run.  So things like transforming any
; FILE! into local paths are done here.
;
call*: adapt :call-internal* [
    command: switch type of command [
        text! [
            ; A TEXT! is passed through as-is, and will be interpreted by
            ; the shell (e.g. `sh -c your text` or `cmd.exe /C your text`)
            ;
            command
        ]
        file! [
            ; We change a FILE! to a TEXT! of its local form -but- enclose it
            ; in a length-1 argv[] block.  That's because CALL-INTERNAL* will
            ; treat a TEXT! as a line to pass and be interpreted by the shell.
            ; Hence if the filename contained spaces, it would be broken up.
            ; Making it an element of an argv[] array keeps it atomic.
            ;
            reduce [file-to-local command]
        ]
        block! [
            if empty? command [  ; !!! should this be a no-op?
                fail "Empty argv[] block passed to CALL"
            ]

            ; We COMPOSE the command for convenience.  If you use WORD!s like
            ; `--do` or tuples like `foo.bar` or paths like `/Wd2070` they are
            ; turned into their text equivalents.  This lets you write code in
            ; the CALL block that looks a bit more like a shell invocation.
            ;
            map-each arg compose command [
                switch type of arg [
                    text! [arg]  ; pass through as is
                    file! [file-to-local arg]
                    word! [as text! arg]
                    path! [to text! arg]
                    tuple! [to text! arg]
                    integer! [to text! arg]

                    fail ["invalid item in argv[] block for CALL:" arg]
                ]
            ]
        ]
        fail  ; unreachable (parameter was typechecked)
    ]
]

; The Atronix CALL implementation was asynchronous by default, launching a
; process and returning immediately.  However, use of parameters that would
; feed it input or output could make it /WAIT implicitly.
;
; The long term goal would be to have some kind of call PORT! which could be
; generated, and then spoken to to feed I/O programmatically a bit at a time.
; (Similar to Tcl's EXPECT, for instance.)  In lieu of that design, this goes
; ahead and keeps the asynchronous behavior in a lower level and chooses to
; /WAIT by default.
;
call: :call*/wait

parse-command-to-argv*: func [
    {Helper for when POSIX gets a TEXT! and the /SHELL refinement not used}

    return: [block!]
    command [text!]
][
    let quoted-shell-item-rule: [  ; Note: ANY because "" is legal as an arg
        while [{\"} | not {"} skip]  ; escaped quotes and nonquotes
    ]
    let unquoted-shell-item-rule: [some [not space skip]]

    let result: parse command [
        collect [
            while [
                while space
                [
                    {"} keep quoted-shell-item-rule {"}
                    | keep unquoted-shell-item-rule
                ]
            ]
            while space
        ]
    ] else [
        fail [
            "Could not parse command line into argv[] block." LF
            "Use CALL/SHELL to defer the shell to parse, or if you believe"
            "the command line is valid then help fix PARSE-COMMAND-TO-ARGV*"
        ]
    ]
    for-each item result [replace/all item {\"} {"}]
    return result
]


argv-block-to-command*: func [
    {Helper for when Windows gets an argv BLOCK! and needs a command line}

    return: [text!]
    argv [block!]
][
    return spaced map-each arg argv [
        any [
            find arg space
            find arg {"}
        ] then [  ; have to put it in quotes, but also escape any quotes
            arg: copy arg
            replace/all arg {"} {\"}
            insert arg {"}
            append arg {"}
        ]
        arg
    ]
]


; CALL is a native built by the C code, BROWSE depends on using that, as well
; as some potentially OS-specific detection on how to launch URLs (e.g. looks
; at registry keys on Windows)

browse*: func [
    "Open web browser to a URL or local file."

    return: <none>
    location [<blank> url! file!]
][
    print "Opening web browser..."

    ; Note that GET-OS-BROWSERS uses the Windows registry convention of having
    ; %1 be what needs to be substituted.  This may not be ideal, it was just
    ; easy to do rather than have to add processing on the C side.  Review.
    ;
    for-each template get-os-browsers [
        let command: replace/all (copy template) "%1" either file? location [
            file-to-local location
        ][
            location
        ]
        trap [
            call/shell command  ; open with no /WAIT, so don't use CALL*
            return
        ] then [
            ; Just keep trying
        ]
    ]
    fail "Could not open web browser"
]

hijack :browse :browse*

sys/export [call call*]
