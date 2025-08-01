Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "Task context"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    purpose: --[
        Globals used for each task. Prevents GC of these values.
        See also the Root Vars (program-wide globals)
    ]--
]

ballast         ; current memory ballast (used for GC)
max-ballast     ; ballast reset value

stack-error     ; special stack overlow error object
halt-error      ; special halt error object

buf-collect     ; temporary cache for collecting object keys or words
byte-buf        ; temporary byte buffer - used mainly by raw print
mold-buf        ; temporary byte buffer - used mainly by mold
