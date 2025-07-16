Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Control"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

wrap: func [
    "Evaluates a block, wrapping all set-words as locals."
    return: [any-stable!]
    body [block!] "Block to evaluate"
][
    return eval bind/copy/set body make object! 0
]
