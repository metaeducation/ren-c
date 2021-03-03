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

info?: function [
    {Returns an info object about a file or url.}
    return: [<opt> object! word!]
    target [file! url!]
    /only {for urls, returns 'file or blank}
][
    if file? target [
        return query target
    ]
    trap [
        t: write target [HEAD]
        if only [return 'file]
        return make object! [
            name: target
            size: t/2
            date: t/3
            type: 'url
        ]
    ] then [
        return null
    ]
]

exists?: func [
    {Returns the type of a file or URL if it exists, otherwise blank.}
    return: [<opt> word!]
        "FILE, DIR, or null"  ; should return LOGIC!, FILETYPE OF separate
    target [file! url! blank!]
][
    if blank? target [return null]  ; https://forum.rebol.info/t/954

    if url? target [
        return info?/only target
    ]

    select try attempt [query target] 'type
]

size-of: size?: function [
    {Returns the size of a file.}
    return: [<opt> integer!]
    target [file! url!]
][
    all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info/size
    ]
]

modified?: function [
    {Returns the last modified date of a file.}
    return: [<opt> date!]
    target [file! url!]
][
    all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info/date
    ]
]

suffix-of: function [
    "Return the file suffix of a filename or url. Else, null."
    return: [<opt> file!]
    path [file! url! text!]
][
    all [
        pos: find-last path #"."
        not find pos #"/"
        to file! pos
    ]
]

dir?: func [
    {Returns TRUE if the file or url ends with a slash (or backslash).}
    return: [logic!]
    target [file! url!]
][
    did find "/\" last target
]

dirize: func [
    {Returns a copy (always) of the path as a directory (ending slash).}
    path [file! text! url!]
][
    path: copy path
    if slash <> last path [append path slash]
    path
]

make-dir: func [
    "Creates the specified directory. No error if already exists."
    path [file! url!]
    /deep "Create subdirectories too"
    <local> dirs end created
][
    path: dirize path  ; append slash (if needed)
    assert [dir? path]

    if exists? path [return path]

    any [not deep, url? path] then [
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
            remove back tail of path ; trailing slash
        ]
    ][
        ; !!! Changing this to `path unless find/last/tail path slash` should
        ; work, but seems to cause a problem.  Review why when time permits.
        ;
        end: find-last/tail path slash else [path]
        insert dirs copy end
        clear end
    ]

    ; Create directories forward:
    created: copy []
    for-each dir dirs [
        path: if empty? path [dir] else [join path dir]
        append path slash
        trap [make-dir path] then (lambda e [
            for-each dir created [attempt [delete dir]]
            fail e
        ])
        insert created path
    ]
    path
]

delete-dir: func [
    {Deletes a directory including all files and subdirectories.}
    dir [file! url!]
    <local> files
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
    {Checks file, url, or text string for a valid script header.}

    return: [<opt> binary!]
    source [file! url! binary! text!]
][
    if match [file! url!] source [
        source: read source
    ]
    find-script source
]

file-type?: function [
    "Return the identifying word for a specific file type (or null)"
    return: [<opt> word!]
    file [file! url!]
][
    all [
        pos: find system/options/file-types try suffix-of file
        first try find pos word!
    ]
]

split-path: func [
    "Splits and returns directory path and file as a block."
    target [file! url! text!]
    <local> dir pos
][
    pos: _
    parse target [
        [#"/" | 1 2 #"." opt #"/"] end (dir: dirize target) |
        pos: here, any [thru #"/" [end | pos: here]] (
            all [
                empty? dir: copy/part target (at head of target index of pos),
                dir: %./
            ]
            all [find [%. %..] pos: to file! pos insert tail of pos #"/"]
        )
        end
    ]
    reduce [dir pos]
]


intern: function [
    "Imports (internalizes) words/values from the lib into the user context."
    data [block! any-word!] "Word or block of words to be added (deeply)"
][
    ; for optimization below (index for resolve)
    index: 1 + length of usr: system/contexts/user

    ; Extend the user context with new words
    data: bind/new :data usr

    ; Copy only the new values into the user context
    resolve/only usr lib index

    :data
]

