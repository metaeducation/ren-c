Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Function Helpers"
    rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    license: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]


map: func [
    {Make a map value (hashed associative block).}
    return: [map!]
    val
][
    return make map! :val
]
