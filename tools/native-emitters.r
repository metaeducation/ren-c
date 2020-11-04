REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %natives-emitters.r
    Rights: {
        Copyright 2017 Atronix Engineering
        Copyright 2017 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Needs: 2.100.100
]

emit-native-proto: function [
    "Emit native prototypes to @unsorted-buffer"
    return: <void>
    proto
    <with> proto-count
][
    line: try text-line-of proto-parser/parse-position

    all [
        block? proto-parser/data
        parse proto-parser/data [
            opt 'export
            set name: set-word!
            opt 'enfix
            ['native | and path! into ['native to end]]
            [
                set spec: block!
            | (
                fail [
                    "Native" (uppercase form to word! name)
                    "needs loadable specification block."
                    (mold the-file) (line)
                ]
            )]
            opt block!  ; optional body
            [
                end
            |
                ; currently extensions add PLATFORMS:, etc.
                ; Ideally this should be checked here for being valid
                ;
                to end
            ]
            end
        ]
    ] then [
        append case [
            ;
            ; could do tests here to create special buffer categories to
            ; put certain natives first or last, etc. (not currently needed)
            ;
            default [
                unsorted-buffer
            ]
        ] unspaced [
            newline newline
            {; !!! DO NOT EDIT HERE! This is generated from} _
                mold the-file _ {line} _ line newline
            mold/only proto-parser/data
        ]

        proto-count: proto-count + 1
    ]
]

emit-include-params-macro: function [
    "Emit macros for a native's parameters"

    return: <void>
    e [object!] "where to emit (see %common-emitters.r)"
    word [word!] "name of the native"
    paramlist [block!] "paramlist of the native"
    /ext [text!] "extension name"
][
    seen-refinement: false

    n: 1
    items: try collect* [
        ;
        ; All natives *should* specify a `return:`, because it's important
        ; to document what the return types are (and HELP should show it).
        ; However, only the debug build actually *type checks* the result;
        ; the C code is trusted otherwise to do the correct thing.
        ;
        ; By convention, definitional returns are the first argument.  (It is
        ; not currently enforced they actually be first in the parameter list
        ; the user provides...so making actions may have to shuffle the
        ; position.  But it may come to be enforced for efficiency).
        ;
        keep {PARAM(1, return)}
        keep {USED(ARG(return))}  ; Suppress warning about not using return
        n: n + 1

        for-each item paramlist [
            any [
                not match [any-word! refinement! lit-word!] item
                set-word? item
            ] then [
                continue
            ]

            param-name: as text! to word! dequote item
            keep cscape/with {PARAM($<n>, ${param-name})} [n param-name]
            n: n + 1
        ]
    ]

    prefix: try if ext [unspaced [ext "_"]]
    e/emit [prefix word items] {
        #define ${PREFIX}INCLUDE_PARAMS_OF_${WORD} \
            $[Items]; \
            assert(GET_SERIES_INFO(frame_->varlist, HOLD))
    }
    e/emit newline
]
