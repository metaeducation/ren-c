Rebol [
    Title: {Tools Built On Top of Usermode PARSE}
    License: {LGPL 3.0}

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
    input [any-series!]
    dialect [block!]
    /multi "Run multiple branches"
    <local> result' set-word rule pattern branch combinators
][
    result': void'
    combinators: copy default-combinators
    parse dialect [while [not <end>] [
        set-word: set-word!, rule: block! (
            combinators.(to word! set-word): compose [(set-word) (rule)]
        )
        |
        pattern: block!, '=>, branch: block!
        (
            parse/combinators input pattern combinators then (
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
