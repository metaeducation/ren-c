REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: File Related"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]


clean-path: function [
    {Returns new directory path with `//` `.` and `..` processed.}

    file [file! url! text!]
    /only "Do not prepend current directory"
    /dir "Add a trailing / if missing"
][
    file: case [
        any [only, not file? file] [
            copy file
        ]

        #"/" = first file [
            file: next file
            out: next what-dir
            while [
                all [
                    #"/" = first file
                    f: find/tail out #"/"
                ]
            ][
                file: next file
                out: f
            ]
            append clear out file
        ]
    ] else [
        append what-dir file
    ]

    all [dir, not dir? file] then [append file #"/"]

    out: make type of file length of file ; same datatype
    count: 0 ; back dir counter

    parse reverse file [
        some [
            "../" (count: me + 1)
            | "./"
            | "/" (
                any [not file? file, #"/" <> last out] then [
                    append out #"/"
                ]
            )
            | copy f [to "/" | to end] (
                if count > 0 [
                    count: me - 1
                ] else [
                    if not find ["" "." ".."] as text! f [
                        append out f
                    ]
                ]
            )
        ]
        end
    ]

    all [#/ = last out, #/ <> last file] then [
        remove back tail of out
    ]

    reverse out
]


read-line: function [
    {Inputs a line of text from the console (no newline)}

    return: "Null if the input was aborted (e.g. via ESCAPE)"
        [<opt> text!]
    /hide "Mask input with a * character"
][
    if hide [
        fail [
            "READ-LINE/HIDE not yet implemented:"
            https://github.com/rebol/rebol-issues/issues/476
        ]
    ]

    ; !!! R3-Alpha's input port was not opened until first INPUT.  You won't
    ; find a comprehensive document explaining the full ramifications.
    ;
    all [
        port? system/ports/input
        open? system/ports/input
    ] else [
        system/ports/input: open [scheme: 'console]
    ]

    if void? data: read system/ports/input [
        ;
        ; !!! Currently VOID is returned from a port when you read it if
        ; there was a Ctrl-C that happened.  The code does not have permission
        ; to spontanously interrupt any API processing, but we can react
        ; by issuing a THROW manually.
        ;
        HALT
    ]

    all [
        1 = length of data
        escape = as issue! data/1
    ] then [
        ; Input Aborted--this does not try and HALT the program overall like
        ; Ctrl-C, but gives the caller the chance to process NULL as distinct
        ; from the user just hitting enter on an empty line (empty string)
        ;
        return null
    ]

    ; !!! On Windows, reading lines gives CR LF sequences.  It has to be that
    ; the stdio port itself stays "pure" if it is to work with CGI, etc.  This
    ; balance of agnostic byte-level READ/WRITE has to be balanced with the
    ; typical desire to non-agnostically work with strings.  For now, we do
    ; the DELINE here for Windows.
    ;
    line: either 3 = fourth system/version [  ; Windows (32-bit or 64-bit)
        deline data
    ][
        as text! data  ; The data that comes from READ is mutable
    ]

    ; !!! Protocol-wise, at the C level stdio implementations often have a
    ; newline given back with the result (and the lack of a newline can show
    ; your buffer wasn't big enough, so you expand it).  Per-key console
    ; implementation can do differently.  Should standardize.
    ;
    return trim/with line newline
]


ask: function [
    {Ask the user for input}

    return: "Null if the input was aborted (via ESCAPE, Ctrl-D, etc.)"
        [<opt> any-value!]
    question "Prompt to user, datatype to request, or dialect block"
        [block! text! datatype!]
    /hide "mask input with * (Rebol2 feature, not yet implemented)"
    ; !!! What about /MULTILINE ?
][
    if hide [
        fail [
            "ASK/HIDE not yet implemented:"
            https://github.com/rebol/rebol-issues/issues/476
        ]
    ]

    ; This is a limited implementation just to get the ball rolling; could
    ; do much more: https://forum.rebol.info/t/1124
    ;
    prompt: _
    type: text!
    switch type of question [
        text! [prompt: question]  ; `ask "Input:"` doesn't filter type
        datatype! [type: question]  ; `ask text!` has no prompt (like INPUT)
        block! [
            parse question [
                opt [set prompt: text!]
                opt [set word: word! (type: ensure datatype! get word)]
            ] else [
                fail "ASK currently only supports [{Prompt:} datatype!]"
            ]
        ]
        fail
    ]

    ; Loop indefinitely so long as the input can't be converted to the type
    ; requested (and there's no cancellation).  Print prompt each time.  Note
    ; that if TEXT! is requested, conversion cannot fail.
    ;
    cycle [
        if prompt [
            write-stdout prompt
            write-stdout space  ; space after prompt is implicit
        ]

        line: read-line else [
            ;
            ; NULL signals a "cancel" was recieved by reading standard input.
            ; This is distinct from HALT (would not return from READ-STDIN).
            ; Whether or not ASK should use "soft failure" via null, just keep
            ; asking, or somehow disable cancellation is open for debate.
            ;
            return null
        ]

        ; The original ASK would TRIM the output, so no leading or trailing
        ; space.  This assumes that is the caller's responsibility.
        ;
        if type = text! [return line]

        e: trap [return to type line]

        ; !!! The actual error trapped during the conversion may contain more
        ; useful information than just saying "** Invalid input".  But there's
        ; no API for a "light" printing of errors.  Scrub out all the extra
        ; information from the error so it isn't as verbose.
        ;
        e/file: _
        e/line: _
        e/where: _
        e/near: _
        print [e]

        ; Keep cycling...
    ]
]


confirm: function [
    {Confirms a user choice}

    return: [logic!]
    question "Prompt to user"
        [any-series!]
    /with [text! block!]
][
    with: default [["y" "yes"] ["n" "no"]]

    all [
        block? with
        length of with > 2

        fail 'with [
            "maximum 2 arguments allowed for with [true false]"
            "got:" mold with
        ]
    ]

    response: ask question

    return case [
        empty? with [true]
        text? with [did find/match response with]
        length of with < 2 [did find/match response first with]
        find first with response [true]
        find second with response [false]
    ]
]


list-dir: function [
    "Print contents of a directory (ls)."

    return: <void>
    'path [<end> file! word! path! text!]
        "Accepts %file, :variables, and just words (as dirs)"
    /l "Line of info format"
    /f "Files only"
    /d "Dirs only"
;   /t "Time order"
    /r "Recursive"
    /i "Indent"
        [any-value!]
][
    i: default [""]

    save-dir: what-dir

    if not file? save-dir [
        fail ["No directory listing protocol registered for" save-dir]
    ]

    switch type of :path [
        null []  ; Stay here
        file! [change-dir path]
        text! [change-dir local-to-file path]
        word! path! [change-dir to-file path]
    ]

    if r [l: true]
    if not l [l: make text! 62] ; approx width

    files: attempt [read %./] else [
        print ["Not found:" :path]
        change-dir save-dir
        return
    ]

    for-each file files [
        any [
            all [f, dir? file]
            all [d, not dir? file]
        ] then [
            continue
        ]

        if text? l [
            append l file
            append/dup l #" " 15 - remainder length of l 15
            if greater? length of l 60 [print l clear l]
        ] else [
            info: get (words of query file)
            change info second split-path info/1
            printf [i 16 -8 #" " 24 #" " 6] info
            if all [r, dir? file] [
                list-dir/l/r/i :file join i "    "
            ]
        ]
    ]

    all [text? l, not empty? l] then [print l]

    change-dir save-dir
]


undirize: function [
    {Returns a copy of the path with any trailing "/" removed.}

    return: [file! text! url!]
    path [file! text! url!]
][
    path: copy path
    if #"/" = last path [clear back tail of path]
    path
]


in-dir: function [
    "Evaluate a block while in a directory."
    return: [<opt> any-value!]
    dir [file!]
        "Directory to change to (changed back after)"
    block [block!]
        "Block to evaluate"
][
    old-dir: what-dir
    change-dir dir

    ; You don't want the block to be done if the change-dir fails, for safety.

    do block  ; return result

    elide (change-dir old-dir)
]


to-relative-file: function [
    {Returns relative portion of a file if in subdirectory, original if not.}

    return: [file! text!]
    file "File to check (local if text!)"
        [file! text!]
    /no-copy "Don't copy, just reference"
    /as-rebol "Convert to Rebol-style filename if not"
    /as-local "Convert to local-style filename if not"
][
    if text? file [ ; Local file
        comment [
            ; file-to-local drops trailing / in R2, not in R3
            if tmp: find/match file file-to-local what-dir [
                file: next tmp
            ]
        ]
        file: maybe find/match file file-to-local what-dir
        if as-rebol [
            file: local-to-file file
            no-copy: true
        ]
    ] else [
        file: maybe find/match file what-dir
        if as-local [
            file: file-to-local file
            no-copy: true
        ]
    ]

    return either no-copy [file] [copy file]
]


; !!! Probably should not be in the "core" mezzanine.  But to make it easier
; for people who seem to be unable to let go of the tabbing/CR past, this
; helps them turn their files into sane ones :-/
;
; http://www.rebol.com/r3/docs/concepts/scripts-style.html#section-4
;
detab-file: function [
    "detabs a disk file"

    return: <void>
    filename [file!]
][
    write filename detab to text! read filename
]

; temporary location
set-net: function [
    {sets the system/user/identity email smtp pop3 esmtp-usr esmtp-pass fqdn}

    return: <void>
    bl [block!]
][
    if 6 <> length of bl [fail "Needs all 6 parameters for set-net"]
    set (words of system/user/identity) bl
]
