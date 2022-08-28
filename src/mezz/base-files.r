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

    t: write target [HEAD] except e -> [return null]

    if only [return 'file]
    return make object! [
        name: target
        size: t.2
        date: t.3
        type: 'url
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

    return try select decay attempt [query target] 'type
]

size-of: size?: function [
    {Returns the size of a file.}
    return: [<opt> integer!]
    target [file! url!]
][
    return all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info.size
    ]
]

modified?: function [
    {Returns the last modified date of a file.}
    return: [<opt> date!]
    target [file! url!]
][
    return all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info.date
    ]
]

suffix-of: function [
    "Return the file suffix of a filename or url. Else, null."
    return: [<opt> file!]
    path [file! url! text!]
][
    path: as text! path
    return all [
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
    return did find "/\" last target
]

dirize: func [
    {Returns a copy (always) of the path as a directory (ending slash).}
    path [file! text! url!]
][
    path: copy path
    if slash <> last path [append path slash]
    return path
]

make-dir: func [
    {Creates the specified directory, no error if already exists}

    return: [file! url!]
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
        if not [# end]: find-last path slash [
            end: path
        ]
        insert dirs copy end
        clear end
    ]

    ; Create directories forward:
    created: copy []
    for-each dir dirs [
        path: if empty? path [dir] else [join path dir]
        append path slash
        make-dir path except e -> [
            for-each dir created [attempt [delete dir]]
            return raise e
        ]
        insert created path
    ]
    return path
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
        for-each file files [delete-dir (join dir file)]
    ]
    return attempt [delete dir]
]

script?: func [
    {Checks file, url, or text string for a valid script header.}

    return: [<opt> binary!]
    source [file! url! binary! text!]
][
    if match [file! url!] source [
        source: read source
    ]
    transcode-header as binary! source else [
        return false
    ] except [
        return false
    ]
]

file-type?: function [
    "Return the identifying word for a specific file type (or null)"
    return: [<opt> word!]
    file [file! url!]
][
    return all [
        pos: try find system.options.file-types suffix-of file
        try first find pos matches word!
    ]
]

split-path: func [
    {Splits and returns directory path and file as a block}
    return: [<opt> file!]
    @dir [<opt> file! url!]
    target [file! url!]
    <local> pos text
][
    text: as text! target
    pos: _
    parse3 text [
        ["/" | "." opt "." opt "/"] end (dir: dirize text) |
        pos: <here>, opt some [thru "/" [end | pos: <here>]] (
            all [
                empty? dir: copy/part text (at head of text index of pos),
                dir: %"./"
            ]
            all [find [%. %..] pos: to file! pos insert tail of pos "/"]
        )
        end
    ]
    dir: as type of target dir
    return pos
]
