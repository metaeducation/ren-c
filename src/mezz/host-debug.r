REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Host Interactive Debugging"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2016 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        This implements simple debugging features built on top of the basic
        FRAME! and breakpoint abstractions offered by Ren-C.  (There is no
        debugging "UI" in Ren-C itself.)

        !!! Currently included in the mezzanine for build convenience, though
        really it should be packaged with the "host".

        TIP: When debugging this kind of code, don't forget the tools for
        working with FRAME!.  If you want to dump a block of frames, you
        could just say:

            print mold map-each f frame-list [label-of f]
     }
]

for-each-userframe: procedure [
    {Enumerates those Rebol stack frames that aren't part of the REPL/UI}

    'number-var [word!]
        {Variable to be set to INTEGER! or NONE! (if pending frame)}
    'frame-var [word!]
        {Variable to be set to the FRAME! at the given level}
    relative [function! frame!]
        {Where to start the counting relative to}
    body [block!]
][
    ; Get the full backtrace of frames, but then remove FOR-EACH-USERFRAME
    ; from the listing along with any other frames that should be removed
    ; "relative to" (e.g. BACKTRACE wouldn't want BACKTRACE in the list)
    ;
    block: backtrace-of none
    assert [(function-of block/1) = :for-each-userframe]
    if function? :relative [
        relative: backtrace-of :relative
        assert [frame? relative]
    ]
    while [block/1 != relative] [
        take block
        assert [not tail? block]
    ]
    take block ;-- take the relative point, also

    ; Go from the end of the trace to the beginning.  Each time a REPL is
    ; found, delete frames until a DO is found (because the only DO in the
    ; REPL is the one that runs the user code)

    block: tail block
    for-back block [
        if (function-of block/1) = :repl [
            while [(function-of block/1) != :do] [
                if head? block [
                    ;
                    ; Must call directly and filter out an unpaired REPL call
                    ; by passing in :repl as the relative.  Commands like
                    ; BACKTRACE assume they are being run by the user, and
                    ; should always be running from inside a DO in the REPL.
                    ;
                    fail "REPL found without DO in FOR-EACH-USERFRAME"
                ]
                take block
                block: back block
            ]
            take block ;-- now take the DO, too (so just user code left)
        ]
    ]

    block: head block
    if empty? block [leave]

    ; Special exception: frame counts usually start at 1, but a BREAKPOINT or
    ; a PAUSE frame is put at 0 when it's the very first listed that is not
    ; pending.  This is because users likely want to start inspecting the
    ; level that triggered the breakpoint, vs. inspecting the breakpoint
    ; itself.  So it keeps that focus at position "1".

    index: 0 ;-- will be bumped past 0 on first use if not breakpoint/pause

    ; Call the given FOR-EACH-'s body with the frame and the number set

    use reduce [frame-var number-var] compose/deep [
        for-next block [
            ;
            ; Note: Beware GROUP!s for precedence, block is COMPOSE/DEEP'd
            ;
            (to set-word! frame-var) block/1
            either pending? block/1 [
                (to set-word! number-var) none
            ][
                if all [
                    | index = 0
                    | :pause != function-of block/1
                    | :breakpoint != function-of block/1
                ][
                    index: 1 ;-- skip having a 0-frame in the list
                ]
                (to set-word! number-var) index
                index: index + 1
            ]

            (body)
        ]
    ]
]


backtrace: function [
    "Backtrace to find a specific FRAME!, or other queried property."

    level [none! integer! function! <...>]
        "Stack level to return frame for (none to list)"
    /brief
        "Do not list depths, just function labels on one line"
    /quiet
        "Return backtrace data without printing it to the console"
    /from
        "Backtrace from a relative anchor point (NOTE VARIADIC--*BEFORE* arg)"
    relative [frame! function!]
][
    ; Use variadic to have a non-quoted LEVEL argument which can be optional
    ; (so that you can type just BACKTRACE vs. BACKTRACE NONE).  However
    ; since it is variadic
    ;
    level: either tail? level [none] [take level]

    max-frames: 100

    unless any-value? :level [level: none]

    unless from [relative: :backtrace]

    result: copy []
    for-each-userframe number frame :relative [
        either level [
            if any [level = number | level = function-of frame] [
                return frame
            ]
        ][
            either brief [
                insert result label-of frame
            ][
                insert result new-line compose/only [
                    (either none? number ['*] [number]) (where-of frame)
                ] true
            ]
        ]

        if (length result) >= max-frames [break]
    ]

    if level [return none] ;-- didn't find frame for specific level

    if quiet [return result] ;-- if they want the list vs. having it printed

    print mold result
]


debug: proc [
    level [integer!]
][
    unless repl-state [fail "No REPL currently running"]
    unless running? repl-state/repl-frame [fail "Stale REPL frame handle"]

    repl-state/focus-frame: backtrace/from :debug level ;-- note variadic :-/
    repl-state/focus-number: level
]
