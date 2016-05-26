REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Read-Eval-Print-Loop (REPL)"
    Rights: {
        Copyright 2016-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This implements a simple interactive command line.  It gathers strings
        to be executed but does not actually run them with DO--instead it
        returns a block to C code which does the actual execution.  The
        reason it is done this way is to avoid having Rebol REPL stack frames
        hanging around when debug commands are executed (e.g. one does not
        want BACKTRACE to see a FOREVER [] loop of gathering input, or the DO)

        Though not implemented in C as the R3-Alpha REPL was, it still relies
        upon INPUT to receive lines.  INPUT reads lines from the "console
        port", which is C code that is linked to STDTERM on POSIX and the
        Win32 Console API on Windows.  Thus, the ability to control the cursor
        and use it to page through line history is still a "black box" at
        that layer.
     }
]

host-repl: function [
    {Implements one Print-and-Read step of a Read-Eval-Print-Loop (REPL).}

    return: [block! error!]
        {Code to run or syntax error in the string input that tried to LOAD}
    last-result [<opt> any-value!]
        {The result from the last time HOST-REPL ran to display (if any)}
    focus-level [blank! integer!]
        {If at a breakpoint, the integer index of how deep the stack was}
    focus-frame [blank! frame!]
        {If at a breakpoint, the function frame where the breakpoint was hit}
][
    source: copy "" ;-- source code potentially built of multiple lines
    
    ; The LOADed and bound code.  It's initialized to empty block so that if
    ; there is no input text (just newline at a prompt) , it will be treated
    ; as DO [].
    ;
    code: copy []

    ; Output the last evaluation result if there was one.
    ;
    if set? 'last-result [
        print ["==" mold :last-result]
    ]

    print " " ;-- newline !!! (print "" does not do that right now)

    ; If a debug frame is in focus then show it in the prompt, e.g.
    ; as `if:|4|>>` to indicate stack frame 4 is being examined, and
    ; it was an `if` statement...so it will be used for binding (you
    ; can examine the condition and branch for instance)
    ;
    if focus-frame [
        if label-of focus-frame [
            print/only label-of focus-frame
            print/only ":"
        ]

        print/only "|"
        print/only focus-level
        print/only "|"
    ]

    print/only ">>"
    print/only space

    forever [ ;-- gather potentially multi-line input

        line: input
        if empty? line [
            ;
            ; if empty line, result is whatever's in `code`, even ERROR!
            ;
            break
        ]

        append source line

        trap/with [
            ;
            ; Note that LOAD/ALL makes BLOCK! even for a single item,
            ; e.g. `load/all "word"` => `[word]`
            ;
            code: load/all source
            assert [block? code]

            ; There is a question of how it should be decided whether the code
            ; in the REPL should be locked as read-only or not.
            ;
            lock code

        ] func [error] [
            ;
            ; If loading the string gave back an error, check to see if it
            ; was the kind of error that comes from having partial input.  If
            ; so, CONTINUE and read more data until it's complete (or until
            ; an empty line signals to just report the error as-is)
            ;
            ; Save the error even if it's a "needs continuation" error, in
            ; case the next input is an empty line.  That makes the error get
            ; reported, stopping people from getting trapped in a loop.
            ;
            ; !!! Note that this is a bit unnatural, but it's similar to what
            ; Ren Garden does (though it takes two lines).  It should not be
            ; applied to input that is pasted as text from another source,
            ; and arguably this could be disruptive to multi-line strings even
            ; if being entered in the REPL.
            ;
            code: error

            switch error/code [
            2000 [
                ; Often an invalid string (error isn't perfect but
                ; could be tailored specifically, e.g. to report
                ; a depth)
                ;
                print/only "{   "

                append source newline
                continue ]

            2001 [
                ; Often a missing bracket (again, imperfect error
                ; that could be improved.)
                ;
                case [
                    error/arg1 = "]" [print/only "[   "]
                    error/arg1 = ")" [print/only "(   "]
                    'default [break]
                ]

                append source newline
                continue ]
            ]
        ]

        break ;-- Exit FOREVER if no additional input to be gathered
    ]

    ; If we're focused on a debug frame, try binding into it
    ;
    if all [focus-frame | any-array? :code] [
        bind code focus-frame
    ]

    return code
]


why?: procedure [
    "Explain the last error in more detail."
    'err [<end> word! path! error! blank!] "Optional error value"
][
    case [
        not set? 'err [err: _]
        word? err [err: get err]
        path? err [err: get err]
    ]

    either all [
        error? err: any [:err system/state/last-error]
        err/type ; avoids lower level error types (like halt)
    ][
        ; In non-"NDEBUG" (No DEBUG) builds, if an error originated from the
        ; C sources then it will have a file and line number included of where
        ; the error was triggered.
        ;
        if all [
            file: attempt [system/state/last-error/__FILE__]
            line: attempt [system/state/last-error/__LINE__]
        ][
            print ["DEBUG BUILD INFO:"]
            print ["    __FILE__ =" file]
            print ["    __LINE__ =" line]
        ]

        say-browser
        err: lowercase ajoin [err/type #"-" err/id]
        browse join-of http://www.rebol.com/r3/docs/errors/ [err ".html"]
    ][
        print "No information is available."
    ]
]


upgrade: procedure [
    "Check for newer versions (update REBOL)."
][
    fail "Automatic upgrade checking is currently not supported."
]
