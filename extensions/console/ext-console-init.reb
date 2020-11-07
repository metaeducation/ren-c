REBOL [
    Title: "Console Extension (Rebol's Read-Eval-Print-Loop, ie. REPL)"

    Name: console
    Type: Module

    Options: []  ; !!! If ISOLATE, wouldn't see LIB/PRINT changes, etc.

    Rights: {
        Copyright 2016-2018 Ren-C Open Source Contributors
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
        main function (see %main/main.c).  HOST-CONSOLE does not itself run
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
    ; !!! Duplicates code in %main-startup.reb, where this isn't exported.
    enclose 'print func [f] [if not system/options/quiet [do f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    ; !!! Duplicates code in %main-startup.reb, where this isn't exported.
    enclose 'print func [f] [if system/options/verbose [do f]]
)


; Define base console! behaviors.  Custom console skins derive from this.
;
; If a console skin has an error while running, the error will be trapped and
; the system will revert to using a copy of this base object.
;
console!: make object! [
    name: _
    repl: true  ; used to identify this as a console! object
    is-loaded: false  ; if true then this is a loaded (external) skin
    was-updated: false  ; if true then console! object found in loaded skin
    last-result: ~startup~  ; last evaluated result (sent by HOST-CONSOLE)

    === APPEARANCE (can be overridden) ===

    prompt: {>>}
    result: {==}
    warning: {!!}
    error: {**}  ; errors FORM themselves, so this is not used yet
    info: {(i)}  ; was `to-text #{e29398}` for "(i)" symbol, caused problems
    greeting:
{Welcome to Rebol.  For more information please type in the commands below:

  HELP    - For starting information
  ABOUT   - Information about your Rebol
  CHANGES - What's different about this version}

    print-greeting: method [
        return: <void>
        {Adds live elements to static greeting content (build #, version)}
    ][
        boot-print [
            "Rebol 3 (Ren-C branch)"
            mold compose [version: (system/version) build: (system/build)]
            newline
        ]

        boot-print greeting
    ]

    print-prompt: method [return: <void>] [
        ;
        ; Note: See example override in skin in the Debugger extension, which
        ; adds the stack "level" number and "current" function name.

        ; We don't want to use PRINT here because it would put the cursor on
        ; a new line.
        ;
        write-stdout unspaced prompt
        write-stdout space
    ]

    print-result: method [return: <void> v [<opt> any-value!]] [
        switch match void! last-result: get/any 'v [
            null [
                ; not a void, fall through to other printing
            ]
            ~ [
                return  ; e.g. result of HELP, understood as output *nothing*
            ]
        ] else [  ; any other labeled VOID!
            print [result mold get/any 'v]
            return
        ]

        case [
            null? :v [
                print "; null"  ; no representation, use comment
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
            loop 20 [
                pos: next (find pos newline else [break])
            ] then [  ; e.g. didn't break
                insert clear pos "..."
            ]
            print [result (molded)]
        ]
    ]

    print-warning: method [s] [print [warning reduce s]]

    print-error: method [e [error!]] [
        if :e/file = 'tmp-boot.r [
            e/file: e/line: _  ; errors in console showed this, junk
        ]
        print [e]
    ]

    print-halted: method [] [
        print "[interrupted by Ctrl-C or HALT instruction]"
    ]

    print-info: method [s] [print [info reduce s]]

    print-gap: method [] [print newline]

    === BEHAVIOR (can be overridden) ===

    input-hook: method [
        {Receives line input, parse/transform, send back to CONSOLE eval}

        return: "null if canceled, otherwise processed text line input"
            [<opt> text!]
    ][
        ask text!
    ]

    dialect-hook: method [
        {Receives code block, parse/transform, send back to CONSOLE eval}
        b [block!]
    ][
        ; By default we do nothing.  But see the Debug console skin for
        ; example of binding the code to the currently "focused" FRAME!, or
        ; this example on the forum of injecting the last value:
        ;
        ; https://forum.rebol.info/t/1071

        b
    ]

    shortcuts: make object! compose/deep [
        d: [dump]
        h: [help]
        q: [quit]
        dt: [delta-time]
        dp: [delta-profile]

        list-shortcuts: [print [system/console/shortcuts]]
        changes: [
            browse join https://github.com/metaeducation/ren-c/blob/master/ [
                %CHANGES.md "#"
                system/version/1 system/version/2 system/version/3
            ]
        ]
        topics: [
            browse https://r3n.github.io/topics/
        ]
    ]

    === HELPERS (could be overridden!) ===

    add-shortcut: method [
        {Add/Change console shortcut}
        return: <void>
        name [any-word!] "Shortcut name"
        block [block!] "Command(s) expanded to"
    ][
        extend shortcuts name block
    ]
]


start-console: function [
    "Called when a REPL is desired after command-line processing, vs quitting"

    return: <void>
    /skin "Custom skin (e.g. derived from MAKE CONSOLE!) or file"
        [file! object!]
    <static>
        o (system/options)  ; shorthand since options are often read/written
][
    === MAKE CONSOLE! INSTANCE FOR SKINNING ===

    ; Instantiate console! object into system/console.  This is updated via
    ; %console-skin.reb if in system/options/resources

    skin-file: case [
        file? skin [skin]
        object? skin [blank]
        default [%console-skin.reb]
    ]

    loud-print "Starting console..."
    loud-print newline
    proto-skin: match object! skin else [make console! []]
    skin-error: _

    all [
        skin-file
        not find o/suppress skin-file
        o/resources
        exists? skin-file: join o/resources skin-file
    ] then [
        trap [
            new-skin: do load skin-file

            ; if loaded skin returns console! object then use as prototype
            all [
                object? new-skin
                select new-skin 'repl  ; quacks like REPL, it's a console!
            ] then [
                proto-skin: new-skin
                proto-skin/was-updated: true
                proto-skin/name: default ["updated"]
            ]

            proto-skin/is-loaded: true
            proto-skin/name: default ["loaded"]
            append o/loaded skin-file

        ] then e -> [
            skin-error: e  ; show error later if `--verbose`
            proto-skin/name: "error"
        ]
    ]

    proto-skin/name: default ["default"]

    system/console: proto-skin

    === HOOK FOR HELP ABOUT LAST ERROR ===

    ; The WHY command lets the user get help about the last error printed.
    ; To do so, it has to save the last error.  Adjust the error printing
    ; hook to save the last error printed.  Also inform people of the
    ; existence of the WHY function on the first error delivery.
    ;
    proto-skin/print-error: adapt :proto-skin/print-error [
        if not system/state/last-error [
            system/console/print-info "Info: use WHY for error information"
        ]

        system/state/last-error: e
    ]

    === PRINT BANNER ===

    if o/about [
        boot-print make-banner boot-banner  ; the fancier banner
    ]

    system/console/print-greeting

    === VERBOSE CONSOLE SKINNING MESSAGES ===

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
            "(CONSOLE" if not proto-skin/was-updated [{not}] "updated)"
        ]
    ]
]


ext-console-impl: function [
    {Rebol ACTION! that is called from C in a loop to implement the console}

    return: "Code for C caller to sandbox, exit status, RESUME code, or hook"
        [block! group! integer! sym-group! handle!]  ; RETURN is hooked below!
    prior "BLOCK! or GROUP! that last invocation of HOST-CONSOLE requested"
        [blank! block! group!]
    result "Quoted result from evaluating PRIOR, or non-quoted error"
        [blank! quoted! error!]
    resumable "Is the RESUME function allowed to exit this console"
        [logic!]
    skin "Console skin to use if the console has to be launched"
        [<opt> object! file!]
][
    === HOOK RETURN FUNCTION TO GIVE EMITTED INSTRUCTION ===

    ; The C caller can be given a BLOCK! representing an code the console is
    ; executing on its own behalf, as part of its "skin".  Building these
    ; blocks is made easier by collaboration between EMIT and a hooked version
    ; of the underlying RETURN of this function.

    instruction: copy []

    emit: function [
        {Builds up sandboxed code to submit to C, hooked RETURN will finalize}

        item "ISSUE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! issue! text!]
        <with> instruction
    ][
        switch type of item [
            issue! [
                if not empty? instruction [append/line instruction ',]
                insert instruction item
            ]
            text! [
                append/line instruction compose [comment (item)]
            ]
            block! [
                if not empty? instruction [append/line instruction ',]
                append/line instruction compose/deep <*> item
            ]
            fail
        ]
    ]

    return: function [
        {Hooked RETURN function which finalizes any gathered EMIT lines}

        state "Describes the RESULT that the next call to HOST-CONSOLE gets"
            [integer! tag! group! datatype! sym-group! handle!]
        <with> instruction prior
        <local> return-to-c (:return)  ; capture HOST-CONSOLE's RETURN
    ][
        switch state [
            <prompt> [
                emit [system/console/print-gap]
                emit [system/console/print-prompt]
                emit [reduce [
                    try system/console/input-hook
                ]]  ; gather first line (or null), put in BLOCK!
            ]
            <halt> [
                emit [halt]
                emit [fail {^-- Shouldn't get here, due to HALT}]
            ]
            <die> [
                emit [quit 1]  ; bash exit code for any generic error
                emit [fail {^-- Shouldn't get here, due to QUIT}]
            ]
            <bad> [
                emit #no-unskin-if-error
                emit [print mold '(<*> prior)]
                emit [fail ["Bad REPL continuation:" (<*> result)]]
            ]
        ] then [
            return-to-c instruction
        ]

        return-to-c switch type of state [
            integer! [  ; just tells the calling C loop to exit() process
                assert [empty? instruction]
                state
            ]
            datatype! [  ; type assertion, how to enforce this?
                emit spaced ["^-- Result should be" an state]
                instruction
            ]
            group! [  ; means "submit user code"
                assert [empty? instruction]
                state
            ]
            sym-group! [  ; means "resume instruction"
                state
            ]
            handle! [  ; means "evaluator hook request" (handle is the hook)
                state
            ]
            default [
                emit [fail [{Bad console instruction:} (<*> mold state)]]
            ]
        ]
    ]

    === DO STARTUP HOOK IF THIS IS THE FIRST TIME THE CONSOLE HAS RUN ===

    if not prior [
        ;
        ; !!! This was the first call before, and it would do some startup.
        ; Now it's probably reasonable to assume if there's anything to be
        ; done on a first call (printing notice of "you broke into debug" or
        ; something like that) then whoever broke into the REPL takes
        ; care of that.
        ;
        assert [blank? :result]
        any [
            undefined? 'system/console
            not system/console
        ] then [
            emit [start-console/skin '(<*> skin)]
        ]
        return <prompt>
    ]

    === GATHER DIRECTIVES ===

    ; #directives may be at the head of BLOCK!s the console ran for itself.
    ;
    directives: collect [
        if block? prior [
            parse prior [some [set i: issue! (keep i)] end]
        ]
    ]

    if find directives #start-console [
        emit [start-console/skin '(<*> skin)]
        return <prompt>
    ]

    === QUIT handling ===

    ; https://en.wikipedia.org/wiki/Exit_status

    all [
        error? :result
        result/id = 'no-catch
        :result/arg2 = :QUIT  ; throw's /NAME
    ] then [
        return switch type of get* 'result/arg1 [
            void! [0]  ; plain QUIT, no /WITH, call that success

            logic! [either :result/arg1 [0] [1]]  ; logic true is success

            integer! [result/arg1]  ; Note: may be too big for status range

            error! [1]  ; currently there's no default error-to-int mapping

            default [1]  ; generic error code
        ]
    ]

    === HALT handling (e.g. Ctrl-C or Escape) ===

    all [
        error? :result
        result/id = 'no-catch
        :result/arg2 = :HALT  ; throw's /NAME
    ] then [
        if find directives #quit-if-halt [
            return 128 + 2 ; standard cancellation exit status for bash
        ]
        if find directives #console-if-halt [
            emit [start-console/skin '(<*> skin)]
            return <prompt>
        ]
        if find directives #unskin-if-halt [
            print "** UNSAFE HALT ENCOUNTERED IN CONSOLE SKIN"
            print "** REVERTING TO DEFAULT SKIN"
            system/console: make console! []
            print mold prior  ; Might help debug to see what was running
        ]
        emit #unskin-if-halt
        emit [system/console/print-halted]
        return <prompt>
    ]

    === RESUME handling ===

    ; !!! This is based on debugger work-in-progress.  A nested console that
    ; has been invoked via a breakpoint the console will sandbox most errors
    ; and throws.  But if it recognizes a special "resume instruction" being
    ; thrown, it will consider its nested level to be done and yield that
    ; result so the program can continue.

    all [
        in lib 'resume
        error? :result
        result/id = 'no-catch
        :result/arg2 = :LIB/RESUME  ; throw's /NAME
    ] then [
        assert [match [sym-group! handle!] :result/arg1]
        if not resumable [
            e: make error! "Can't RESUME top-level CONSOLE (use QUIT to exit)"
            e/near: result/near
            e/where: result/where
            emit [system/console/print-error (<*> e)]
            return <prompt>
        ]
        return :result/arg1
    ]

    if error? :result [  ; all other errors
        ;
        ; Errors can occur during MAIN-STARTUP, before the SYSTEM/CONSOLE has
        ; a chance to be initialized (it may *never* be initialized if the
        ; interpreter is being called non-interactively from the shell).
        ;
        if object? system/console [
            emit [system/console/print-error (<*> :result)]
        ] else [
            emit [print [(<*> :result)]]
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

                repeat n 25 [
                    if 1 = remainder n 5 [
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
                print mold prior  ; Might help debug to see what was running
            ]
        ]
        return <prompt>
    ]

    assert [quoted? :result]

    === HANDLE RESULT FROM EXECUTION OF CODE ON USER'S BEHALF ===

    if group? prior [
        emit [system/console/print-result (<*> result)]
        return <prompt>
    ]

    === HANDLE CONTINUATION THE CONSOLE SENT TO ITSELF ===

    assert [block? prior]

    ; `result` of console instruction can be:
    ;
    ; GROUP! - code to be run in a sandbox on behalf of the user
    ; BLOCK! - block of gathered input lines so far, need another one
    ;
    result: unquote result

    if group? result [
        return result  ; GROUP! signals we're running user-requested code
    ]

    if not block? result [
        return <bad>
    ]

    === TRY ADDING LINE OF INPUT TO CODE REGENERATED FROM BLOCK ===

    ; Note: INPUT-HOOK has already run once per line in this block

    assert [not empty? result]  ; should have at least one item

    if blank? last result [
        ;
        ; It was aborted.  This comes from ESC on POSIX (which is the ideal
        ; behavior), Ctrl-D on Windows (because ReadConsole() can't trap ESC),
        ; Ctrl-D on POSIX (just to be compatible with Windows).
        ;
        emit [system/console/print-result ~]
        return <prompt>
    ]

    trap [
        ; Note that LOAD/ALL makes BLOCK! even for a single item,
        ; e.g. `load/all "word"` => `[word]`
        ;
        code: load/all delimit newline result
        assert [block? code]

    ] then error -> [
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
            ] also unclosed -> [
                ;
                ; Backslash is used in the second column to help make a
                ; pattern that isn't legal in Rebol code, which is also
                ; uncommon in program output.  This enables detection of
                ; transcripts, potentially to replay them without running
                ; program output or evaluation results.
                ;
                write-stdout unspaced [unclosed "\" _ _]
                emit [reduce [  ; reduce will runs in sandbox
                    ((<*> result))  ; splice previous inert literal lines
                    system/console/input-hook  ; hook to run in sandbox
                ]]

                return block!
            ]
        ]

        ; Could be an unclosed double quote (unclosed tag?) which more input
        ; on a new line cannot legally close ATM
        ;
        emit [system/console/print-error (<*> error)]
        return <prompt>
    ]

    === HANDLE CODE THAT HAS BEEN SUCCESSFULLY LOADED ===

    if shortcut: select system/console/shortcuts try first code [
        ;
        ; Shortcuts like `q => [quit]`, `d => [dump]`
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
    emit #unskin-if-halt  ; Ctrl-C during dialect hook is a problem
    emit [
        comment {not all users may want CONST result, review configurability}
        as group! system/console/dialect-hook (<*> code)
    ]
    return group!  ; a group RESULT should come back to HOST-CONSOLE
]


why: function [
    "Explain the last error in more detail."
    return: <void>
    'err [<end> word! path! error!] "Optional error value"
][
    err: default [system/state/last-error]

    if match [word! path!] err [
        err: get err
    ]

    if error? err [
        err: lowercase unspaced [err/type #"-" err/id]
        browse join http://www.rebol.com/r3/docs/errors/ [err ".html"]
    ] else [
        print "No information is available."
    ]
]


upgrade: function [
    "Check for newer versions."
    return: <void>
][
    ; Should this be a console-detected command, like Q, or is it meaningful
    ; to define this as a function you could call from code?
    ;
    do <upgrade>
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
append lib compose [
    console!: (ensure object! console!)
    ext-console-impl: (:ext-console-impl)
]

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
    ; These must be <with>'d to be exported, otherwise the ABOUT: in
    ; the object key would be gathered as a local.
    ;
    why: (ensure action! :why)
    upgrade: (ensure action! :upgrade)
]
