Rebol [
    Title: "Catch-any"
    File: %catch-any.r
    Copyright: [2012 "Saphirion AG"]
    License: {
        Licensed under the Apache License, Version 2.0 (the "License");
        you may not use this file except in compliance with the License.
        You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0
    }
    Author: "Ladislav Mecir"
    Purpose: "Catch any REBOL exception"
]

make object! [
    do-block: function [
        ; helper for catching BREAK, CONTINUE, THROW or QUIT
        return: [<opt> any-value!]
        block [block!]
        exception [word!]
    ] [
        ; TRY wraps CATCH/QUIT to circumvent bug#851
        try [
            catch/quit [
                catch [
                    loop 1 [
                        try [
                            set exception 'return
                            print mold block ;-- !!! make this an option
                            result: do block
                            set exception blank
                            return :result
                        ]
                        ; an error was triggered
                        set exception 'error
                        return ()
                    ]
                    ; BREAK or CONTINUE
                    set exception 'break
                    return ()
                ]
                ; THROW
                set exception 'throw
                return ()
            ]
            ; QUIT
            set exception 'quit
            return ()
        ]
    ]

    set 'catch-any func [
        {catches any REBOL exception}
        return: [<opt> any-value!]
        block [block!] {block to evaluate}
        exception [word!] {used to return the exception type}
        /local result
    ][
        ; !!! outdated comment, RETURN/REDO no longer exists, look into what
        ; this was supposed to be for.  --HF

        ; catch RETURN, EXIT and RETURN/REDO
        ; using the DO-BLOCK helper call
        ; the helper call is enclosed in a block
        ; not containing any additional values
        ; to not give REDO any "excess arguments"
        ; also, it is necessary to catch all above exceptions again
        ; in case they are triggered by REDO
        ; TRY wraps CATCH/QUIT to circumvent bug#851
        try [
            catch/quit [
                try [
                    catch [
                        loop 1 [result: do-block block exception]
                    ]
                ]
            ]
        ]
        if get exception [return ()]
        return :result
    ]
]
