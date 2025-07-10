; functions/file/file-typeq.r
[#1651 ; "FILE-TYPE? should return NULL for unknown types"
    (null? file-type? %foo.0123456789bar0123456789)
]
