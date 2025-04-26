REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Control"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

wrap: func [
    "Evaluates a block, wrapping all set-words as locals."
    return: [any-value!]
    body [block!] "Block to evaluate"
][
    return eval bind/copy/set body make object! 0
]
