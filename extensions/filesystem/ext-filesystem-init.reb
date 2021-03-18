REBOL [
    Title: "File and Directory Access"
    Name: Filesystem
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

sys/make-scheme [
    title: "File Access"
    name: 'file
    actor: get-file-actor-handle
    info: system/standard/file-info ; for C enums
    init: func [return: <none> port <local> path] [
        if url? port/spec/ref [
            parse port/spec/ref [thru #":" 0 2 slash path:]
            append port/spec compose [path: (to file! path)]
        ]
    ]
]

sys/make-scheme/with [
    title: "File Directory Access"
    name: 'dir
    actor: get-dir-actor-handle
] 'file
