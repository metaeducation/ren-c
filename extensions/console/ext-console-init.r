Rebol [
    title: "Console Extension (Rebol's Read-Eval-Print-Loop, ie. REPL)"

    name: Console
    type: module

    rights: --[
        Copyright 2016-2025 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    ]--

    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--

    description: --[
        This is a rich, skinnable console for Rebol--where basically all the
        implementation is itself userspace Rebol code.  Documentation for the
        skinning hooks exist here:

        https://github.com/r3n/reboldocs/wiki/User-and-Console

        The HOST-CONSOLE Rebol function is invoked in a loop by a small C
        main function (see %main/main.c).  HOST-CONSOLE does not itself run
        arbitrary user code with DO.  That would be risky, because it actually
        is not allowed to panic or be canceled with Ctrl-C.  Instead, it just
        gathers input...and produces a block which is returned to C to
        actually execute.

        This design allows the console to sandbox potentially misbehaving
        skin code, and fall back on a default skin if there is a problem.
        It also makes sure that that user code doesn't see the console's
        implementation in its backtrace.

        The default INPUT-HOOK uses READ-LINE from the Stdio extension.  That
        implementation of READ-LINE is based on reading keystrokes one at a
        time on Windows and Linux (vs. using APIs that read entire lines at
        once), so it could theoretically offer more flexibility such as tab
        completion--but hooks to expose that haven't been developed.
    ]--
]


boot-print: redescribe [
    "Prints during boot when not quiet."
](
    ; !!! Duplicates code in %main-startup.r, where this isn't exported.
    enclose print/ f -> [if no? system.options.quiet [eval f]]
)

loud-print: redescribe [
    "Prints during boot when verbose."
](
    ; !!! Duplicates code in %main-startup.r, where this isn't exported.
    enclose print/ f -> [if yes? system.options.verbose [eval f]]
)


; Define base console! behaviors.  Custom console skins derive from this.
;
; If a console skin has an error while running, the error will be trapped and
; the system will revert to using a copy of this base object.
;
; !!! We EXPORT the CONSOLE! object, because the concept was that users should
; be able to create new instances of the console object.  What they'd do with
; it hasn't really been worked through yet (nested REPLs?)  Review.
;
export console!: make object! [
    name: null
    repl: 'true  ; used to identify this as a console! object
    is-loaded: 'no  ; if yes then this is a loaded (external) skin
    was-updated: 'no  ; if yes then console! object found in loaded skin
    last-result: ~  ; last evaluated result (sent by HOST-CONSOLE)

    === APPEARANCE (can be overridden) ===

    prompt: ">>"
    result: "=="
    warning: "!!"
    error: "**"  ; errors FORM themselves, so this is not used yet
    info: "(i)"  ; was `to-text #{e29398}` for "(i)" symbol, caused problems
    greeting:
--[Welcome to Rebol.  For more information please type in the commands below:

  HELP    - For starting information
  ABOUT   - Information about your Rebol]--

    print-greeting: proc [
        "Adds live elements to static greeting content (build #, version)"
        <.>
    ][
        boot-print [
            "Rebol 3 (Ren-C branch)"
            mold compose [version: (system.version) build: (system.build)]
            newline
        ]

        boot-print greeting
    ]

    print-prompt: proc [<.>] [
        ;
        ; Note: See example override in skin in the Debugger extension, which
        ; adds the stack "level" number and "current" function name.

        ; We don't want to use PRINT here because it would put the cursor on
        ; a new line.
        ;
        write stdout unspaced prompt
        write stdout space
    ]

    print-result: func [
        ^v "Value (done with meta parameter to handle unstable isotopes)"
            [any-value?]
        <.>
    ][
        ignore ^last-result: ^v  ; don't decay, suppress ERROR! propagation

        === HANDLE ERROR! FIRST ===

        ; Typical type checks (e.g. INTEGER?, GHOST? and such) will fail on
        ; ERROR!, so test for these first.
        ;
        ; Note that this is a definitional error...not a panic/exception, so
        ; PRINT-ERROR is distinct from PRINT-PANIC.

        if error? ^v [
            print-error disarm ^v
            return ~
        ]

        === GHOSTS! ===

        if ghost? ^v [
            print unspaced [result _ "\~,~\" _ _ ";" _ "antiform (ghost!)"]
            return ~
        ]

        === UNPACK FIRST VALUE IN "PACKS" ===

        ; Block antiforms represent multiple returns.  Only the first output
        ; is printed--but with a comment saying that it
        ; was in a pack.  This hints the user to do a ^META on the value to
        ; see the complete pack.
        ;
        ; 0-length packs (~[]~ antiform, a.k.a. "void") mold like antiforms.

        if pack? ^v [
            if void? ^v [  ; mold like a regular antiform, for now
                print unspaced [
                    result _ --[\~[]~\  ; antiform (pack!) "void"]--
                ]
                return ~
            ]

            if not decayable? ^v [
                print "; undecayable pack"
                v: unanti ^v
                print unspaced [
                    result _ "\" mold quasi v "\" _ _ ";" _ "antiform (pack!)"
                ]
                return ~
            ]

            print ["; first in pack of length" length of unanti ^v]
            v: decay ^v
        ]

        === PRINT NO OUTPUT FOR TRASH! (antiform RUNE!) ===

        ; By default, we've decided that some state(s) need to not display in
        ; the console, to suppress output from things like HELP or PRINT,
        ; because it's noisy to have `== xxx` produced in that case.
        ;
        ; (Of course, a console customization could be done which displayed
        ; all results...but the default should look somewhat streamlined.)
        ;
        ; Whatever doesn't display will be a "lie" in some sense.  The two
        ; competing lies are VOID (a.k.a. empty block antiform, result of
        ; things like `eval []`) and TRASH (a.k.a. the antiforms of RUNE!).
        ;
        ; The decision has flipped many times, but trash wins:
        ;
        ; https://forum.rebol.info/t/console-treatment-of-void-vs-trash/2045
        ;
        ; TRASH? can hold arbitrary message content as an antiform RUNE!, but
        ; the most succinct state of trash is antiform space (TRIPWIRE, whose
        ; quasiform looks like a single tilde `~`).  Right now we don't show
        ; any trash, regardless of whether it contains a label or not.

        if trash? ^v [
            return ~
        ]

        === ANTIFORMS (^META v parameter means they are quasiforms) ===

        if antiform? ^v [
            ;
            ; Antiforms don't technically have "a representation", but the
            ; historical console behavior is to add a comment annotation.
            ;
            ;     >> eval [~something~]
            ;     == ~something~  ; anti
            ;
            ; Antiforms are evaluative products only, so you won't see the
            ; annotation for anything you picked out of a block:
            ;
            ;     >> first [~something~]
            ;     == ~something~
            ;
            let name: switch type of unanti ^v [
                ; error and ghost handled above
                group! ["splice"]
                group! ["datatype"]
                frame! ["action"]
                word! ["keyword"]
            ] else ["unknown"]

            v: lift ^v  ; turn antiform into quasiform
            print unspaced [
                result _ "\" mold v "\" _ _ ";" _ "antiform (" name "!)"
            ]
            return ~
        ]

        === "ORDINARY" VALUES (non-antiform) ===

        case [
            free? v [
                ;
                ; Molding a freed value would cause an error...which is
                ; usually okay (you shouldn't be working with freed series)
                ; but if we didn't special case it here, the error would seem
                ; to be in the console code itself.
                ;
                print-panic make warning! "Series data unavailable due to FREE"
            ]

            port? v [
                ; PORT!s are returned by many operations on files, to
                ; permit chaining.  They contain many fields so their
                ; molding is excessive, and there's not a ton to learn
                ; about them.  Cut down the output more than the mold:limit.
                ;
                print [result "&[port! [...] [...]]"]
            ]
        ]
        else [
            ; print the first 20 lines of the first 2048 characters of mold
            ;
            let molded: mold:limit v 2048
            let pos: molded
            repeat 20 [
                pos: next (find pos newline else [break])
            ] then [  ; e.g. didn't break
                insert clear pos "..."
            ]
            print [result (molded)]
        ]

        return ~
    ]

    print-warning: proc [s <.>] [print [warning reduce s]]

    print-error: proc [e [warning!] <.>] [
        comment [  ; MOLD is more informative, but messy
            v: unanti ^v
            print unspaced [
                result _ "\" mold quasi v "\" _ _ ";" _ "antiform (error!)"
            ]
        ]
        print ["** Error:" form e]
        if try e.id [
            print ["** Id:" mold e.id]
        ]
    ]

    print-panic: proc [e [warning!] <.>] [
        if e.file = 'tmp-boot.r [
            e.file: e.line: null  ; errors in console showed this, junk
        ]
        print ["** PANIC:" form e]
        if try e.id [
            print ["** Id:" mold e.id]
        ]
        if try e.where [
            print ["** Where:" mold e.where]
        ]
        if try e.near [
            print ["** Near:" mold e.near]
        ]
        if try e.file [
            print ["** File:" mold e.file]
        ]
        if try e.line [
            print ["** Line:" mold e.line]
        ]
    ]

    print-halted: proc [<.>] [
        print newline  ; interrupts happen anytime, clearer to start newline
        print "[interrupted by Ctrl-C or HALT instruction]"
    ]

    print-info: proc [s <.>] [print [info reduce s]]

    print-gap: proc [<.>] [print newline]

    === BEHAVIOR (can be overridden) ===

    input-hook: func [
        "Receives line input, parse and transform, send back to CONSOLE eval"

        return: "~escape~ if canceled, null on no input, else line of text"
            [<null> text! ~(~escape~)~]
        <.>
    ][
        return read-line stdin except ['~escape~]
    ]

    ; See the Debug console skin for example of binding to the currently
    ; "focused" FRAME!, or this example on the forum of last value injection:
    ;
    ;   https://forum.rebol.info/t/1071
    ;
    ; 1. The WRAP* operation will collect the top-level SET-WORD declarations
    ;    and add them to the passed in context, and then run the block bound
    ;    into that context.  This allows:
    ;
    ;        >> x: 10
    ;        == 10
    ;
    ;    But it does not allow:
    ;
    ;        >> (y: 20)
    ;        ** Error: y is not bound
    ;
    ;    See: https://forum.rebol.info/t/2128/6
    ;
    ; 2. We know the higher-level WRAP will create a context that inherits
    ;    from the block it's passed, and then run it.  But with the lower
    ;    level code working with modules, it's less clear--virtually binding
    ;    the module via a "USE!" won't get the inheritance, and if we make
    ;    the module the binding literally then that would have to override
    ;    whatever binding was on the block.  Leave it open for now, as these
    ;    unbound block cases aren't the only ones to consider.
    ;
    dialect-hook: func [
        "Receives full code block, bind and process, send back to CONSOLE eval"
        return: [block!]
        b [block!]
        <.>
    ][
        wrap* system.contexts.user b  ; expand w/top-level set-word [1]
        return inside system.contexts.user b  ; two operations for now [2]
    ]

    shortcuts: make object! compose:deep [
        d: [dump]
        h: [help]
        q: [quit 0]
        dt: [delta-time]
        dp: [delta-profile]

        list-shortcuts: [print [system.console.shortcuts]]
        changes: [
            let gitroot: https://github.com/metaeducation/ren-c/blob/master/
            browse join gitroot [
                %CHANGES.md "#"
                system.version.1 "." system.version.2 "." system.version.3
            ]
        ]
        topics: [
            browse https://r3n.github.io/topics/
        ]
    ]

    === HELPERS (could be overridden!) ===

    add-shortcut: proc [
        "Add/Change console shortcut"
        name [word!] "Shortcut name"
        block [block!] "Command(s) expanded to"
        <.>
    ][
        set (extend shortcuts name) block
    ]
]


start-console: proc [
    "Called when a REPL is desired after command-line processing, vs quitting"

    :skin "Custom skin (e.g. derived from MAKE CONSOLE!) or file"
        [file! object!]
]
bind construct [
    o: system.options  ; shorthand since options are often read or written
][
    === MAKE CONSOLE! INSTANCE FOR SKINNING ===

    ; Instantiate console! object into system.console.  This is updated via
    ; %console-skin.r if in system.options.resources

    let skin-file: case [
        file? opt skin [skin]
        object? opt skin [null]
    ] else [%console-skin.r]

    loud-print "Starting console..."
    loud-print newline
    let proto-skin: (match object! opt skin) else [make console! []]
    let skin-error: null

    all [
        skin-file
        not find opt o.suppress skin-file
        o.resources
        exists? skin-file: join o.resources skin-file
    ] then [
        let new-skin: do skin-file then [
            ; if loaded skin returns console! object then use as prototype
            all [
                object? new-skin
                select new-skin 'repl  ; quacks like REPL, it's a console!
            ] then [
                proto-skin: new-skin
                proto-skin.was-updated: 'yes
                proto-skin.name: default ["updated"]
            ]

            proto-skin.is-loaded: 'yes
            proto-skin.name: default ["loaded"]
            append o.loaded skin-file
        ]
        except e -> [
            skin-error: e  ; show error later if `--verbose`
            proto-skin.name: "error"
        ]
    ]

    proto-skin.name: default ["default"]

    system.console: proto-skin

    === HOOK FOR HELP ABOUT LAST ERROR ===

    ; The WHY command lets the user get help about the last error printed.
    ; To do so, it has to save the last error.  Adjust the error printing
    ; hook to save the last error printed.  Also inform people of the
    ; existence of the WHY function on the first error delivery.
    ;
    proto-skin.print-panic: adapt proto-skin.print-panic/ [
        if not system.state.last-error [
            system.console/print-info "Info: use WHY for error information"
        ]

        system.state.last-error: e
    ]

    === PRINT BANNER ===

    if yes? o.about [
        boot-print make-banner boot-banner  ; the fancier banner
    ]

    system.console/print-greeting

    === VERBOSE CONSOLE SKINNING MESSAGES ===

    loud-print [newline "Console skinning:" newline]
    if skin-error [
        loud-print [
            "  Error loading console skin  -" skin-file LF LF
            skin-error LF LF
            "  Fix error and restart CONSOLE"
        ]
    ] else [
       loud-print [
            space space
            if yes? proto-skin.is-loaded [
                "Loaded skin"
            ] else [
                "Skin does not exist"
            ]
            "-" skin-file
            "(CONSOLE" if no? proto-skin.was-updated ["not"] "updated)"
        ]
    ]
]


console*: func [
    "Rebol ACTION! that is called from C in a loop to implement the console"

    return: "Code for C caller to sandbox, exit status, RESUME code, or hook"
        [block! group! integer! ^group! handle!]  ; RETURN is hooked below!
    prior "BLOCK! or GROUP! that last invocation of HOST-CONSOLE requested"
        [<opt> block! group!]
    result "^META result from PRIOR eval, non-quoted error, or exit code #"
        [<opt> warning! quoted! quasiform! integer!]
    resumable "Is the RESUME function allowed to exit this console"
        [yesno?]
    skin "Console skin to use if the console has to be launched"
        [<opt> object! file!]
    {.}
][
    === HANDLE EXIT CODE ===

    ; We could do some sort of handling or cleanup here if we wanted to.
    ; Note that for simplicity's sake, the QUIT supplied by the CONSOLE to
    ; the user context only returns integer exit codes (otherwise we'd have
    ; to add a parameter for what the QUIT/VALUE was--or multiplex it into
    ; the result--making this code more complicated for little point.)

    if integer? opt result [  ; QUIT for console only returns exit statuses
        return result
    ]

    === HOOK RETURN FUNCTION TO GIVE EMITTED INSTRUCTION ===

    ; The C caller can be given a BLOCK! representing an code the console is
    ; executing on its own behalf, as part of its "skin".  Building these
    ; blocks is made easier by collaboration between EMIT and a hooked version
    ; of the underlying RETURN of this function.

    [.]: {
        instruction: copy []
    }

    let emit: proc [
        "Builds up sandboxed code to submit to C, hooked RETURN will finalize"

        item "RUNE! directive, TEXT! comment, (<*> composed) code BLOCK!"
            [block! rune! text!]
    ][
        switch:type item [
            rune! [
                if not empty? .instruction [append:line .instruction ',]
                insert .instruction item
            ]
            text! [
                append:line .instruction spread compose [comment (item)]
            ]
            block! [
                if not empty? .instruction [append:line .instruction ',]
                append:line .instruction spread (  ; use item's tip binding
                    compose2:deep inside item '@(<*>) item
                )
            ]
            panic
        ]
    ]

    return: func [
        "Hooked RETURN function which finalizes any gathered EMIT lines"

        state "Describes the RESULT that the next call to HOST-CONSOLE gets"
            [integer! tag! group! datatype! ^group! handle!]
        {
            return-to-c (return/)  ; capture HOST-CONSOLE's RETURN
        }
    ][
        switch state [
            <prompt> [
                emit [system.console/print-gap]
                emit [system.console/print-prompt]
                emit [reduce [
                    reify system.console/input-hook  ; [...],  ~escape~, null
                ]]  ; gather first line (or escape), put in BLOCK!
            ]
            <halt> [
                emit [halt]
                emit [panic "^^-- Shouldn't get here, due to HALT"]
            ]
            <die> [  ; run emitted code, *then* die
                emit #die-when-done
            ]
            <bad> [
                emit #no-unskin-if-error
                emit [print mold '(<*> prior)]
                emit [panic ["Bad REPL continuation:" (<*> result)]]
            ]
        ] then [
            return-to-c .instruction
        ]

        return-to-c switch:type state [
            integer! [  ; just tells the calling C loop to exit() process
                assert [empty? .instruction]
                state
            ]
            datatype! [  ; type assertion, how to enforce this?
                emit spaced ["^-- Result should be" to word! state]
                .instruction
            ]
            group! [  ; means "submit user code"
                assert [empty? .instruction]
                state
            ]
            group?:metaform/ [  ; means "resume instruction"
                state
            ]
            handle! [  ; means "evaluator hook request" (handle is the hook)
                state
            ]
        ] else [
            emit [panic ["Bad console instruction:" (<*> mold state)]]
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
        assert [not result]
        any [
            unset? $system.console
            not system.console
        ] then [
            emit [start-console:skin (<*> lift skin)]
        ]
        return <prompt>
    ]

    assert [result]  ; all other calls should provide a result

    === GATHER DIRECTIVES ===

    ; #directives may be at the head of BLOCK!s the console ran for itself.
    ;
    let directives: collect [
        let i
        if block? prior [
            parse3 prior [
                opt some [i: rune! (keep i)]
                accept (~)
            ]
        ]
    ]

    if find directives #die-when-done [
        return 1  ; just tell C to exit with status code 1... now
    ]

    if find directives #start-console [
        emit [start-console:skin (<*> lift skin)]
        return <prompt>
    ]

    === HALT handling (e.g. Ctrl-C) ===

    ; Note: Escape is handled during input gathering by a dedicated signal.

    all [
        warning? result
        result.id = 'no-catch
        result.arg2 = unrun lib.halt/  ; throw's :NAME
    ] then [
        if find directives #quit-if-halt [
            return 128 + 2 ; standard cancellation exit status for bash
        ]
        if find directives #console-if-halt [
            emit [start-console:skin (<*> lift skin)]
            return <prompt>
        ]
        if find directives #unskin-if-halt [
            emit [print "** UNSAFE HALT ENCOUNTERED IN CONSOLE SKIN"]
            emit [print "** REVERTING TO DEFAULT SKIN"]
            system.console: make console! []
            emit [print mold prior]  ; Might help debug to see what was running
        ]

        ; !!! This would add an "unskin if halt" which would stop you from
        ; halting the print response to the halt message.  But that was still
        ; in effect during <prompt> which is part of the same "transaction"
        ; as PRINT-HALTED.  To the extent this is a good idea, it needs to
        ; guard -only- the PRINT-HALTED and put the prompt in a new state.
        ;
        comment [emit #unskin-if-halt]

        emit [system.console/print-halted]
        return <prompt>
    ]

    === RESUME handling ===

    ; !!! This is based on debugger work-in-progress.  A nested console that
    ; has been invoked via a breakpoint the console will sandbox most errors
    ; and throws.  But if it recognizes a special "resume instruction" being
    ; thrown, it will consider its nested level to be done and yield that
    ; result so the program can continue.

    all [
        has lib 'resume
        warning? result
        result.id = 'no-catch
        result.arg2 = unrun lib.resume/  ; throw's :NAME
    ] then [
        assert [match [^group! handle!] result.arg1]
        if no? resumable [
            e: make warning! "Can't RESUME top-level CONSOLE (use QUIT to exit)"
            e.near: result.near
            e.where: result.where
            emit [system.console/print-panic (<*> e)]
            return <prompt>
        ]
        return result.arg1
    ]

    if warning? result [  ; all other panics
        ;
        ; Panics can occur during MAIN-STARTUP, before the system.CONSOLE has
        ; a chance to be initialized (it may *never* be initialized if the
        ; interpreter is being called non-interactively from the shell).
        ;
        if object? opt system.console [
            emit [system.console/print-panic (<*> result)]
        ] else [
            emit [console!/print-panic (<*> result)]  ; fallback to default
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
                        write stdout form (5 - to-integer (n / 5))
                    ] else [
                        write stdout "."
                    ]
                    wait 0.25
                ]
                print newline
            ]
            emit "Only gets here if user did not hit Ctrl-C"
            return <die>
        ]
        if block? prior [
            case [
                find directives #host-console-error [
                    emit [print "** HOST-CONSOLE ACTION! ITSELF RAISED ERROR"]
                    emit [print "** RECOVERY NOT LIKELY, BUT TRYING ANYWAY"]
                ]
                not find directives #no-unskin-if-error [
                    emit [print "** UNSAFE ERROR ENCOUNTERED IN CONSOLE SKIN"]
                ]
                emit [print mold (<*> result)]
            ] then [
                emit [print "** REVERTING TO DEFAULT SKIN"]
                system.console: make console! []
                emit [print mold (<*> prior)]  ; Might help to see what ran
            ]
        ]
        return <prompt>
    ]

    === HANDLE RESULT FROM EXECUTION OF CODE ON USER'S BEHALF ===

    if group? prior [
        emit [system.console/print-result (<*> result)]  ; result is meta
        return <prompt>
    ]

    === HANDLE CONTINUATION THE CONSOLE SENT TO ITSELF ===

    assert [block? prior]

    ; `result` of console instruction can be:
    ;
    ; GROUP! - code to be run in a sandbox on behalf of the user
    ; BLOCK! - block of gathered input lines so far, need another one
    ;
    result: unlift result

    if group? result [
        return result  ; GROUP! signals we're running user-requested code
    ]

    if not block? result [
        return <bad>
    ]

    === TRY ADDING LINE OF INPUT TO CODE REGENERATED FROM BLOCK ===

    ; Note: INPUT-HOOK has already run once per line in this block

    assert [not empty? result]  ; should have at least one item

    if '~escape~ = last result [  ; Escape key pressed during READ-LINE
        ;
        ; Note: At one time it had to be Ctrl-D on Windows, as ReadConsole()
        ; could not trap escape.  But input was changed to use more granular
        ; APIs on windows, on a keystroke-by-keystroke basis vs reading a
        ; whole line at a time.
        ;
        return <prompt>
    ]

    if '~null~ = last result [  ; disconnection from input source
        return 0  ; !!! how did this work before?
    ]

    let code: (
        transcode (delimit newline result)
    ) except error -> [
        ;
        ; If loading the string gave back an error, check to see if it
        ; was the kind of error that comes from having partial input
        ; (scan-missing).  If so, CONTINUE and read more data until
        ; it's complete (or until an empty line signals to just report
        ; the error as-is)
        ;
        if error.id = 'scan-missing [
            ;
            ; Error message tells you what's missing, not what's open and
            ; needs to be closed.  Invert the symbol.
            ;
            switch error.arg1 [
                #"}" [#"{"]
                #")" [#"("]
                #"]" [#"["]
            ] also unclosed -> [
                ;
                ; Backslash is used in the second column to help make a
                ; pattern that isn't legal in Rebol code, which is also
                ; uncommon in program output.  This enables detection of
                ; transcripts, potentially to replay them without running
                ; program output or evaluation results.
                ;
                ; *Note this is not running in a continuation at present*,
                ; so the WRITE-STDOUT can only be done via the EMIT.
                ;
                emit [write stdout unspaced [(<*> unclosed) "\" _ _]]

                emit [reduce [  ; reduce will runs in sandbox
                    (<*> spread result)  ; splice previous inert literal lines
                    reify system.console/input-hook  ; hook to run in sandbox
                ]]

                return block!  ; documents expected match of REDUCE product
            ]
        ]

        ; Could be an unclosed double quote (unclosed tag?) which more input
        ; on a new line cannot legally close ATM
        ;
        emit [system.console/print-panic (<*> error)]
        return <prompt>
    ]

    ; If the transcode returned [], then it was like (transcode "") or
    ; transcode "; some comment".  But rather than have the console print out
    ; a note that evaluated to GHOST!, we just cycle the prompt.  This is
    ; more pleasing if you just hit enter to see if the console is responding.
    ;
    if [] = code [
        return <prompt>
    ]

    assert [block? code]

    === HANDLE CODE THAT HAS BEEN SUCCESSFULLY LOADED ===

    if let shortcut: select system.console.shortcuts ?? code.1 [
        ;
        ; Shortcuts like `q => [quit 0]`, `d => [dump]`
        ;
        if (has sys.contexts.user code.1) and (set? inside code code.1) [
            ;
            ; Help confused user who might not know about the shortcut not
            ; panic by giving them a message.  Reduce noise for the casual
            ; shortcut by only doing so when a bound variable exists.
            ;
            emit [system.console/print-warning (<*>
                spaced [
                    uppercase to text! code.1
                        "interpreted by console as:" mold :shortcut
                ]
            )]
            emit [system.console/print-warning (<*>
                spaced ["use" to get-word! code.1 "to get variable."]
            )]
        ]
        take code
        insert code spread shortcut
    ]

    ; Run the "dialect hook", which can transform the completed code block
    ;
    emit #unskin-if-halt  ; Ctrl-C during dialect hook is a problem
    emit [
        comment "not all users may want CONST result, review configurability"
        as group! system.console/dialect-hook '(<*> code)
    ]
    return group!  ; a group RESULT should come back to HOST-CONSOLE
]


=== WHY and UPGRADE (do these belong here?) ===

; We can choose to expose certain functionality only in the console prompt,
; vs. needing to be added to global visibility.  Adding to the lib context
; means these will be seen by scripts, e.g. `do "why"` will work.
;

export why: proc [
    "Explain the last error in more detail."
    'err [<end> word! path! warning!] "Optional error value"
][
    let err: default [system.state.last-error]

    if match [word! path!] err [
        err: get err
    ]

    if warning? err [
        err: lowercase unspaced [err.type "-" err.id]
        let docroot: http://www.rebol.com/r3/docs/errors/
        browse join docroot [err ".html"]
    ] else [
        print "No information is available."
    ]
]


export upgrade: proc [
    "Check for newer versions."
][
    ; Should this be a console-detected command, like Q, or is it meaningful
    ; to define this as a function you could call from code?
    ;
    do @upgrade
]
