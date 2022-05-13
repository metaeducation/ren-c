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

launch: function [
    {Runs a script as a separate process; return immediately.}

    script "The name of the script"
        [<blank> file! text!]
    /args "Arguments to the script"
        [text! block!]
    /wait "Wait for the process to terminate"
][
    if file? script [script: file-to-local clean-path script]
    command: reduce [file-to-local system.options.boot script]
    append command opt arg
    apply :call* [command, /wait wait]
]

wrap: func [
    "Evaluates a block, wrapping all set-words as locals."
    body [block!] "Block to evaluate"
][
    do bind/copy/set body make object! 0
]
