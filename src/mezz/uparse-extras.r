Rebol [
    title: "Tools Built On Top of Usermode PARSE"
    license: "LGPL 3.0"

    type: module
    name: Usermode-PARSE-Extras

    exports: [
        destructure
    ]

    description: --[
        These routines are sensitive to UPARSE and should not be used in
        the bootstrap process at this time.
    ]--
]

destructure: func [
    input [any-series?]
    dialect [block!]
    :multi "Run multiple branches"
][
    let ^result: void
    let combinators: copy default-combinators
    parse dialect [until <end> [
        let set-word: *in* set-word?/, let rule: *in* block! (
            combinators.(unchain set-word): compose [(set-word) (rule)]
        )
        |
        let pattern: *in* block!, '=>, let branch: *in* block!
        (
            if parse-match:combinators input pattern combinators (
                branch
            ) also ^r -> [
                if not multi [
                    return ^r
                ]
                result: r
            ]
        )
        |
        panic "Invalid DESTRUCTURE dialect entry"
    ]]
    return ^result
]
