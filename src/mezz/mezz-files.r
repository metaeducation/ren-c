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
        only or [not file? file] [
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

    if dir and [not dir? file] [append file #"/"]

    out: make type of file length of file ; same datatype
    count: 0 ; back dir counter

    parse reverse file [
        some [
            "../" (count: me + 1)
            | "./"
            | #"/" (
                if (not file? file) or [#"/" <> last out] [
                    append out #"/"
                ]
            )
            | copy f [to #"/" | to end] (
                if count > 0 [
                    count: me - 1
                ] else [
                    if not find ["" "." ".."] as text! f [
                        append out f
                    ]
                ]
            )
        ]
    ]

    if (#"/" = last out) and [#"/" <> last file] [
        remove back tail of out
    ]

    reverse out
]


ask: function [
    {Inputs a line of text from the console. New-line character is removed.}

    return: "Null if the input was aborted (via ESCAPE, Ctrl-D, etc.)"
        [~null~ text!]
    question "Prompt to user"
        [datatype! any-series!]
][
    if datatype? question [
        if question <> text! [
            fail "ASK only supports ASK TEXT! in bootstrap build"
        ]
    ]
    else [
        write-stdout either block? question [spaced question] [question]
    ]

    all [
        port? system/ports/input
        open? system/ports/input
    ] or [
        system/ports/input: open [scheme: 'console]
    ]

    data: read system/ports/input
    if 0 = length of data [
        ;
        ; !!! Zero-length data is the protocol being used to signal a halt in
        ; the (deprecated) Host OS layer.  All those who READ from the
        ; INPUT port shouldn't have to know this and retransmit the halt, so
        ; it should probably be something the READ itself does.
        ;
        halt
    ]

    all [
        1 = length of data
        escape = to-char data/1
    ] then [
        ; Input Aborted (e.g. Ctrl-D on Windows, ESC on POSIX)--this does not
        ; try and HALT the program overall like Ctrl-C, but gives the caller
        ; the chance to process NULL and realize it as distinct from the user
        ; just hitting enter on an empty line (empty string)
        ;
        return null;
    ]

    line: to-text data
    trim/with line newline
    line
]


confirm: function [
    {Confirms a user choice.}

    return: [logic!]
    question [any-series!]
        "Prompt to user"
    /with
    choices [text! block!]
][
    choices: default [["y" "yes"] ["n" "no"]]

    if block? choices and [length of choices > 2] [
        fail/where [
            "maximum 2 arguments allowed for choices [true false]"
            "got:" mold choices
        ] 'choices
    ]

    response: ask question

    return case [
        empty? choices [true]
        text? choices [did find/match response choices]
        length of choices < 2 [did find/match response first choices]
        find first choices response [true]
        find second choices response [false]
    ]
]


list-dir: function [
    "Print contents of a directory (ls)."

    return: [~]
    'path [<end> file! word! path! text!]
        "Accepts %file, :variables, and just words (as dirs)"
    /l "Line of info format"
    /f "Files only"
    /d "Dirs only"
;   /t "Time order"
    /r "Recursive"
    /i "Indent"
        indent
][
    indent: default [""]

    save-dir: what-dir

    if not file? save-dir [
        fail ["No directory listing protocol registered for" save-dir]
    ]

    switch type of :path [
        null [] ; Stay here
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
            f and [dir? file]
            d and [not dir? file]
        ] then [
            continue
        ]

        if text? l [
            append l file
            append/dup l #" " 15 - remainder length of l 15
            if greater? length of l 60 [print l clear l]
        ] else [
            info: get (words of query file)
            split-path/file info/1 the filename:
            change info filename
            printf [indent 16 -8 #" " 24 #" " 6] info
            if all [r | dir? file] [
                list-dir/l/r/i :file join indent "    "
            ]
        ]
    ]

    if (text? l) and [not empty? l] [print l]

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
    return: [~null~ any-value!]
    dir [file!]
        "Directory to change to (changed back after)"
    block [block!]
        "Block to evaluate"
][
    old-dir: what-dir
    change-dir dir

    ; You don't want the block to be done if the change-dir fails, for safety.

    do block ;-- return result

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
        file: any [
            find/match file file-to-local what-dir
            file
        ]
        if as-rebol [
            file: local-to-file file
            no-copy: true
        ]
    ] else [
        file: any [
            find/match file what-dir
            file
        ]
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

    return: [~]
    filename [file!]
][
    write filename detab to text! read filename
]

; temporary location
set-net: function [
    {sets the system/user/identity email smtp pop3 esmtp-usr esmtp-pass fqdn}

    return: [~]
    bl [block!]
][
    if 6 <> length of bl [fail "Needs all 6 parameters for set-net"]
    set (words of system/user/identity) bl
]
