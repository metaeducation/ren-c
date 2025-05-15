Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: File Related"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

decode-url: sys.util.decode-url/


; 1. TAG! is a shorthand for getting files relative to the path of the
;    currently running script.
;
;    !!! This has strange interactions if you have a function that gets called
;    back after a script has finished, and it still wants to fetch resources
;    relative to its original location.  These issues are parallel to that of
;    using the current working directory, so one should be cautious.
;
; 2. @WORD! like `@tool` gets translated into a URL!.  The list is itself
;    loaded from the internet, URL is in `system.locale.library.utilities`.
;
;    !!! As the project matures, this would have to come from a curated list,
;    not just links on individuals' websites.  There should also be some kind
;    of local caching facility.
;
clean-path: func [
    "Returns new directory path with `.` and `..` processed"

    return: [file! url! text!]
    path [file! url! text! tag! @word!]
    :only "Do not prepend current directory"
    :dir "Add a trailing / if missing"
][
    if tag? path [  ; path relative to currently running script [1]
        if #"/" = first path [
            panic ["TAG! import from SYSTEM.SCRIPT.PATH not relative:" path]
        ]
        if #"%" = first path [
            panic ["Likely mistake, % in TAG!-style import path:" path]
        ]
        else [
            path: join system.script.path (as text! path)
        ]
    ]

    if match [@word!] path [  ; lookup @tool on the internet [2]
        path: switch as tag! path  ; !!! list actually used tags, should change
            (load system.locale.library.utilities)
        else [
            panic ["Module" path "not in system.locale.library.utilities"]
        ]
    ]

    let scheme: null

    let target
    case [
        url? path [
            scheme: decode-url path
            target: either scheme.path [
                to file! scheme.path
            ][
                copy %/
            ]
        ]

        any [
            only
            text? path
            #"/" = first path
        ][
            target: copy path
        ]

        file? path [
            if url? let current: what-dir [
                scheme: decode-url current
                current: any [
                    scheme.path
                    copy %/
                ]
            ]

            target: to file! unspaced [opt current, path]  ; !!! why OPT?
        ]
    ]

    if all [dir, #"/" <> last target] [
        append target #"/"
    ]

    path: make type of target length of target

    let count: 0
    let part
    parse3 reverse target [
        opt some [not <end> [
            "../"
            (count: me + 1)
            |
            "./"
            |
            "/"
            (
                if any [
                    not file? target
                    #"/" <> try last path
                ][
                    append path #"/"
                ]
            )
            |
            part: across [to "/" | to <end>] (
                either count > 0 [
                    count: me - 1
                ][
                    if not find ["" "." ".."] as text! part [
                        append path part
                    ]
                ]
            )
        ]]
    ]

    if all [
        #"/" = last path
        #"/" <> last target
    ][
        remove back tail of path
    ]

    reverse path

    if not scheme [
        return path
    ]

    return to url! head of insert path unspaced [
        form scheme.scheme "://"
        if scheme.user [
            unspaced [
                scheme.user
                if scheme.pass [
                    unspaced [":" scheme.pass]
                ]
                "@"
            ]
        ]
        scheme.host
        if scheme.port-id [
            unspaced [":" scheme.port-id]
        ]
    ]
]


; This is a limited implementation of ASK just to get the ball rolling; could
; do much more: https://forum.rebol.info/t/1124
;
; 1. Reading a single character is not something possible in buffered line
;    I/O...you have to either be piping from a file or have a smart console.
;    The PORT! model in R3-Alpha was less than half-baked, but this READ-CHAR
;    has been added to try and help some piped I/O scenarios work (e.g. the
;    Whitespace interpreter test scripts.)
;
; 2. Getting NULL from READ-LINE signals "end of file".  At present this only
;    applies to redirected input--as there's no limit to how much you can type
;    in the terminal.  But it might be useful to have a key sequence that will
;    simulate end of file up until the current code finishes, so you can test
;    eof handling interactively of code that expects to operate on files.
;
; 3. The error trapped during the conversion may contain more useful info than
;    just saying "** Invalid input".  But there's no API for a "light" print
;    of errors.  Scrub out all the extra information from the error so it isn't
;    as verbose.
;
ask: func [
    "Ask the user for input"

    return: "Null if the input was aborted (via ESCAPE, Ctrl-D, etc.)"
        [any-value?]
    question "Prompt to user, datatype to request, or dialect block"
        [block! text! datatype!]
    :hide "mask input with * (Rebol2 feature, not yet implemented)"
    ; !!! What about /MULTILINE ?
][
    if hide [
        panic [
            "ASK/HIDE not yet implemented:"
            https://github.com/rebol/rebol-issues/issues/476
        ]
    ]

    let prompt: null
    let type: text!
    switch:type question [
        text! [prompt: question]  ; `ask "Input:"` doesn't filter type
        datatype! [type: question]  ; `ask text!` has no prompt (like INPUT)
        block! [
            parse question [
                opt prompt: text!
                opt let word: *in* word! (type: ensure datatype! get word)
            ] except [
                panic -[ASK currently only supports ["Prompt:" datatype!]]-
            ]
        ]
        panic ~<unreachable>~
    ]

    if type = issue! [
        return read-char stdin  ; won't work buffered [1]
    ]

    cycle [  ; while not canceled, loop while input can't be converted to type
        if prompt [
            write-stdout prompt
            write-stdout space  ; space after prompt is implicit
        ]

        let line: read-line stdin except e -> [
            return null  ; escape key pressed, return as null
        ]

        if not line [  ; can't happen with interactive console [2]
            return null
        ]

        if type = text! [
            return line  ; allow empty line, TRIM is caller's responsibility
        ]

        if empty? line [  ; assume means ask again (what about empty TAG!?)
            continue
        ]

        return (to type line except e -> [
            e.file: null  ; scrub for light printing of error [3]
            e.line: null
            e.where: null
            e.near: null
            print [e]

            continue  ; Keep cycling, bypasses the RETURN (...)
        ])
    ]
]


confirm: func [
    "Confirms a user choice"

    return: [logic?]
    question "Prompt to user"
        [any-series?]
    :with [text! block!]
][
    with: default [["y" "yes"] ["n" "no"]]

    all [
        block? with
        length of with > 2

        panic:blame [
            "maximum 2 arguments allowed for with [true false]"
            "got:" mold with
        ] $with
    ]

    let response: ask question

    return case [
        empty? with [okay]
        text? with [did find:match response with]
        length of with < 2 [did find:match response first with]
        find first with response [okay]
        find second with response [okay]
    ]
]


list-dir: func [
    "Print contents of a directory (ls)"

    return: []
    'path [<end> file! word! path! text!]
        "Accepts %file, :variables, and just words (as dirs)"
    :l "Line of info format"
    :f "Files only"
    :d "Dirs only"
;   :t "Time order"
    :r "Recursive"
    :i "Indent"
        [integer! text!]
][
    path: default [null]
    i: default [""]

    let save-dir: what-dir

    if not file? save-dir [
        panic ["No directory listing protocol registered for" save-dir]
    ]

    switch:type :path [
        null?/ []  ; Stay here
        file! [change-dir path]
        text! [change-dir local-to-file path]
        word! path! [change-dir to-file path]
    ]

    if r [l: ok]
    if not l [l: make text! 62] ; approx width

    let files
    sys.util/rescue [
        files: read %./
    ] then [
        print ["Not found:" :path]
        change-dir save-dir
        return ~
    ]

    for-each 'file files [
        any [
            all [f, dir? file]
            all [d, not dir? file]
        ] then [
            continue
        ]

        if text? l [
            append l file
            append:dup l #" " 15 - remainder length of l 15
            if greater? length of l 60 [print l clear l]
        ] else [
            let info: get (words of query file)
            let [_ filename]: split-path info.1
            change info
            printf [i 16 -8 #" " 24 #" " 6] info
            if all [r, dir? file] [
                list-dir:l:r:i file join i "    "
            ]
        ]
    ]

    all [text? l, not empty? l] then [print l]

    change-dir save-dir
]


undirize: func [
    "Returns a copy of the path with any trailing / removed"

    return: [file! text! url!]
    path [file! text! url!]
][
    path: copy path
    if #"/" = last path [clear back tail of path]
    return path
]


in-dir: func [
    "Evaluate a block in a directory, and restore current directory when done"
    return: [any-value?]
    dir [file!]
        "Directory to change to (changed back after)"
    block [block!]
        "Block to evaluate"
][
    let old-dir: what-dir
    change-dir dir

    ; You don't want the block to be done if the change-dir fails, for safety.

    return (
        eval block  ; return result
        elide change-dir old-dir
    )
]


to-relative-file: func [
    "Returns relative portion of a file if in subdirectory, original if not"

    return: [file! text!]
    file "File to check (local if text!)"
        [file! text!]
    :no-copy "Don't copy, just reference"
    :as-rebol "Convert to Rebol-style filename if not"
    :as-local "Convert to local-style filename if not"
][
    if text? file [ ; Local file
        comment [
            ; file-to-local drops trailing / in R2, not in R3
            if [_ tmp]: find:match file file-to-local what-dir [
                file: next tmp
            ]
        ]
        let pos
        if [_ pos]: find:match file (file-to-local what-dir) [
            file: pos  ; !!! https://forum.rebol.info/t/1582/6
        ]
        if as-rebol [
            file: local-to-file file
            no-copy: ok
        ]
    ] else [
        let pos
        if [_ pos]: find:match file what-dir [
            file: pos  ; !!! https://forum.rebol.info/t/1582/6
        ]
        if as-local [
            file: file-to-local file
            no-copy: ok
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
detab-file: func [
    "detabs a disk file"

    return: []
    filename [file!]
][
    write filename detab to text! read filename
]

; temporary location
set-net: func [
    "sets the system.user.identity email smtp pop3 esmtp-usr esmtp-pass fqdn"

    bl [block!]
][
    if 6 <> length of bl [panic "Needs all 6 parameters for set-net"]
    set (words of system.user.identity) bl
]
