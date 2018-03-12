REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Console (Rebol's Read-Eval-Print-Loop, ie. REPL)"
    Rights: {
        Copyright 2016-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This is a rich, skinnable console for Rebol--where basically all the
        implementation is itself userspace Rebol code.  Documentation for the
        skinning hooks exist here:

        https://github.com/r3n/reboldocs/wiki/User-and-Console

        The HOST-CONSOLE Rebol function is invoked in a loop by a small C
        main function (see %host-main.c).  HOST-CONSOLE does not itself run
        arbitrary user code with DO.  That would be risky, because it actually
        is not allowed to fail or be canceled with Ctrl-C.  Instead, it just
        gathers input...and produces a block which is returned to C to
        actually execute.

        This design allows the console to sandbox potentially misbehaving
        skin code, and fall back on a default skin if there is a problem.
        It also makes sure that that user code doesn't see the console's
        implementation in its backtrace.

        !!! While not implemented in C as the R3-Alpha console was, this
        code relies upon the INPUT function to communicate with the user.
        INPUT is a black box that reads whole lines from the "console port",
        which is implemented via termios on POSIX and the Win32 Console API
        on Windows:

        https://blog.nelhage.com/2009/12/a-brief-introduction-to-termios/
        https://docs.microsoft.com/en-us/windows/console/console-functions

        Someday in the future, the console port itself should offer keystroke
        events and allow the line history (e.g. Cursor Up, Cursor Down) to be
        implemented in Rebol as well.
     }
]

; Define console! object for skinning - stub for elsewhere?
;
console!: make object! [
    name: _
    repl: true      ;-- used to identify this as a console! object (quack!)
    is-loaded:  false ;-- if true then this is a loaded (external) skin
    was-updated: false ;-- if true then console! object found in loaded skin
    last-result: _  ;-- last evaluated result (sent by HOST-CONSOLE)

    ;; APPEARANCE (can be overridden)

    prompt:   {>>}
    result:   {==}
    warning:  {!!}
    error:    {**}                 ;; not used yet
    info:     to-string #{e29398}  ;; info sign!
    greeting: _

    print-prompt: proc [] [
        ;
        ; !!! Previously the HOST-CONSOLE hook explicitly took an (optional)
        ; FRAME! where a debug session was focused and a stack depth integer,
        ; which was put into the prompt.  This feature is not strictly
        ; necessary (just helpful), and made HOST-CONSOLE seem less generic.
        ; It would make more sense for aspects of the debugger's state to
        ; be picked up from the environment somewhere (who's to say the
        ; focus-frame and focus-level are the only two relevant things)?
        ;
        ; For now, comment out the feature...and assume if it came back it
        ; would be grafted here in the PRINT-PROMPT.
        ;
        comment [
            ; If a debug frame is in focus then show it in the prompt, e.g.
            ; as `if:|4|>>` to indicate stack frame 4 is being examined, and
            ; it was an `if` statement...so it will be used for binding (you
            ; can examine the condition and branch for instance)
            ;
            if focus-frame [
                if label-of focus-frame [
                    write-stdout unspaced [label-of focus-frame ":"]
                ]
                write-stdout unspaced ["|" focus-level "|"]
            ]
        ]

        ; We don't want to use PRINT here because it would put the cursor on
        ; a new line.
        ;
        write-stdout identity block? prompt then [
            unspaced prompt
        ] else [
            form prompt
        ]
        write-stdout space
    ]

    print-result: proc [v [<opt> any-value!]]  [
        last-result: :v
        case [
            unset? 'v [] ;-- void evaluation, just don't print anything

            function? :v [ ;-- fully molded function output is too much
                print [result "#[function!" (mold words-of :v) "...]"]
            ]
        ]
        else [
            print [result (mold :v)]
        ]
    ]

    print-warning:  proc [s] [print [warning reduce s]]
    print-error:    proc [e [error!]] [print e]

    print-halted: proc [] [
        print "[interrupted by Ctrl-C or HALT instruction]"
    ]

    print-info:     proc [s] [print [info space space space reduce s]]
    print-greeting: proc []  [boot-print greeting]
    print-gap:      proc []  [print-newline]

    ;; BEHAVIOR (can be overridden)

    input-hook: func [
        {Receives line input, parse/transform, send back to CONSOLE eval}
        s [string!]
    ][
        s
    ]

    dialect-hook: func [
        {Receives code block, parse/transform, send back to CONSOLE eval}
        b [block!]
    ][
        ; !!! As with the notes on PRINT-PROMPT, the concept that the
        ; debugger parameterizes the HOST-CONSOLE function directly is being
        ; phased out.  So things like showing the stack level in the prompt,
        ; or binding code into the frame with focus, is something that would
        ; be the job of a "debugger skin" which extracts its parameterization
        ; from the environment.  Once these features are thought out more,
        ; that skin can be implemented (or the default skin can just look
        ; for debug state, and not apply debug skinning if it's not present.)
        ;
        comment [
            if focus-frame [
                bind code focus-frame
            ]
        ]

        b
    ]

    shortcuts: make object! compose/deep [
        d: [dump]
        h: [help]
        q: [quit]
        dt: [delta-time]
        dp: [delta-profile]

        list-shortcuts: [print system/console/shortcuts]
        changes: [
            say-browser
            browse (join-all [
                https://github.com/metaeducation/ren-c/blob/master/CHANGES.md#
                join-all ["" system/version/1 system/version/2 system/version/3]
            ])
        ]
        topics: [
            say-browser
            browse https://r3n.github.io/topics/
        ]
    ]

    ;; HELPERS (could be overridden!)

    add-shortcut: proc [
        {Add/Change console shortcut}
        name  [any-word!]
            {shortcut name}
        block [block!]
            {command(s) expanded to}
    ][
        extend shortcuts name block
    ]
]


start-console: procedure [
    "Called when a REPL is desired after command-line processing, vs quitting"

    <static>
        o (system/options) ;-- shorthand since options are often read/written
][
    ; Instantiate console! object into system/console for skinning.  This
    ; object can be updated %console-skin.reb if in system/options/resources

    loud-print "Starting console..."
    loud-print ""
    proto-skin: make console! []
    skin-error: _

    if all [
        skin-file: %console-skin.reb
        not find o/suppress skin-file
        o/resources
        exists? skin-file: join-of o/resources skin-file
    ][
        trap/with [
            new-skin: do load skin-file

            ;; if loaded skin returns console! object then use as prototype
            if all [
                object? new-skin
                select new-skin 'repl ;; quacks like REPL, say it's a console!
            ][
                proto-skin: new-skin
                proto-skin/was-updated: true
                proto-skin/name: any [proto-skin/name "updated"]
            ]

            proto-skin/is-loaded: true
            proto-skin/name: any [proto-skin/name "loaded"]
            append o/loaded skin-file

        ] func [error] [
            skin-error: error       ;; show error later if --verbose
            proto-skin/name: "error"
        ]
    ]

    proto-skin/name: any [proto-skin/name | "default"]

    system/console: proto-skin

    ; Make the error hook store the error as the last one printed, so the
    ; WHY command can access it.  Also inform people of the existence of
    ; the WHY function on the first error delivery.
    ;
    proto-skin/print-error: adapt :proto-skin/print-error [
        unless system/state/last-error [
            system/console/print-info "Note: use WHY for error information"
        ]

        system/state/last-error: e
    ]

    ; banner time
    ;
    if o/about [
        ;-- print fancy boot banner
        ;
        boot-print make-banner boot-banner
    ] else [
        boot-print [
            "Rebol 3 (Ren-C branch)"
            mold compose [version: (system/version) build: (system/build)]
            newline
        ]
    ]

    boot-print boot-welcome

    ; verbose console skinning messages
    loud-print [newline {Console skinning:} newline]
    if skin-error [
        loud-print [
            {  Error loading console skin  -} skin-file | |
            skin-error | |
            {  Fix error and restart CONSOLE}
        ]
    ] else [
       loud-print [
            space space
            proto-skin/is-loaded ?? {Loaded skin} !! {Skin does not exist}
            "-" skin-file
            spaced [
                "(CONSOLE" (proto-skin/was-updated ?! {not}) "updated)"
            ]
        ]
    ]

    system/console/print-greeting
]


host-console: function [
    {Rebol function called from C in a loop to implement the console.}

    return: [integer! block! group!]
        {Exit code, or Rebol to request that the C caller run in a sandbox}

    prior [blank! block! group!]
        {Whatever BLOCK! or GROUP! that HOST-CONSOLE previously ran}

    result [<opt> any-value!]
        {The result from evaluating LAST-CODE (if not errored or halted)}

    status [blank! error!]
        {BLANK! if no error, or ERROR! if a problem (includes HALT, QUIT...)}
][
    if not prior [
        ;
        ; First time running, so do startup.  As a temporary hack we get some
        ; properties passed from the C main() as a BLOCK! in result.  (These
        ; should probably be injected into the environment somehow instead.)
        ;
        assert [not status | block? result and (length of result = 3)]
        set [exec-path: argv: boot-exts:] result
        return (host-start exec-path argv boot-exts)
    ]

    ; BLOCK! code execution represents an instruction sent by the console to
    ; itself.  This "dialect" allows placing an ISSUE! or a block of issues as
    ; the first inert item as directives.  Canonize as block for easy search.
    ;
    directives: case [
        group? prior [[]]
        issue? first prior [reduce [first prior]]
        block? first prior [first prior]
    ] !! []

    ; QUIT handling (uncaught THROW/NAME with the name as the QUIT FUNCTION!)
    ;
    ; !!! R3-Alpha permitted arbitrary values for parameterized QUIT, which
    ; would be what DO of a script would return.  But if not an integer,
    ; they have to be mapped to an operating system exit status:
    ;
    ; https://en.wikipedia.org/wiki/Exit_status
    ;
    all [
        error? status
        status/id = 'no-catch-named
        :status/arg2 = :QUIT
    ] then [
        assert [unset? 'result]

        return case [
            void? :status/arg1 [
                ;
                ; Plain QUIT (no /WITH), consider it success
                ;
                0
            ]

            blank? :status/arg1 [0]

            integer? :status/arg1 [
                ;
                ; Fairly obviously, an integer should return an integer
                ; result.  But Rebol integers are 64 bit and signed, while
                ; exit statuses are small and unsigned.
                ;
                return :status/arg1
            ]

            error? :status/arg1 [
                ;
                ; Rebol errors have numbers, but they're out of range on
                ; platforms with byte-sized error codes.  Use generic 1.
                ;
                return 1
            ]
        ] !! 1
    ]

    ; HALT handling (uncaught THROW/NAME with the name as the HALT FUNCTION!)
    ;
    all [
        error? status
        status/id = 'no-catch-named
        :status/arg2 = :HALT
    ] then [
        assert [unset? 'result]
        if find directives #quit-if-halt [
            return 130 ; standard exit code for bash (128 + 2)
        ]
        if find directives #console-if-halt [
            return [
                start-console
                    |
                <needs-prompt>
            ]
        ]
        if find directives #unskin-if-halt [
            print "** UNSAFE HALT ENCOUNTERED IN CONSOLE SKIN"
            print "** REVERTING TO DEFAULT SKIN"
            system/console: make console! []
            print mold prior ;-- Might help debug to see what was running
        ]
        return compose/deep [
            #unskin-if-halt
                |
            system/console/print-halted
                |
            <needs-prompt>
        ]
    ]

    if error? status [
        assert [unset? 'result]

        instruction: compose/deep [
            ;
            ; Errors can occur during HOST-START, before the SYSTEM/CONSOLE
            ; has a chance to be initialized.
            ;
            all [
                function? :system/console ;-- starts as BLANK!
                function? :system/console/print-error ;-- may not be set
            ] then [
                system/console/print-error (status)
            ] else [
                print (status)
            ]
                |
        ]
        if find directives #quit-if-error [
            append instruction [
                quit/with 1 ;-- catch-all bash code for general errors
            ]
            return instruction
        ]
        if find directives #halt-if-error [
            append instruction [halt]
            return instruction
        ]
        if find directives #countdown-if-error [
            insert instruction [
                #console-if-halt
                    |
            ]
            append instruction compose/deep [
                print-newline
                print "** Hit Ctrl-C to break into the console in 5 seconds"
                repeat n 25 [
                    if 1 = remainder n 5 [
                        write-stdout form 5 - to-integer divide n 5
                    ] else [
                        write-stdout "."
                    ]
                    wait 0.25
                ]
                print-newline
                quit/with 1
            ]
            return instruction
        ]
        if block? prior [
            case [
                find directives #host-console-error [
                    print "** HOST-CONSOLE FUNCTION! ITSELF RAISED ERROR"
                    print "** SAFE RECOVERY NOT LIKELY, BUT TRYING ANYWAY"
                ]
                not find directives #no-unskin-if-error [
                    print "** UNSAFE ERROR ENCOUNTERED IN CONSOLE SKIN"
                ]
            ] also [
                print "** REVERTING TO DEFAULT SKIN"
                system/console: make console! []
                print mold prior ;-- Might help debug to see what was running
            ]
        ]
        append instruction [<needs-prompt>]
        return instruction
    ]

    assert [blank? status] ;-- no failure or halts during last execution

    if group? prior [ ;-- plain execution of user code
        if unset? 'result [
            return [
                system/console/print-result () ; can't COMPOSE voids in blocks
                    |
                <needs-prompt>
            ]
        ]
        return compose/deep/only [
            ;
            ; Can't pass `result` in directly, because it might be a FUNCTION!
            ; (which when composed in will execute instead of be passed as a
            ; parameter).  Can't use QUOTE because it would not allow BAR!.
            ; Use UNEVAL, which is a stronger QUOTE created for this purpose.
            ;
            system/console/print-result uneval (:result)
                |
            <needs-prompt>
        ]
    ]

    assert [block? prior] ;-- continuation sent by console to itself

    needs-prompt: true
    needs-gap: true
    needs-input: true
    source: copy {}

    ; TAG!s are used to make the needs of the continuations more
    ; clear at the `return [...]` points in this function.  But the
    ; special case of returning a BLOCK! is the self-trigger that
    ; indicates a need for the actual execution on the user's behalf.
    ; And the case of a STRING! is used to feed back the source after
    ; allowing a processing hook to run on it.
    ;
    case [
        block? result [
            return as group! result
        ]
        string? result [ ;-- dialect-hook
            source: result
            needs-prompt: needs-gap: needs-input: false
        ]
    ] else [
        switch result [
            <no-op> []
            <needs-prompt> [needs-prompt: true]
            <needs-gap> [needs-gap: true | needs-prompt: false]
            <no-prompt> [needs-prompt: needs-gap: false]
            <no-gap> [needs-gap: false]
        ] else [
            return compose/deep/only [
                #no-unskin-if-error
                    |
                print mold uneval (prior)
                    |
                fail ["Bad REPL continuation:" uneval (result)]
            ]
        ]
    ]

    if needs-gap [
        return [
            system/console/print-gap
                |
            <no-gap>
        ]
    ]

    if needs-prompt [
        return [
            system/console/print-prompt
                |
            <no-prompt>
        ]
    ]

    ; The LOADed and bound code.  It's initialized to empty block so that if
    ; there is no input text (just newline at a prompt) , it will be treated
    ; as DO [].
    ;
    code: copy []

    forever [ ;-- gather potentially multi-line input

        if needs-input [
            ;
            ; !!! Unfortunately Windows ReadConsole() has no way of being set
            ; to ignore Ctrl-C.  In usermode code, this is okay as Ctrl-C
            ; stops the Rebol code from running...but HOST-CONSOLE disables
            ; the halting behavior assigned to Ctrl-C.  To avoid glossing over
            ; that problem, INPUT doesn't just return blank or void...it
            ; FAILs.  We make a special effort to TRAP it here, but it would
            ; be a bug if seen by any other function.
            ;
            ; Upshot is that on Windows, Ctrl-C during HOST-CONSOLE winds up
            ; acting like if you had hit Ctrl-D (or on POSIX, escape).  Ctrl-C
            ; does nothing on POSIX--as we can "SIG_IGN"ore the Ctrl-C handler
            ; so it won't interrupt the read() loop.)
            ;
            user-input: trap/with [input] [blank]

            if blank? user-input [
                ;
                ; It was aborted.  This comes from ESC on POSIX (which is the
                ; ideal behavior), Ctrl-D on Windows (because ReadConsole()
                ; can't trap ESC), Ctrl-D on POSIX (just to be compatible with
                ; Windows), and the case of Ctrl-C on Windows just on calls
                ; to INPUT here in HOST-CONSOLE (usually it HALTs).
                ;
                ; Do a no-op execution that just cycles the prompt.
                ;
                return [<needs-gap>]
            ]

            return compose/deep [
                use [line] [
                    #unskin-if-halt ;-- Ctrl-C during input hook is a problem
                        |
                    line: system/console/input-hook (user-input)
                        |
                    append (source) line
                ]
                (source) ;-- STRING! signals feedback to BLANK? LAST-RESULT
            ]
        ]

        needs-input: true

        trap/with [
            ;
            ; Note that LOAD/ALL makes BLOCK! even for a single item,
            ; e.g. `load/all "word"` => `[word]`
            ;
            code: load/all source
            assert [block? code]

        ] func [error <with> return] [
            ;
            ; If loading the string gave back an error, check to see if it
            ; was the kind of error that comes from having partial input
            ; (scan-missing).  If so, CONTINUE and read more data until
            ; it's complete (or until an empty line signals to just report
            ; the error as-is)
            ;
            code: error

            if error/id = 'scan-missing [
                ;
                ; Error message tells you what's missing, not what's open and
                ; needs to be closed.  Invert the symbol.
                ;
                switch error/arg1 [
                    "}" ["{"]
                    ")" ["("]
                    "]" ["["]
                ] also unclosed -> [
                    ;
                    ; Backslash is used in the second column to help make a
                    ; pattern that isn't legal in Rebol code, which is also
                    ; uncommon in program output.  This enables detection of
                    ; transcripts, potentially to replay them without running
                    ; program output or evaluation results.
                    ;
                    write-stdout unspaced [unclosed #"\" space space]
                    append source newline
                    continue
                ] else [
                    ;
                    ; Could be an unclosed double quote (unclosed tag?) which
                    ; more input on a new line cannot legally close ATM
                ]
            ]

            return compose/deep [
                system/console/print-error (error)
                    |
                <needs-prompt>
            ]
        ]

        break ;-- Exit FOREVER if no additional input to be gathered
    ]

    instruction: copy [
        #unskin-if-halt ;-- Ctrl-C during dialect hook is a problem
            |
    ]

    if did shortcut: select system/console/shortcuts to-value first code [
        ;
        ; Shortcuts (defaults include `q => [quit]`, `d => [dump]`)
        ;
        if all [bound? code/1 | set? code/1] [
            ;
            ; Help confused user who might not know about the shortcut not
            ; panic by giving them a message.  Reduce noise for the casual
            ; shortcut by only doing so when a bound variable exists.
            ;
            append instruction compose/deep [
                system/console/print-warning [
                    (uppercase to-string code/1)
                        "interpreted by console as:" form [(:shortcut)]
                ]
                    |
                system/console/print-warning [
                    "use" form to-get-word (code/1) "to get variable."
                ]
            ]
        ]
        take code
        insert code shortcut
    ]

    ; There is a question of how it should be decided whether the code in the
    ; CONSOLE should be locked as read-only or not.  It may be a configuration
    ; switch, as it also may be an option for a module or a special type of
    ; function which does not lock its source.
    ;
    lock code

    ; Sandbox the dialect hook, which should return a BLOCK!.  When the block
    ; makes it back to HOST-CONSOLE it will be turned into a GROUP! and run.
    ;
    append instruction compose/only [
            |
        system/console/dialect-hook (code)
    ]
    return instruction
]


why: procedure [
    "Explain the last error in more detail."
    'err [<end> word! path! error! blank!] "Optional error value"
][
    err: default [system/state/last-error]

    if match [word! path!] err [
        err: get err
    ]

    if error? err [
        say-browser
        err: lowercase unspaced [err/type #"-" err/id]
        browse join-of http://www.rebol.com/r3/docs/errors/ [err ".html"]
    ] else [
        print "No information is available."
    ]
]


upgrade: procedure [
    "Check for newer versions."
][
    ; Should this be a console-detected command, like Q, or is it meaningful
    ; to define this as a function you could call from code?
    ;
    do <upgrade>
]


; The ECHO routine has to collaborate specifically with the console, because
; it is often desirable to capture the input only, the output only, or both.
;
; !!! The features that tie the echo specifically to the console would be
; things like ECHO INPUT, e.g.:
;
; https://github.com/red/red/issues/2487
;
; They are not implemented yet, but ECHO is moved here to signify the known
; issue that the CONSOLE must collaborate specifically with ECHO to achieve
; this.
;
echo: procedure [
    {Copies console I/O to a file.}

    'instruction [file! string! block! word!]
        {File or template with * substitution, or command: [ON OFF RESET].}

    <static>
    target ([%echo * %.txt])
    form-target
    sub ("")
    old-input (copy :input)
    old-write-stdout (copy :write-stdout)
    hook-in
    hook-out
    logger
    ensure-echo-on
    ensure-echo-off
][
    ; Sample "interesting" feature, be willing to form the filename by filling
    ; in the blank with a substitute string you can change.
    ;
    form-target: default [func [return: [file!]] [
        either block? target [
            as file! unspaced replace (copy target) '* (
                either empty? sub [[]] [unspaced ["-" sub]]
            )
        ][
            target
        ]
    ]]

    logger: default [func [value][
        write/append form-target either char? value [to-string value][value]
        value
    ]]

    ; Installed hook; in an ideal world, WRITE-STDOUT would not exist and
    ; would just be WRITE, so this would be hooking WRITE and checking for
    ; STDOUT or falling through.  Note WRITE doesn't take CHAR! right now.
    ;
    hook-out: default [proc [
        value [string! char! binary!]
            {Text to write, if a STRING! or CHAR! is converted to OS format}
    ][
        old-write-stdout value
        logger value
    ]]

    ; It looks a bit strange to look at a console log without the input
    ; being included too.  Note that hooking the input function doesn't get
    ; the newlines, has to be added.
    ;
    hook-in: default [
        chain [
            :old-input
                |
            func [value] [
                logger value
                logger newline
                value ;-- hook still needs to return the original value
            ]
        ]
    ]

    ensure-echo-on: default [does [
        ;
        ; Hijacking is a NO-OP if the functions are the same.
        ; (this is indicated by a BLANK! return vs a FUNCTION!)
        ;
        hijack 'write-stdout 'hook-out
        hijack 'input 'hook-in
    ]]

    ensure-echo-off: default [does [
        ;
        ; Restoring a hijacked function with its original will
        ; remove any overhead and be as fast as it was originally.
        ;
        hijack 'write-stdout 'old-write-stdout
        hijack 'input 'old-input
    ]]

    case [
        word? instruction [
            switch instruction [
                on [ensure-echo-on]
                off [ensure-echo-off]
                reset [
                    delete form-target
                    write/append form-target "" ;-- or just have it not exist?
                ]
            ] else [
                word: to-uppercase word
                fail [
                    "Unknown ECHO command, not [ON OFF RESET]"
                        |
                    unspaced ["Use ECHO (" word ") to force evaluation"]
                ]
            ]
        ]

        string? instruction [
            sub: instruction
            ensure-echo-on
        ]

        any [block? instruction | file? instruction] [
            target: instruction
            ensure-echo-on
        ]
    ]
]
