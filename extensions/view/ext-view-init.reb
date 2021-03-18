REBOL [
    Title: "View Extension"
    Name: View
    Type: Module
    Version: 1.0.0
    License: {Apache 2.0}
]

; Move the default filters to usermode code, instead of a hardcoded C literal
;
request-file: adapt :request-file* [
    ;
    ; !!! What notation should be used to indicate the default filter?
    ; Perhaps put in a GROUP!?
    ;
    filter: default '[
        "All files"         %*.*
        "Rebol scripts"     %*.r
        ; ("Default idea"   %*.xxx)
        "Text files"        %*.txt
    ]
]

; Built on the lower-evel REQUEST-DIR* function
;
; "Asks user to select a directory and returns it as file path"
;
request-dir: chain [
    adapt :request-dir* [
        if path [
            dir: lib/replace/all file-to-local dir "/" "//"
        ]
    ]
        |
    func [result] [
        if result [
            return to-rebol-file result
        ]
    ]
]

export [request-file request-dir]
