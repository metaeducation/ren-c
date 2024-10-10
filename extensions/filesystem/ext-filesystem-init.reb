REBOL [
    Title: "File and Directory Access"
    Name: Filesystem
    Type: Module
    Version: 1.0.0
    License: "Apache 2.0"
]

sys.util/make-scheme [
    title: "File Access"
    name: 'file
    actor: get-file-actor-handle
    info: system.standard.file-info ; for C enums
    /init: func [return: [~] port <local> path] [
        if url? port.spec.ref [
            parse3 port.spec.ref [thru #":" 0 2 slash path: <here>]
            append port.spec spread compose [path: (to file! path)]
        ]
    ]
]

sys.util/make-scheme:with [
    title: "File Directory Access"
    name: 'dir
    actor: get-dir-actor-handle
] 'file
