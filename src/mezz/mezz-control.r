Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Control"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

launch: func [
    "Runs a script as a separate process; return immediately"

    script "The name of the script"
        [<opt-out> file! text!]
    :args "Arguments to the script"
        [text! block!]
    :wait "Wait for the process to terminate"
][
    if file? script [script: file-to-local clean-path script]
    let command: reduce [file-to-local system.options.boot script]
    append command opt spread args
    return call* // [command wait: wait]
]
