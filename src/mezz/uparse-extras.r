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
            if not raised? parse/combinators input pattern combinators (
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
