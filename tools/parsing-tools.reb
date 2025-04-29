Rebol [
    title: "Parsing tools"
    rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    author: "@codebybrett"
    version: 2.100.0
    needs: 2.100.100
    purpose: {
        These are some common routines used to assist parsing tasks.
    }
]


parsing-at: func [
    {Defines a rule which evaluates a block for the next input position, fails otherwise.}
    return: [block!]
    'word [word!] {Word set to input position (will be local).}
    block [block!]
        {Block to evaluate. Return next input position, or blank/false.}
    /end {Drop the default tail check (allows evaluation at the tail).}
][
    return use [result position][
        block: compose/only [(as group! block)]  ; code to be run as rule
        if not end [
            block: compose/deep [either not tail? (word) [(block)] [null]]
        ]
        block: compose/deep [result: either position: (block) [[:position]] [[end skip]]]
        use compose [(word)] compose/deep [
            [(as set-word! :word) ; <here> implicit
            (as group! block) result]
        ]
    ]
]
