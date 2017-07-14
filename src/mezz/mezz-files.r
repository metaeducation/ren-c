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
    "Returns new directory path with `//` `.` and `..` processed."
    file [file! url! string!]
    /only
        "Do not prepend current directory"
    /dir
        "Add a trailing / if missing"
][
    file: case [
        any [only | not file? file] [
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

    if all [dir | not dir? file] [append file #"/"]

    out: make type-of file length-of file ; same datatype
    cnt: 0 ; back dir counter

    parse reverse file [
        some [
            "../" (++ cnt)
            | "./"
            | #"/" (
                if any [not file? file | #"/" <> last out] [append out #"/"]
            )
            | copy f [to #"/" | to end] (
                either cnt > 0 [
                    -- cnt
                ][
                    unless find ["" "." ".."] as string! f [append out f]
                ]
            )
        ]
    ]

    if all [#"/" = last out | #"/" <> last file] [remove back tail out]
    reverse out
]


input: function [
    {Inputs a string from the console. New-line character is removed.}
    return: [string!]
;   /hide
;       "Mask input with a * character"
][
    if any [
        not port? system/ports/input
        not open? system/ports/input
    ][
        system/ports/input: open [scheme: 'console]
    ]

    line: to-string read system/ports/input
    trim/with line newline
    line
]


ask: function [
    "Ask the user for input."
    return: [string!]
    question [any-series!]
        "Prompt to user"
    /hide
        "mask input with *"
][
    print/only either block? question [spaced question] [question]
    trim either hide [input/hide] [input]
]


confirm: function [
    "Confirms a user choice."
    return: [logic!]
    question [any-series!]
        "Prompt to user"
    /with
    choices [string! block!]
][
    if all [block? :choices | 2 < length-of choices] [
        cause-error 'script 'invalid-arg adjoin "maximum 2 arguments allowed (true/false) " mold choices
    ]

    response: ask question

    unless with [choices: [["y" "yes"] ["n" "no"]]]

    to-value case [
        empty? choices [true]
        string? choices [find?/match response choices]
        2 > length-of choices [find?/match response first choices]
        find? first choices response [true]
        find? second choices response [false]
    ]
]


list-dir: procedure [
    "Print contents of a directory (ls)."
    'path [<end> file! word! path! string!]
        "Accepts %file, :variables, and just words (as dirs)"
    /l "Line of info format"
    /f "Files only"
    /d "Dirs only"
;   /t "Time order"
    /r "Recursive"
    /i "Indent"
        indent
][
    indent: default ""

    save-dir: what-dir

    unless file? save-dir [
        fail ["No directory listing protocol registered for" save-dir]
    ]

    switch type-of :path [
        _ [] ; Stay here
        :file! [change-dir path]
        :string! [change-dir to-rebol-file path]
        :word! :path! [change-dir to-file path]
    ]

    if r [l: true]
    unless l [l: make string! 62] ; approx width    
    
    if not (files: attempt [read %./]) [
        print ["Not found:" :path]
        change-dir save-dir
        leave
    ]
    
    for-each file files [
        if any [
            all [f | dir? file]
            all [d | not dir? file]
        ][continue]

        either string? l [
            append l file
            append/dup l #" " 15 - remainder length-of l 15
            if greater? length-of l 60 [print l clear l]
        ][
            info: get query file
            change info second split-path info/1
            printf [indent 16 -8 #" " 24 #" " 6] info
            if all [r | dir? file] [
                list-dir/l/r/i :file join-of indent "    "
            ]
        ]
    ]
    
    if all [string? l | not empty? l] [print l]
    
    change-dir save-dir
]


undirize: function [
    {Returns a copy of the path with any trailing "/" removed.}
    return: [file! string! url!]
    path [file! string! url!]
][
    path: copy path
    if #"/" = last path [clear back tail path]
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

    also do block change-dir old-dir
]


to-relative-file: function [
    "Returns relative portion of a file if in subdirectory, original if not."
    return: [file! string!]
    file [file! string!]
        "File to check (local if string!)"
    /no-copy
        "Don't copy, just reference"
    /as-rebol
        "Convert to REBOL-style filename if not"
    /as-local
        "Convert to local-style filename if not"
][
    either string? file [ ; Local file
        ; Note: to-local-file drops trailing / in R2, not in R3
        ; if tmp: find/match file to-local-file what-dir [file: next tmp]
        file: any [find/match file to-local-file what-dir | file]
        if as-rebol [
            file: to-rebol-file file
            no-copy: true
        ]
    ][
        file: any [find/match file what-dir | file]
        if as-local [
            file: to-local-file file
            no-copy: true
        ]
    ]

    unless no-copy [file: copy file]
    
    file
]


; !!! Probably should not be in the "core" mezzanine.  But to make it easier
; for people who seem to be unable to let go of the tabbing/CR past, this
; helps them turn their files into sane ones :-/
;
; http://www.rebol.com/r3/docs/concepts/scripts-style.html#section-4
;
detab-file: procedure [
    "detabs a disk file"
    filename [file!]
][
    write filename detab to string! read filename
]

; temporary location
set-net: procedure [
    {sets the system/user/identity email smtp pop3 esmtp-usr esmtp-pass fqdn}
    bl [block!]
][
    if 6 <> length-of bl [fail "Needs all 6 parameters for set-net"]
    set words-of system/user/identity bl
]
