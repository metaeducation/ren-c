Rebol [
    title: "File and Directory Access"
    name: Filesystem
    type: module
    version: 1.0.0
    license: "Apache 2.0"
]

sys.util/make-scheme [
    title: "File Access"
    name: 'file
    actor: file-actor/
    info: system.standard.file-info ; for C enums
    init: func [return: [] port <local> path] [
        if url? port.spec.ref [
            parse3 port.spec.ref [thru #":" 0 2 slash path: <here>]
            append port.spec spread compose [path: (to file! path)]
        ]
    ]
]

sys.util/make-scheme:with [
    title: "File Directory Access"
    name: 'dir
    actor: dir-actor/
] 'file
