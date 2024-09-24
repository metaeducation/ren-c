Rebol [
    Title: "Tools Built On Top of Usermode PARSE"
    License: "LGPL 3.0"

    Type: module
    Name: Usermode-PARSE-Extras

    Exports: [
        destructure
    ]

    Description: {
        These routines are sensitive to UPARSE and should not be used in
        the bootstrap process at this time.
    }
]

destructure: func [
    input [any-series?]
    dialect [block!]
    /multi "Run multiple branches"
][
    let result': void'
    let combinators: copy default-combinators
    parse dialect [while [not <end>] [
        let set-word: *in* set-word!, let rule: *in* block! (
            combinators.(to word! set-word): compose [(set-word) (rule)]
        )
        |
        let pattern: *in* block!, '=>, let branch: *in* block!
        (
            if validate/combinators input pattern combinators (
                branch
            ) also ^r' -> [
                if not multi [
                    return unmeta r'
                ]
                result': r'
            ]
        )
        |
        fail "Invalid DESTRUCTURE dialect entry"
    ]]
    return unmeta result'
]


; Note: Users could write `parse data [...rules... || <input>]` and get the
; same effect generally.
;
; !!! It might be tempting to write this as an ADAPT which changes the
; rules to be:
;
;    rules: reduce [rules '|| <input>]
;
; But if someone changed the meaning of <input> with different /COMBINATORS
; that would not work.  This method will work regardless.
;
validate: (comment [redescribe [  ; redescribe not working at the moment (?)
    "Process input in the parse dialect, return input if match"
] ]
    enclose parse*/ func [f [frame!]] [
        let input: f.input  ; EVAL FRAME! invalidates args; cache for returning

        eval f except [return null]

        return input
    ]
)
