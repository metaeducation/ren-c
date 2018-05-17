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

finish-init-core: proc [
    "Completes the boot sequence for Ren-C core."
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

        adjoin: (ensure action! get 'join)
        join: (func [dummy] [
            fail/where [
                {JOIN is reserved in Ren-C for future use}
                {(It will act like R3's REPEND, which has a slight difference}
                {from APPEND of a REDUCE'd value: it only reduces blocks).}
                {Use ADJOIN for the future JOIN, JOIN-OF for non-mutating.}
                {If in <r3-legacy> mode, old JOIN meaning is available.}
            ] 'dummy
        ])

        unset?: (func [dummy:] [
            fail/where [
                {UNSET? is reserved in Ren-C for future use}
                {(Will mean NULL? GET, like R3-Alpha VALUE? for WORDs/PATHs)}
                {Use NULL? for a similar test, but there is no UNSET! type}
                {If in <r3-legacy> mode, old UNSET? meaning is available}
            ] 'dummy
        ])

        value?: (func [dummy:] [
            fail/where [
                {VALUE? is reserved in Ren-C for future use}
                {(It will be a shorthand for ANY-VALUE? a.k.a. NOT VOID?)}
                {SET? is like R3-Alpha VALUE?, but only for WORDs/PATHs}
                {If in <r3-legacy> mode, old VALUE? meaning is available.}
            ] 'dummy
        ])

        ; !!! See UNLESS for the plan of it being retaken.  For the moment
        ; this compatibility shim is active in %mezz-legacy.r, but not
        ; exposed to the user context.

        unless: (ensure action! get 'if-not)

        (comment [
        unless: (function [ ;-- enfixed below
            {Returns left hand side, unless the right hand side is something}

            return: [any-value!]
            left [<end> any-value!]
            right [<opt> any-value! <...>]
            :look [any-value! <...>]
            /try {Consider right being BLANK! a value to override the left}
        ][
            any [
                unset? 'left
                elide (right: take* right)
                block? first look
            ] then [
                fail/where [
                    "UNLESS has been repurposed in Ren-C as an enfix operator"
                    "which defaults to the left hand side, unless the right"
                    "side has a value which overrides it.  You may use IF-NOT"
                    "as a replacement, or even define UNLESS: :LIB/IF-NOT,"
                    "though actions like OR, DEFAULT, etc. are usually better"
                    "replacements for the intents that UNLESS was used for."
                    "!!! NOTE: `if not` as two words isn't the same in Ren-C,"
                    "as `if not x = y` is read as `if (not x) = y` since `=`"
                    "completes its left hand side.  Be careful rewriting."
                ] 'look
            ]

            either-test (try ?? :value? !! :something?) :right [:left]
        ])

        unless*: (redescribe [ ;-- enfixed below
            {Same as UNLESS/TRY (right side being BLANK! overrides the left)}
        ](
            specialize 'unless [try: true]
        ))])

        switch: (adapt 'switch [
            for-each c cases [
                lib/all [ ;-- SWITCH/ALL would override
                    did match [word! path!] c
                    'null <> c
                    not datatype? get c
                ] then [
                    fail/where [
                        {Temporarily disabled word/path SWITCH clause:} :c LF

                        {You likely meant to use a LIT-WORD! / LIT-PATH!} LF

                        {SWITCH in Ren-C evaluates its match clauses, and}
                        {will even allow 0-arity ACTION!s (larger arities are}
                        {put in a GROUP! to facilitate skipping after a match}
                        {is found.  But to help catch old uses, only datatype}
                        {lookups are enabled.  SWITCH: :LIB/SWITCH overrides.}
                    ] 'cases
                ]
            ]
        ])

        ; Ren-C has standardized on one type of "invokable", which is ACTION!.
        ; For the rationale, see: https://forum.rebol.info/t/596
        ;
        ; These FUNCTION! synonyms are kept working since they aren't needed
        ; for other purposes, but new code should not use them.
        ;
        any-function!: (ensure datatype! action!)
        function!: (ensure datatype! action!)
        any-function?: (ensure action! :action?)
        function?: (ensure action! :action?)

        ; Ren-C is also unifying the various ANY-WORD! types with ANY-STRING!
        ; types.  String is a great name for the abstract category, so it's
        ; not a good name for a concrete type member.  TEXT! is the new name.
        ; See: https://forum.rebol.info/t/text-vs-string/612
        ;
        string!: (ensure datatype! text!)
        string?: (ensure action! :text?)
        to-string: (specialize 'to [type: text!]) ;-- mezz hasn't run yet

        ; Ren-C *prefers* the use of GROUP! to PAREN!, both legal for now.
        ; https://trello.com/c/ANlT44nH
        ;
        paren!: (ensure datatype! group!)
        paren?: (ensure action! :group?)
        to-paren: (specialize 'to [type: group!]) ;-- mezz hasn't run yet

        ; Typesets containing ANY- helps signal they are not concrete types
        ; https://trello.com/c/d0Nw87kp
        ;
        number!: (ensure typeset! any-number!)
        number?: (ensure action! :any-number?)
        scalar!: (ensure typeset! any-scalar!)
        scalar?: (ensure action! :any-scalar?)
        series!: (ensure typeset! any-series!)
        series?: (ensure action! :any-series?)

        ; ANY-VALUE!, vs e.g. "ANY-DATATYPE!" https://trello.com/c/1jTJXB0d
        ;
        any-type!: (ensure typeset! any-value!)

        ; In user code and in the C code, good to avoid BLOCK! vs. ANY-BLOCK!
        ; https://trello.com/c/lCSdxtux
        ;
        any-block!: (ensure typeset! any-array!)
        any-block?: (ensure action! :any-array?)

        ; Similar to the BLOCK!/ANY-BLOCK! problem in understanding the inside
        ; and outside of the system, ANY-CONTEXT! is better for the superclass
        ; of OBJECT!, ERROR!, PORT!, FRAME!, MODULE!...
        ;
        any-object!: (ensure typeset! any-context!)
        any-object?: (ensure action! :any-context?)
    ]

    comment [
        tmp/unless: enfix :tmp/unless
        tmp/unless*: enfix :tmp/unless*
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
