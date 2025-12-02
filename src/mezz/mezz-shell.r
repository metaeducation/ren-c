Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: Shell-like Command Functions"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
]

ls: list-dir/
pwd: what-dir/

rm: ~#[Use DELETE, not RM (Rebol REMOVE is different, shell dialect coming)]#~

mkdir: make-dir/

cd: lambda [
    "CHANGE-DIR convenience (literal argument), returns directory after change"

    []: [file! url!]
    'path [
        <end> "CD with no argument just returns current directory"
        text! "Local filesystem convention (backslashes on Windows)"
        file! "Translated to local filesystem convention"
        word! path! tuple! "Converted TO FILE!"
    ]
][
    change-dir switch:type all [set? $path, path] [  ; :PATH someday will work
        null?/ [^void]
        file! [path]
        text! [local-to-file path]  ; LOCAL-TO-FILE is in filesystem extension
        tuple! word! path! [to-file path]
    ]

    what-dir
]

more: lambda [
    "Print file (shell shortcut function)"

    'file "Accepts %file and also just words (as file names)"
        [file! word! path! text!]
][
    print deline read:string switch:type file [
        file! [file]
        text! [local-to-file file]
        word! path! [to-file file]
    ]
]
