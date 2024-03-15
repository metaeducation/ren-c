REBOL [
    Title: "Parsing tools"
    Type: module
    Name: Parsing-Tools
    Rights: {
        Rebol is Copyright 1997-2015 REBOL Technologies
        REBOL is a trademark of REBOL Technologies

        Ren-C is Copyright 2015-2018 MetaEducation
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

if trap [:import/into] [  ; See %import-shim.r
    do <import-shim.r>
]

import <bootstrap-shim.r>

seek: []  ; Temporary measure, SEEK as no-op in bootstrap

export parsing-at: func [
    {Make rule that evaluates a block for next input position, fails otherwise}
    return: [block!]
    'word "Word set to input position (will be local)"
        [word!]
    code "Code to evaluate. Should Return next input position, or null"
        [block!]
    /end {Drop the default tail check (allows evaluation at the tail).}
][
    return use [result position][
        if end [  ; feature not used by Ren-C bootstrap, but kept anyway
            code: compose [(as group! code)]
        ] else [
            code: compose [either not tail? (word) (code) [null]]
        ]
        code: compose [
            result: either position: (spread code) [
                [:position]  ; seek
            ][
                [end skip]
            ]
        ]
        use compose [(word)] reduce [compose [
            (as set-word! word) <here>
            (as group! code) result
        ]]
    ]
]
