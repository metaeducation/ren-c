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


file-to-local*: specialize 'file-to-local [only: true]
local-to-file*: specialize 'local-to-file [only: true]

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

    out: make type of file length of file ; same datatype
    count: 0 ; back dir counter

    parse reverse file [
        some [
            "../" (count: ++ 1)
            | "./"
            | #"/" (
                if any [not file? file | #"/" <> last out] [append out #"/"]
            )
            | copy f [to #"/" | to end] (
                either count > 0 [
                    count: -- 1
                ][
                    unless find ["" "." ".."] as string! f [append out f]
                ]
            )
        ]
    ]

    if all [#"/" = last out | #"/" <> last file] [remove back tail of out]
    reverse out
]


input: function [
    {Inputs a string from the console. New-line character is removed.}

    return: [string! blank!]
        {Blank if the input was aborted via ESC}
;   /hide
;       "Mask input with a * character"
][
    if any [
        not port? system/ports/input
        not open? system/ports/input
    ][
        system/ports/input: open [scheme: 'console]
    ]

    data: read system/ports/input
    if 0 = length of data [
        ;
        ; !!! While zero-length data is the protocol being used to signal a
        ; halt in the (deprecated) Host OS layer, even in more ideal
        ; circumstances it is probably bad to try to get INPUT to be emitting
        ; that HALT.
        ;
        fail "Signal for Ctrl-C got to INPUT function somehow."
    ]

    if all [
        1 = length of data
        escape = to-char data/1
    ][
        ; Input Aborted (e.g. Ctrl-D on Windows, ESC on POSIX)--this does not
        ; try and HALT the program overall, but gives the caller the chance
        ; to process the BLANK! and realize it as distinct from the user
        ; just hitting enter on an empty line (empty string)
        ;
        return blank;
    ]

    line: to-string data
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
    return: [logic! blank!]
    question [any-series!]
        "Prompt to user"
    /with
    choices [string! block!]
][
    if all [block? :choices | 2 < length of choices] [
        cause-error 'script 'invalid-arg join-of "maximum 2 arguments allowed for choices [true false] got: " mold choices
    ]

    response: ask question

    unless with [choices: [["y" "yes"] ["n" "no"]]]

    to-logic case [
        empty? choices [true]
        string? choices [find/match response choices]
        2 > length of choices [find/match response first choices]
        find first choices response [true]
        find second choices response [false]
    ] else [
         false
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
    indent: default [""]

    save-dir: what-dir

    unless file? save-dir [
        fail ["No directory listing protocol registered for" save-dir]
    ]

    switch type of :path [
        _ [] ; Stay here
        (file!) [change-dir path]
        (string!) [change-dir local-to-file path]
        (word!) (path!) [change-dir to-file path]
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
            append/dup l #" " 15 - remainder length of l 15
            if greater? length of l 60 [print l clear l]
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

    do block also-do [change-dir old-dir]
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
        ; Note: file-to-local drops trailing / in R2, not in R3
        ; if tmp: find/match file file-to-local what-dir [file: next tmp]
        file: any [find/match file file-to-local what-dir | file]
        if as-rebol [
            file: local-to-file file
            no-copy: true
        ]
    ][
        file: any [find/match file what-dir | file]
        if as-local [
            file: file-to-local file
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
    if 6 <> length of bl [fail "Needs all 6 parameters for set-net"]
    set (words of system/user/identity) bl
]
