REBOL [
    Title: "Parsing tools"
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren/C is Copyright 2015 MetaEducation
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "@codebybrett"
    Version: 2.100.0
    Needs: 2.100.100
    Purpose: {
        These are some common routines used to assist parsing tasks.
    }
]


parsing-at: func [
    {Defines a rule which evaluates a block for the next input position, fails otherwise.}
    'word [word!] {Word set to input position (will be local).}
    block [block!]
        {Block to evaluate. Return next input position, or blank/false.}
    /end {Drop the default tail check (allows evaluation at the tail).}
] [
    use [result position][
        block: compose/only [to-value (to group! block)]
        if not end [
            block: compose/deep [all [not tail? (word) (block)]]
        ]
        block: compose/deep [result: either position: (block) [[:position]] [[end skip]]]
        use compose [(word)] compose/deep [
            [(to set-word! :word) (to group! block) result]
        ]
    ]
]
