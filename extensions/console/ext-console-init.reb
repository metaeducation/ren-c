REBOL [
    Title: "Console Extension (Rebol's Read-Eval-Print-Loop, ie. REPL)"

    Name: console
    Type: Module

    Options: [isolate] ;-- !!! said [extension delay], what for?

    Rights: {
        Copyright 2016-2018 Rebol Open Source Contributors
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

boot-print: redescribe [
    "Prints during boot when not quiet."
](
    ;; !!! Duplicates code in %host-start.r, where this isn't exported.
    enclose 'print func [f] [if not system/options/quiet [eval f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    ;; !!! Duplicates code in %host-start.r, where this isn't exported.
    enclose 'print func [f] [if system/options/verbose [eval f]]
)

boot-welcome:
{Welcome to Rebol.  For more information please type in the commands below:

  HELP    - For starting information
  ABOUT   - Information about your Rebol
  CHANGES - What's different about this version}


; Define console! object for skinning - stub for elsewhere?
;
console!: make object! [
    name: null
    repl: okay  ; used to identify this as a console! object (quack!)
    is-loaded: null  ; if okay then this is a loaded (external) skin
    was-updated: null  ; if okay then console! object found in loaded skin
    last-result: ~  ; last evaluated result (sent by HOST-CONSOLE)

    ;; APPEARANCE (can be overridden)

    prompt: {>>}
    result: {==}
    warning: {!!}
    error: {**} ;-- not used yet
    info: to-text #{e29398} ;-- info "(i)" symbol
    greeting: null

    print-prompt: func [return: [~]] [
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
                if label of focus-frame [
                    write-stdout unspaced [label of focus-frame ":"]
                ]
                write-stdout unspaced ["|" focus-level "|"]
            ]
        ]

        ; We don't want to use PRINT here because it would put the cursor on
        ; a new line.
        ;
        write-stdout <- block? prompt and [
            unspaced prompt
        ] or [
            form prompt
        ]
        write-stdout space
    ]

    print-result: function [return: [~] v [any-value!]]  [

        if trash? last-result: get/any 'v [
            ;
            ; There are no antiforms in the R3C branch, but we can lie and
            ; make this legacy bootstrapping branch at least look a bit like
            ; the new branch.
            ;
            return ~
        ]

        case [
            void? :v [  ; nothingness (e.g. result of eval [] or if null [...])
                print [result "~void~  ; anti"]
            ]

            null? :v [
                ; As with nothing, we can be forward-looking and lie about the
                ; representation of null, as a WORD! antiform.
                ;
                print [result "~null~  ; anti"]
            ]

            okay? :v [
                print [result "~okay~  ; anti"]
            ]

            free? :v [
                ; Molding a freed value would cause an error...which is
                ; usually okay (you shouldn't be working with freed series)
                ; but if we didn't special case it here, the error would seem
                ; to be in the console code itself.
                ;
                print-error make error! "Series data unavailable due to FREE"
            ]

            port? :v [
                ; PORT!s are returned by many operations on files, to
                ; permit chaining.  They contain many fields so their
                ; molding is excessive, and there's not a ton to learn
                ; about them.  Cut down the output more than the mold/limit.
                ;
                print [result "#[port! [...] [...]]"]
            ]
        ]
        else [
            ; print the first 20 lines of the first 2048 characters of mold
            ;
            pos: molded: mold/limit :v 2048
            repeat 20 [
                pos: next (find pos newline else [break])
            ] then [ ; e.g. didn't break
                insert clear pos "..."
            ]
            print [result (molded)]
        ]
    ]

    print-warning:  func [s] [print [warning reduce s]]
    print-error:    func [e [error!]] [print [form e]]

    print-halted: func [] [
        print "[interrupted by Ctrl-C or HALT instruction]"
    ]

    print-info:     func [s] [print [info space space space reduce s]]
    print-greeting: func []  [boot-print greeting]
    print-gap:      func []  [print newline]

    ;; BEHAVIOR (can be overridden)

    input-hook: func [
        {Receives line input, parse/transform, send back to CONSOLE eval}

        return: "null if canceled, otherwise processed text line input"
            [~null~ text!]
    ][
        return ask text!
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

        return b
    ]

    shortcuts: make object! compose/deep [
        d: [dump]
        h: [help]
        q: [quit 0]
        dt: [delta-time]
        dp: [delta-profile]

        list-shortcuts: [print system/console/shortcuts]
        changes: [
            say-browser
            browse (join
                https://github.com/metaeducation/ren-c/blob/master/CHANGES.md#
                unspaced [
                    system/version/1 "." system/version/2 "."system/version/3
                ]
            )
        ]
        topics: [
            say-browser
            browse https://r3n.github.io/topics/
        ]
    ]

    ;; HELPERS (could be overridden!)

    add-shortcut: func [
        {Add/Change console shortcut}
        return: [~]
        name [any-word!] "Shortcut name"
        block [block!] "Command(s) expanded to"
    ][
        set (extend shortcuts 'name) block
    ]
]


start-console: function [
    "Called when a REPL is desired after command-line processing, vs quitting"

    return: [~]
    <static>
        o (system/options) ;-- shorthand since options are often read/written
][
    ; Instantiate console! object into system/console for skinning.  This
    ; object can be updated %console-skin.reb if in system/options/resources

    loud-print "Starting console..."
    loud-print ""
    proto-skin: make console! []
    skin-error: null

    all [
        skin-file: %console-skin.reb
        not find maybe o/suppress skin-file
        o/resources
        exists? skin-file: join o/resources skin-file
    ] then [
        sys/util/rescue [
            new-skin: do load skin-file

            ;; if loaded skin returns console! object then use as prototype
            all [
                object? new-skin
                select new-skin 'repl ;; quacks like REPL, it's a console!
            ] then [
                proto-skin: new-skin
                proto-skin/was-updated: okay
                proto-skin/name: default ["updated"]
            ]

            proto-skin/is-loaded: okay
            proto-skin/name: default ["loaded"]
            append o/loaded skin-file

        ] then arrow e [
            skin-error: e       ;; show error later if --verbose
            proto-skin/name: "error"
        ]
    ]

    proto-skin/name: default ["default"]

    system/console: proto-skin

    ; Make the error hook store the error as the last one printed, so the
    ; WHY command can access it.  Also inform people of the existence of
    ; the WHY function on the first error delivery.
    ;
    proto-skin/print-error: adapt :proto-skin/print-error [
        if not error? system/state/last-error [  ; NOT of ERROR! abrupt fails
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
            {  Error loading console skin  -} skin-file LF LF
            skin-error LF LF
            {  Fix error and restart CONSOLE}
        ]
    ] else [
       loud-print [
            space space
            if proto-skin/is-loaded [
                {Loaded skin}
            ] else [
                {Skin does not exist}
            ]
            "-" skin-file
            spaced [
                "(CONSOLE" (if not proto-skin/was-updated [{not}]) "updated)"
            ]
        ]
    ]
]


ext-console-impl: function [
    {Rebol ACTION! that is called from C in a loop to implement the console}

    return: "Code submission for C caller to run in a sandbox, or exit status"
        [block! group! integer! path!] ;-- Note: RETURN is hooked below!!!
    prior "BLOCK! or GROUP! that last invocation of HOST-CONSOLE requested"
        [blank! block! group!]
    result "Unevaluated result of evaluating PRIOR, or error"
        [~null~ word! group! error!]  ; null if never ran before
    resumable "Is the RESUME function allowed to exit this console"
        [logic!]
][
    ; We hook the RETURN function so that it actually returns an instruction
    ; that the code can build up from multiple EMIT statements.

    instruction: copy []

    emit: function [
        {Builds up sandboxed code to submit to C, hooked RETURN will finalize}

        return: [~]
        item "ISSUE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! issue! text!]
        <with> instruction
    ][
        switch type of item [
            issue! [
                if not empty? instruction [append/line instruction [()]]
                insert instruction item
            ]
            text! [
                append/line instruction compose [comment (item)]
            ]
            block! [
                if not empty? instruction [append/line instruction [()]]
                append/line instruction compose/deep <*> item
            ]
            fail
        ]
    ]

    return: function [
        {Hooked RETURN function which finalizes any gathered EMIT lines}

        return: []
        state "Describes the RESULT that the next call to HOST-CONSOLE gets"
            [integer! tag! group! datatype! path!]
        <with> instruction prior
        <local> return-to-c (:return) ;-- capture HOST-CONSOLE's RETURN
    ][
        switch state [
            <prompt> [
                emit [system/console/print-gap]
                emit [system/console/print-prompt]
                emit [reduce [
                    any [system/console/input-hook  '~escape~]
                ]] ;-- gather first line (or null), put in BLOCK!
            ]
            <halt> [
                emit [halt]
                emit [fail {^-- Shouldn't get here, due to HALT}]
            ]
            <die> [
                emit [quit 1] ;-- catch-all bash code for general errors
                emit [fail {^-- Shouldn't get here, due to QUIT}]
            ]
            <bad> [
                emit #no-unskin-if-error
                emit [print (<*> mold meta prior)]
                emit [fail ["Bad REPL continuation:" ((<*> meta result))]]
            ]
        ] then [
            return-to-c instruction
        ]

        return-to-c switch type of state [
            integer! [ ;-- just tells the calling C loop to exit() process
                assert [empty? instruction]
                state
            ]
            datatype! [ ;-- type assertion, how to enforce this?
                instruction
            ]
            group! [ ;-- means "submit user code"
                assert [empty? instruction]
                state
            ]
            path! [ ;-- means "resume instruction"
                assert [empty? instruction]
                state
            ]
            default [
                emit [fail [{Bad console instruction:} (<*> mold state)]]
            ]
        ]
    ]

    if not prior [
        ;
        ; !!! This was the first call before, and it would do some startup.
        ; Now it's probably reasonable to assume if there's anything to be
        ; done on a first call (printing notice of "you broke into debug" or
        ; something like that) then whoever broke into the REPL takes
        ; care of that.
        ;
        assert [result = null]
        if unset? 'system/console or [not system/console] [
            emit [start-console]
        ]
        return <prompt>
    ]

    ; BLOCK! code execution represents an instruction sent by the console to
    ; itself.  Some #directives may be at the head of these blocks.
    ;
    directives: collect [
        if block? prior [
            parse prior [opt some [i: issue! (keep i)] to <end>]
        ]
    ]

    if find directives #start-console [
        emit [start-console]
        return <prompt>
    ]

    ; QUIT handling (uncaught THROW/NAME with the name as the QUIT ACTION!)
    ;
    ; !!! R3-Alpha permitted arbitrary values for parameterized QUIT, which
    ; would be what DO of a script would return.  But if not an integer,
    ; they have to be mapped to an operating system exit status:
    ;
    ; https://en.wikipedia.org/wiki/Exit_status
    ;
    all [
        error? :result
        result/id = 'no-catch
        :result/arg2 = :quit ;; name
    ] then [
        return switch type of :result/arg1 [
            null [0] ;-- plain QUIT, no /WITH, call that success

            blank! [0] ;-- consider blank also to be success

            integer! [result/arg1] ;-- Note: may be too big for status range

            error! [1] ;-- !!! integer error mapping deprecated

            default [1] ;-- generic error code
        ]
    ]

    ; HALT handling (uncaught THROW/NAME with the name as the HALT ACTION!)
    ;
    all [
        error? :result
        result/id = 'no-catch
        :result/arg2 = :halt ;; name
    ] then [
        if find directives #quit-if-halt [
            return 128 + 2 ; standard cancellation exit status for bash
        ]
        if find directives #console-if-halt [
            emit [start-console]
            return <prompt>
        ]
        if find directives #unskin-if-halt [
            print "** UNSAFE HALT ENCOUNTERED IN CONSOLE SKIN"
            print "** REVERTING TO DEFAULT SKIN"
            system/console: make console! []
            print mold prior ;-- Might help debug to see what was running
        ]
        emit #unskin-if-halt
        emit [system/console/print-halted]
        return <prompt>
    ]

    ; RESUME handling (uncaught THROW/NAME with the name as RESUME ACTION!)
    ;
    all [
        error? :result
        result/id = 'no-catch
        :result/arg2 = :RESUME ;; name
    ] then [
        assert [path? :result/arg1]
        if not resumable [
            e: make error! "Can't RESUME top-level CONSOLE (use QUIT to exit)"
            e/near: result/near
            e/where: result/where
            emit [system/console/print-error (<*> meta e)]
            return <prompt>
        ]
        return :result/arg1
    ]

    if error? :result [ ;-- all other errors
        ;
        ; Errors can occur during HOST-START, before the SYSTEM/CONSOLE has
        ; a chance to be initialized (it may *never* be initialized if the
        ; interpreter is being called non-interactively from the shell).
        ;
        if object? system/console [
            emit [system/console/print-error (<*> meta result)]
        ] else [
            emit [print [form (<*> meta result)]]
        ]
        if find directives #die-if-error [
            return <die>
        ]
        if find directives #halt-if-error [
            return <halt>
        ]
        if find directives #countdown-if-error [
            emit #console-if-halt
            emit [
                print newline
                print "** Hit Ctrl-C to break into the console in 5 seconds"

                count-up n 25 [
                    if remainder n 5 = 1 [
                        write-stdout form (5 - to-integer (n / 5))
                    ] else [
                        write-stdout "."
                    ]
                    wait 0.25
                ]
                print newline
            ]
            emit {Only gets here if user did not hit Ctrl-C}
            return <die>
        ]
        if block? prior [
            case [
                find directives #host-console-error [
                    print "** HOST-CONSOLE ACTION! ITSELF RAISED ERROR"
                    print "** SAFE RECOVERY NOT LIKELY, BUT TRYING ANYWAY"
                ]
                not find directives #no-unskin-if-error [
                    print "** UNSAFE ERROR ENCOUNTERED IN CONSOLE SKIN"
                ]
                print mold result
            ] then [
                print "** REVERTING TO DEFAULT SKIN"
                system/console: make console! []
                print mold prior ;-- Might help debug to see what was running
            ]
        ]
        return <prompt>
    ]

    result: unmeta result

    if group? prior [ ;-- plain execution of user code
        emit [system/console/print-result ((<*> meta get/any 'result))]
        return <prompt>
    ]

    ; If PRIOR is BLOCK!, this is a continuation the console sent to itself.
    ; RESULT can be:
    ;
    ; GROUP! - code to be run in a sandbox on behalf of the user
    ; BLOCK! - block of gathered input lines so far, need another one
    ;
    assert [block? prior]

    if group? result [
        if empty? result [return <prompt>] ;-- user just hit enter, don't run
        return result ;-- GROUP! signals we're running user-requested code
    ]

    if not block? result [
        return <bad>
    ]

    ;-- INPUT-HOOK ran, block of strings ready

    assert [not empty? result] ;-- should have at least one item

    if '~escape~ = last result [
        ;
        ; It was aborted.  This comes from ESC on POSIX (which is the ideal
        ; behavior), Ctrl-D on Windows (because ReadConsole() can't trap ESC),
        ; Ctrl-D on POSIX (just to be compatible with Windows).
        ;
        take/last result
        return <prompt>
    ]

    sys/util/rescue [
        ;
        ; Note that LOAD/ALL makes BLOCK! even for a single item,
        ; e.g. `load/all "word"` => `[word]`
        ;
        code: load/all delimit newline result
        assert [block? code]

    ] then arrow error [
        ;
        ; If loading the string gave back an error, check to see if it
        ; was the kind of error that comes from having partial input
        ; (scan-missing).  If so, CONTINUE and read more data until
        ; it's complete (or until an empty line signals to just report
        ; the error as-is)
        ;
        if error/id = 'scan-missing [
            ;
            ; Error message tells you what's missing, not what's open and
            ; needs to be closed.  Invert the symbol.
            ;
            switch error/arg1 [
                "}" ["{"]
                ")" ["("]
                "]" ["["]
            ] also arrow unclosed [
                ;
                ; Backslash is used in the second column to help make a
                ; pattern that isn't legal in Rebol code, which is also
                ; uncommon in program output.  This enables detection of
                ; transcripts, potentially to replay them without running
                ; program output or evaluation results.
                ;
                write-stdout unspaced [unclosed #"\" space space]
                emit compose/deep [reduce [
                    (result)
                    any [system/console/input-hook  '~escape~]
                ]]

                return block!
            ]
        ]

        ; Could be an unclosed double quote (unclosed tag?) which more input
        ; on a new line cannot legally close ATM
        ;
        emit [system/console/print-error (<*> meta error)]
        return <prompt>
    ]

    if shortcut: select system/console/shortcuts maybe first code [
        ;
        ; Shortcuts like `q => [quit 0]`, `d => [dump]`
        ;
        if (bound? code/1) and [set? code/1] [
            ;
            ; Help confused user who might not know about the shortcut not
            ; panic by giving them a message.  Reduce noise for the casual
            ; shortcut by only doing so when a bound variable exists.
            ;
            emit [system/console/print-warning (<*>
                spaced [
                    uppercase to text! code/1
                        "interpreted by console as:" mold :shortcut
                ]
            )]
            emit [system/console/print-warning (<*>
                spaced ["use" to get-word! code/1 "to get variable."]
            )]
        ]
        take code
        insert code shortcut
    ]

    ; Run the "dialect hook", which can transform the completed code block
    ;
    emit #unskin-if-halt ;-- Ctrl-C during dialect hook is a problem
    emit [as group! system/console/dialect-hook ((<*> code))]
    return group! ;-- a group RESULT should come back to HOST-CONSOLE
]


why: function [
    "Explain the last error in more detail."
    return: [~]
    'err [<end> word! path! error!] "Optional error value"
][
    err: default [system/state/last-error]

    if match [word! path!] err [
        err: get err
    ]

    if error? err [
        say-browser
        err: lowercase unspaced [err/type #"-" err/id]
        browse join http://www.rebol.com/r3/docs/errors/ unspaced [err ".html"]
    ] else [
        print "No information is available."
    ]
]

; !!! It should likely be the case that the namespace for the user natives in
; an extension would be shared with the Rebol code for a module, but there's
; also a likely need to be able to have several source-level Rebol files
; (and possibly several independent modules) in an extension.  This hasn't
; been completely hammered out yet.
;
; As a result, for the C code in %mod-console.c to be able to find the Rebol
; entry point for its mechanics, we export it to lib.  But this needs a much
; better solution.
;
append lib compose [ext-console-impl: (:ext-console-impl)]

; !!! The whole host startup/console is currently very manually loaded
; into its own isolated context by the C startup code.  This way, changes
; to functions the console loop depends on (like PRINT or INPUT) that the
; user makes will not break the console's functionality.  It would be
; better if it used the module system, but since it doesn't, it does not
; have a place to put "exports" to lib or user.  We'd like people to be
; able to access the ABOUT, WHY, and USAGE functions... so export them
; here to LIB.  Again--this should be done by making this a module!
;
append lib compose [
    ;
    ;-- These must be <with>'d to be exported, otherwise the ABOUT: in
    ;-- the object key would be gathered as a local.
    ;
    why: (ensure action! :why)
]
