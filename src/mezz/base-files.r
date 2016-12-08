REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: File Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

info?: func [
    {Returns an info object about a file or url.}
    target [file! url!]
][
    query target
]

exists?: func [
    {Returns the type of a file or URL if it exists, otherwise blank.}
    target [file! url!]
][ ; Returns 'file or 'dir, or blank
    select attempt [query target] 'type
]

size?: func [
    {Returns the size of a file.}
    target [file! url!]
][
    all [
        target: attempt [query target]
        target/size
    ]
]

modified?: func [
    {Returns the last modified date of a file.}
    target [file! url!]
][
    all [
        target: attempt [query target]
        target/date
    ]
]

suffix-of: func [
    "Return the file suffix of a filename or url. Else, NONE."
    path [file! url! string!]
][
    to-value if all [
        path: find/last path #"."
        not find path #"/"
    ][to file! path]
]

dir?: func [
    {Returns TRUE if the file or url ends with a slash (or backslash).}
    target [file! url!]
][
    true? find "/\" last target
]

dirize: func [
    {Returns a copy (always) of the path as a directory (ending slash).}
    path [file! string! url!]
][
    path: copy path
    if slash <> last path [append path slash]
    path
]

make-dir: func [
    "Creates the specified directory. No error if already exists."
    path [file! url!]
    /deep "Create subdirectories too"
    /local dirs end created
][
    if empty? path [return path]
    if slash <> last path [path: dirize path]

    if exists? path [
        if dir? path [return path]
        cause-error 'access 'cannot-open path
    ]

    if any [not deep url? path] [
        create path
        return path
    ]

    ; Scan reverse looking for first existing dir:
    path: copy path
    dirs: copy []
    while [
        all [
            not empty? path
            not exists? path
            remove back tail path ; trailing slash
        ]
    ][
        end: any [find/last/tail path slash path]
        insert dirs copy end
        clear end
    ]

    ; Create directories forward:
    created: copy []
    for-each dir dirs [
        path: either empty? path [dir][path/:dir]
        append path slash
        if trap? [make-dir path] [
            for-each dir created [attempt [delete dir]]
            cause-error 'access 'cannot-open path
        ]
        insert created path
    ]
    path
]

delete-dir: func [
    {Deletes a directory including all files and subdirectories.}
    dir [file! url!]
    /local files
][
    if all [
        dir? dir
        dir: dirize dir
        attempt [files: load dir]
    ] [
        for-each file files [delete-dir dir/:file]
    ]
    attempt [delete dir]
]

script?: func [
    {Checks file, url, or string for a valid script header.}
    source [file! url! binary! string!]
][
    ; !!! to word! necessary as long as OPTIONS_DATATYPE_WORD_STRICT exists
    switch to word! type-of source [
        file! url! [source: read source]
        string! [source: to binary! source] ; Remove this line if FIND-SCRIPT changed to accept string!
    ]
    find-script source ; Returns binary!
]

file-type?: func [
    "Return the identifying word for a specific file type (or NONE)."
    file [file! url!]
][
    to-value if file: find find system/options/file-types suffix-of file word! [
        first file
    ]
]

split-path: func [
    "Splits and returns directory path and file as a block."
    target [file! url! string!]
    /local dir pos
][
    parse target [
        [#"/" | 1 2 #"." opt #"/"] end (dir: dirize target) |
        pos: any [thru #"/" [end | pos:]] (
            all [empty? dir: copy/part target at head target index-of pos dir: %./]
            all [find [%. %..] pos: to file! pos insert tail pos #"/"]
        )
    ]
    reduce [dir pos]
]

intern: function [
    "Imports (internalize) words and their values from the lib into the user context."
    data [block! any-word!] "Word or block of words to be added (deeply)"
][
    index: 1 + length usr: system/contexts/user ; optimization
    data: bind/new :data usr   ; Extend the user context with new words
    resolve/only usr lib index ; Copy only the new values into the user context
    :data
]

load: function [
    {Simple load of a file, URL, or string/binary - minimal boot version.}

    source [file! url! string! binary!]
    /header
        {Includes REBOL header object if present}
    /all
        {Load all values, including header (as block)}
    /unlocked
        {Do not lock the source from modification.}
    ;/unbound {Do not bind the block}
][
    load_ALL: all
    all: :lib/all

    if string? data: case [
        file? source [read source]
        url? source [read source]
        'else [source]
    ] [data: to binary! data]

    if binary? :data [
        data: transcode data
        hdr?: all ['REBOL = first data block? second data]
        case [
            header [
                unless hdr? [cause-error 'syntax 'no-header source]
                remove data
                data/1: attempt [construct/only system/standard/header (first data)]
            ]
            load_ALL blank ; /header overrides /all
            hdr? [remove/part data 2]
        ]
    ;   unless unbound []
        data: intern data

        ; If appropriate and possible, return singular data value
        unless any [
            load_ALL
            header
            1 <> length data
        ][data: first data]
    ]

    ; This is the low-level LOAD code.  It seems to work all right when it
    ; assumes it locks things, so keep that as a default.  (User-level LOAD
    ; does not lock by default, but code that uses it to load code to run
    ; locks it, e.g. DO %filename.reb)
    ;
    either unlocked [:data] [lock :data]
]

; Reserve these slots near LOAD:
save:
import:
    blank
