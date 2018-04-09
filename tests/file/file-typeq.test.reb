; functions/file/file-typeq.r
[#1651 ; "FILE-TYPE? should return NONE for unknown types"
    (blank? file-type? %foo.0123456789bar0123456789)
]
