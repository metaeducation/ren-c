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
    return: [~null~ object! word!]
    target [file! url!]
    /only {for urls, returns 'file or blank}
][
    if file? target [
        return query target
    ]

    let t: write target [HEAD] except e -> [return null]

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
    return: [~null~ word!]
        "FILE, DIR, or null"  ; should return LOGIC!, FILETYPE OF separate
    target [file! url! blank!]
][
    if blank? target [return null]  ; https://forum.rebol.info/t/954

    if url? target [
        return info?/only target
    ]

    return select maybe attempt [query target] 'type
]

size-of: size?: func [
    {Returns the size of a file.}
    return: [~null~ integer!]
    target [file! url!]
][
    return all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info.size
    ]
]

modified?: func [
    {Returns the last modified date of a file.}
    return: [~null~ date!]
    target [file! url!]
][
    return all [
        info: attempt [info? target]  ; !!! Why not let the error report?
        info.date
    ]
]

suffix-of: func [
    "Return the file suffix of a filename or url. Else, null."
    return: [~null~ file!]
    path [file! url! text!]
][
    path: as text! path
    return all [
        let pos: find-last path "."
        not find pos "/"
        to file! pos
    ]
]

dir?: func [
    {Returns TRUE if the file or url ends with a slash (or backslash).}
    return: [logic?]
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
        if (not [@ /end]: find-last path slash) [
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

    return: [~null~ binary!]
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

file-type?: func [
    "Return the identifying word for a specific file type (or null)"
    return: [~null~ word!]
    file [file! url!]
][
    return all [
        let pos: find system.options.file-types maybe suffix-of file
        first maybe find pos matches word!
    ]
]

split-path: func [
    {Splits and returns file and directory path (either may be null)}
    return: [~null~ file! url!]
    @filename [~null~ file! url!]  ; /FILE used by AUGMENT in bootstrap shim

    target [file! url!]
    /relax "Allow filenames to be . and .."
    <local> text directory
][
    parse3 as text! target [
        directory: across opt some thru "/"
        filename: across thru <end>
    ]
    if empty? directory [
        directory: null
    ] else [
        directory: as kind of target directory
    ]
    if empty? filename [
        filename: null
    ] else [
        filename: as file! filename
        all [
            not relax
            find [%. %..] filename
            fail {. and .. are invalid filenames}
        ]
    ]
    return directory
]
