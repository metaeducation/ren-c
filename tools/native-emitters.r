REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generate auto headers"
    File: %native-emitters.r
    Type: module
    Name: Native-Emitters
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

do %import-shim.r
import %bootstrap-shim.r
import %common.r
import %common-emitter.r

import %common-parsers.r

import %text-lines.reb

export emit-native-proto: func [
    "Emit native prototypes to @unsorted-buffer"
    return: <none>
    proto
][
    let line: try text-line-of proto-parser/parse-position

    let [spec name]
    all [
        block? proto-parser/data
        parse? proto-parser/data [
            opt 'export
            set name: set-word!
            opt 'enfix
            ['native | 'native/combinator]
            [
                set spec: block!
            | (
                fail [
                    "Native" (uppercase form to word! name)
                    "needs loadable specification block."
                    (mold proto-parser/file) (line)
                ]
            )]
        ]
    ] then [
        ; could do tests here to create special buffer categories to
        ; put certain natives first or last, etc. (not currently needed)
        ;
        temp: proto-parser/data
        export-word: try if 'export = temp/1 [
            temp: next temp
            'export
        ]
        set-word: ensure set-word! temp/1
        temp: next temp

        enfix-word: try if 'enfix = temp/1 [
            temp: next temp
            'enfix
        ]

        any [
            'native = temp/1
            all [path? temp/1, 'native = temp/1/1]
        ] else [
            fail ["Malformed native:" mold proto-parser/data]
        ]

        append proto-parser/unsorted-buffer compose/only [
            (proto-parser/file) (line) (export-word) (set-word)
                (enfix-word) (temp)
        ]

        proto-parser/count: proto-parser/count + 1
    ]
]

export emit-include-params-macro: function [
    "Emit macros for a native's parameters"

    return: <none>
    e [object!] "where to emit (see %common-emitters.r)"
    native-name [word!] "native name (add -combinator if combinator)"
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
        loop [text? :paramlist/1] [paramlist: next paramlist]
        if (the return:) <> :paramlist/1 [
            fail [native-name "does not have a RETURN: specification"]
        ] else [
            keep {PARAM(1, return)}
            keep {USED(ARG(return))}  ; Suppress warning about not using return
            n: n + 1
            paramlist: next paramlist
        ]

        for-each item paramlist [
            if not match [any-word! refinement! lit-word!] item [
                continue
            ]

            param-name: as text! to word! noquote item
            keep cscape/with {PARAM($<n>, ${param-name})} [n param-name]
            n: n + 1
        ]
    ]

    prefix: try if ext [unspaced [ext "_"]]
    e/emit [prefix native-name items] {
        #define ${PREFIX}INCLUDE_PARAMS_OF_${NATIVE-NAME} \
            $[Items]; \
            assert(GET_SERIES_INFO(frame_->varlist, HOLD))
    }
    e/emit newline
]
