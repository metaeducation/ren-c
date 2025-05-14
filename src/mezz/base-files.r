Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Boot Base: File Functions"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    notes: --[
        This code is evaluated just after actions, natives, sysobj, and other
        lower-levels definitions. This file intializes a minimal working
        environment that is used for the rest of the boot.
    ]--
]

info?: func [
    "Returns an info object about a file or url"
    return: [~null~ object! word!]
    target [file! url!]
    :only "for urls, returns 'file or blank"
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
    "Returns the type of a file or URL if it exists, otherwise blank"
    return: [~null~ word!]
        "FILE, DIR, or null"  ; should return LOGIC!, FILETYPE OF separate
    target [file! url! blank!]
][
    if blank? target [return null]  ; https://forum.rebol.info/t/954

    if url? target [
        return info?:only target
    ]

    return select opt query target 'type
]

; !!! size-of used to be defined here, but it's a generic and so it has to
; be implemented differently, until generics can be written in usermode.
;
; See size-of native for the hacked in code.


modified?: func [
    "Returns the last modified date of a file"
    return: [~null~ date!]
    target [file! url!]
][
    return all [
        info: info? target
        info.date
    ]
]

suffix-of: func [
    "Return the file suffix of a filename or url, else null"
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
    "Returns TRUE if the file or url ends with a slash (or backslash)"
    return: [logic?]
    target [file! url!]
][
    return did find "/\" last target
]

dirize: func [
    "Returns a copy (always) of the path as a directory (ending slash)"
    path [file! text! url!]
][
    path: copy path
    if slash <> last path [append path slash]
    return path
]

make-dir: func [
    "Creates the specified directory, no error if already exists"

    return: [file! url!]
    path [file! url!]
    :deep "Create subdirectories too"
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
        if (not [_ :end]: find-last path slash) [
            end: path
        ]
        insert dirs copy end
        clear end
    ]

    ; Create directories forward:
    created: copy []
    for-each 'dir dirs [
        path: if empty? path [dir] else [join path dir]
        append path slash
        make-dir path except e -> [
            for-each 'dir created [
                sys.util/rescue [delete dir]
            ]
            return fail e
        ]
        insert created path
    ]
    return path
]

delete-dir: func [
    "Deletes a directory including all files and subdirectories"
    dir [file! url!]
    <local> files
][
    if all [
        dir? dir
        dir: dirize dir
        files: try load dir
    ] [
        for-each 'file files [delete-dir (join dir file)]
    ]
    sys.util/rescue [
        delete dir
    ]
]

script?: func [
    "Checks file, url, or text string for a valid script header"

    return: [~null~ blob!]
    source [file! url! blob! text!]
][
    if match [file! url!] source [
        source: read source
    ]
    transcode-header as blob! source else [
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
        let pos: find system.options.file-types opt suffix-of file
        first opt find pos word?/
    ]
]

split-path: func [  ; /FILE used in bootstrap vs. multi-return
    "Splits and returns ~[directory filename]~ (either may be null)"
    return: [~[[~null~ file! url!] [~null~ file! url!]]~]

    target [file! url!]
    :relax "Allow filenames to be . and .."
    <local> directory filename
][
    parse3 as text! target [
        directory: across opt some thru "/"
        filename: across thru <end>
    ]
    if empty? directory [
        directory: null
    ] else [
        directory: as type of target directory
    ]
    if empty? filename [
        filename: null
    ] else [
        filename: as file! filename
        all [
            not relax
            find [%. %..] filename
            panic ". and .. are invalid filenames"
        ]
    ]
    return pack [directory filename]
]
