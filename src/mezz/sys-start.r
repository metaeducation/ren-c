REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Sys: Startup"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2017 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Context: sys
    Note: {
        The Startup_Core() function in %b-init.c is supposed to be a fairly
        minimal startup, to get the system running.  For instance, it does
        not do any command-line processing...as the host program might not
        even *have* a command line.  It just gets basic things set up like
        the garbage collector and other interpreter services.

        Not much of that work can be delegated to Rebol routines, because
        the evaluator can't run for a lot of that setup time.  But at the
        end of Startup_Core() when the evaluator is ready, it runs this
        routine for any core initialization code which can reasonably be
        delegated to Rebol.

        After this point, it is expected that further initialization be done
        by the host.  That includes the mentioned command-line processing,
        which due to this layering can be done with PARSE.
    }
]

finish-init-core: func [
    "Completes the boot sequence for Ren-C core."
    return: <void>
    boot-mezz [block!]
        {Mezzanine code loaded as part of the boot block in Startup_Core()}
    <local> tmp ;-- need to get JOIN, SYSTEM, and other bits for COMPOSE
][
    ; Remove the reference through which this function we are running is
    ; found, so it's invisible to the user and can't run again (but leave
    ; a hint that it's in the process of running vs. just unsetting it)
    ;
    finish-init-core: 'running

    ; Make the user's global context.  Remove functions whose names are being
    ; retaken for new functionality--to be kept this way during a deprecation
    ; period.  Ther lib definitions are left as-is, however, since the new
    ; definitions are required by SYS and LIB code itself.
    ;
    tmp: make object! 320
    append tmp compose [
        system: (ensure object! system)
    ]

    system/contexts/user: tmp

    ; It was a stated goal at one point that it should be possible to protect
    ; the entire system object and still run the interpreter.  This was
    ; commented out, so the state of that feature is unknown.
    ;
    comment [if :lib/secure [protect-system-object]]

    ; The mezzanine is currently considered part of what Startup_Core() will
    ; initialize for all clients.
    ;
    do bind-lib boot-mezz

    finish-init-core: 'done
]
