Rebol [
    system: "Ren-C Language Interpreter and Run-time Environment"
    title: "SymId definitions that don't need CANON(name) in core EXE"
    file: %symbol-ids.r
    rights: --[
        Copyright 2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
        If you just want a SymId to use in an enumeration, and don't want
        to force the a-priori creation of a Symbol struct in memory for it,
        put it here.

        (This is similar to %ext-symbol-ids.r, except instead of being numbers
        like SYM_EXT_1234 the constants are actual SYM_XXX names.)
    ]--
]

; === CHECKSUM (CORE) ===

crc32
adler32
