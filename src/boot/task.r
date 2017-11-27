REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Task context"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Globals used for each task. Prevents GC of these values.
        See also the Root Vars (program-wide globals)
    }
]

ballast         ; current memory ballast (used for GC)
max-ballast     ; ballast reset value

stack-error     ; special stack overlow error object
halt-error      ; special halt error object

buf-collect     ; temporary cache for collecting object keys or words
buf-utf8        ; UTF8 reused buffer
byte-buf        ; temporary byte buffer - used mainly by raw print
mold-buf        ; temporary byte buffer - used mainly by mold
