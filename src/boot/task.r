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
        See also the Root Context (program-wide globals)
    }
]

stack           ; data stack
ballast         ; current memory ballast (used for GC)
max-ballast     ; ballast reset value
stack-error     ; special stack overlow error object
halt-error      ; special halt error object
this-context    ; current context
buf-emit        ; temporary emit output block
buf-collect     ; temporary cache for collecting object keys or words
buf-utf8        ; UTF8 reused buffer
buf-print       ; temporary print output - used by raw print
byte-buf        ; temporary byte buffer - used mainly by raw print
uni-buf         ; temporary unicode buffer - used mainly by mold 
mold-stack      ; mold loop detection

