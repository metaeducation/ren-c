REBOL [
    Title: "Debugger Extension"
    Name: Debugger

    Type: Module

    Version: 1.0.0
    License: {Apache 2.0}

    Description: {
        The goal of the debugger in Ren-C is to be mostly usermode code, and
        to rely on leveraging exposure of the FRAME! datatype.  One of the
        concepts involved in that is that things like mappings of integers
        to frames is all part of the userspace code's responsibility.

        !!! This is a port to usermode of what was very experimental C code.
        So it is *doubly* experimental.  The primary hope is to show the
        "shape" of the debugger and that the evaluator is exposing the
        information it would need.  Ideally this would be abstracted across
        the hoops one must jump through to implement a single-threaded
        debugger vs. a threaded/interprocess-focused implementation (e.g.
        via a debug://` PORT!)
    }
]


; !!! Putting the console into usermode code and having a customizable "skin"
; (with sandboxing of expressions run on behalf of the skin vs. user
; evaluations) is a pretty complex idea.  Going to having more than one
; skin in effect raises more questions, since the currently in-effect skin
; was assumed to be `system.console.skin`.
;
; So there are many open issues with that.  For right now, what happens is
; that nested console sessions keep track of `system.console.skin` and put
; it back when they are done, so it always represents the current skin.  We
; add some customizations on the base console for the debugger, but these
; will not interact with shortcuts in the non-debugger skin at this time.

debug-console-skin: make console! [
    greeting:
{!! Entering *EXPERIMENTAL* Debug Console that only barely works for a demo.
Expect crashes and mayhem.  But see BACKTRACE, RESUME, and STEP.}

    base-frame: _
    focus-frame: _
    focus-index: _

    print-greeting: meth [return: <none>] [
        ;
        ; We override in order to avoid printing out the redundant Rebol
        ; version information (and to print the greeting only once, which
        ; maybe the default PRINT-GREETING should know to do to).
        ;
        if greeting [
            print newline
            print greeting
            greeting: _
        ]
        base-frame: parent of parent of binding of 'return
        focus-frame: parent of parent of base-frame
        focus-index: 1
    ]

    print-prompt: function [] [
        ;
        ; If a debug frame is in focus then show it in the prompt, e.g.
        ; as `if:|4|>>` to indicate stack frame 4 is being examined, and
        ; it was an `if` statement...so it will be used for binding (you
        ; can examine the condition and branch for instance)
        ;
        if focus-frame [
            if label of focus-frame [
                write-stdout unspaced [label of focus-frame ":"]
            ]
            write-stdout unspaced ["|" focus-index "|"]
        ]

        ; We don't want to use PRINT here because it would put the cursor on
        ; a new line.
        ;
        write-stdout unspaced prompt
        write-stdout space
    ]

    dialect-hook: meth [
        {Receives code block, parse/transform, send back to CONSOLE eval}
        b [block!]
    ][
        if empty? b [
            print-info "Interpreting empty input as STEP"
            return [step]
        ]

        all [
            1 = length of b
            integer? :b.1
        ] then [
            print-info "Interpreting integer input as DEBUG"
            return compose [debug (b.1)]
        ]

        if focus-frame [
            bind b focus-frame
        ]
        return b
    ]
]


backtrace*: function [
    "Backtrace to find a specific FRAME!, or other queried property."

    return: [<opt> block! frame!]
        "Nothing if printing, if specific level a frame! else block"
    start [frame!]
        "Where to consider the trace point as starting from"
    level [blank! integer! action!]
        "Stack level to return frame for (blank to list)"
    /limit "Max number of frames (pending and active), false for no limit"
        [logic! integer!]
    /brief "Do not list depths, just function labels on one line"
][
    get-frame: not blank? :level

    if get-frame [
        any [limit brief] then [
            fail "Can't use /LIMIT or /BRIEF unless getting a list of frames"
        ]

        ; See notes on handling of breakpoint below for why 0 is accepted.
        ;
        if all [integer? level | level < 0] [
            fail ["Invalid backtrace level" level]
        ]
    ]

    max-rows: case [
        blank? limit [
            20  ; Default 20, leaves room to type on 80x25 terminal
        ]
        limit = false [
            99999  ; as many frames as possible
        ]
        integer? limit [
            if limit < 0 [
                fail ["Invalid limit of frames" frames]
            ]
            limit + 1  ; add one for ellipsis
        ]
        fail
    ]

    row: 0  ; row we're on (incl. pending frames and maybe ellipsis)
    number: 0  ; level label number in the loop (no pending frames)
    first-frame: true  ; special check of first frame for "breakpoint 0"

    f: start

    stack: collect [loop [f: parent of f] [
        a: action of f

        if :a = :console [
            ;
            ; For now, just skip any CONSOLE frames in the list.  It might
            ; be better to show them in some circumstances, but really they
            ; just clog up the backtrace.
            ;
            continue
        ]

        if not pending? f [
            if first-frame and (any [
                true  ; !!! Now these are ADAPT, try just zeroing first frame
                :a = :pause
                :a = :breakpoint
            ])[
                ; Omitting breakpoints from the list entirely presents a
                ; skewed picture of what's going on.  But giving them
                ; "index 1" means that inspecting the frame you're actually
                ; interested in (the one where you put the breakpoint) bumps
                ; to 2, which feels unnatural.
                ;
                ; Compromise by not incrementing the stack numbering for
                ; this case, leaving a leading breakpoint frame at index 0.
            ] else [
                number: me + 1
            ]
        ] else [copy []]

        first-frame: false

        row: me + 1

        ; !!! Try and keep the numbering in sync with other code that wants
        ; to be on the same page about the index.
        ;
        comment [
            assert [number = backtrace-index f]
        ]

        if get-frame [
            if integer? :level [
                if number <> :level [
                    continue
                ]
            ] else [
                assert [action? :level]
                if (action of f) <> :level [
                    continue
                ]
            ]
        ] else [
            if row >= max-rows [
                ;
                ; If there's more stack levels to be shown than we were asked
                ; to show, then put an `+ ...` in the list and break.
                ;
                keep '+

                if not brief [
                    ;
                    ; In the non-/ONLY backtrace, the pairing of the ellipsis
                    ; with a plus is used in order to keep the "record size"
                    ; of the list at an even 2.  Asterisk might have been
                    ; used but that is taken for "pending frames".
                    ;
                    ; !!! Review arbitrary symbolic choices.
                    ;
                    keep/line []
                    keep [*]
                ]

                break
            ]
        ]

        if get-frame [
            ;
            ; If we were fetching a single stack level, then our result will
            ; be a FRAME! (which can be queried for further properties via
            ; `near of`, `label of`, `action of`, etc.)
            ;
            return f
        ]

        ; !!! Should /BRIEF omit pending frames?  Should it have a less
        ; "loaded" name for the refinement?
        ;
        if brief [
            keep (label of f else [<anonymous>])
            continue
        ]

        keep ^(near of f)

        ; If building a backtrace, we just keep accumulating results as long
        ; as there are stack levels left and the limit hasn't been hit.

        ; The integer identifying the stack level (used to refer to it
        ; in other debugging commands).  Since we're going in reverse, we
        ; add it after the props so it will show up before, and give it
        ; the newline break marker.
        ;
        if pending? f [
            ;
            ; You cannot (or should not) switch to inspect a pending frame,
            ; as it is partially constructed.  It gets a "*" in the list
            ; instead of a number.
            ;
            ; !!! This may be too restrictive; though it is true you can't
            ; RESUME/FROM or UNWIND a pending frame (due to the index
            ; not knowing how many values it would have consumed if a
            ; call were to complete), inspecting the existing args could
            ; be okay.  Disallowing it offers more flexibility in the
            ; dealings with the arguments, however (for instance: not having
            ; to initialize not-yet-filled args could be one thing).
            ;
            keep [*]
        ] else [
            keep number
        ]

        keep/line []
    ]]

    ; If we ran out of stack levels before finding the single one requested
    ; via /AT, return null
    ;
    if get-frame [return null]

    ; Return accumulated backtrace otherwise, in the reverse order pushed
    ;
    return reverse stack
]


backtrace: function [
    {Prints out a backtrace at the current location}

    return: <none>
][
    ; We could backtrace relative to `binding of 'return`, but this would
    ; mean `>> if true [backtrace]` would see that IF in the trace.
    ;
    stack: backtrace* debug-console-skin/base-frame _
    print mold/only stack
]


; Make adaptation of a breakpoint that lets you know it happened with a
; message.  (By using ADAPT you won't see both BREAKPOINT and BREAKPOINT* on
; the stack during BACKTRACE...it uses only one frame.)
;
breakpoint: adapt :breakpoint* [
    system.console.print-info "BREAKPOINT hit"
]


; The debugger gives its stack analysis in terms of FRAME!s, and in order to
; keep the API and design of FRAME!s manageable they are always associated
; with an ACTION!.  This means there's no user-exposed concept exposing the
; "implementation stack", e.g. no exposure of GROUP!s that are executing as
; if they are their own stack levels.  So when an interrupt is caused by
; single stepping it won't have any point of reference on the stack without
; some function call associated.  So stepping "synthesizes a frame" out of
; thin air by using INTERRUPT, which is like a breakpoint but that the
; backtrace mechanics can choose not to show.
;
interrupt: adapt :breakpoint* [
    ;
    ; !!! INTERRUPT doesn't currently print anything; it's assumed that
    ; changing the prompt would be enough (though a status bar message would
    ; be a good idea in a GUI environment)
]


debug: function [
    {Dialect for interactive debugging, see documentation for details}
    return: <none>
    'value [<opt> integer! frame! action! block!]
        {Stack level to inspect or dialect block, or enter debug mode}
][
    if not integer? :value [
        fail "Since switching to usermode, for now DEBUG only takes INTEGER!"
    ]

    frame: backtrace* debug-console-skin/base-frame value else [
        fail ["FRAME! does not exist for" value]
    ]

    debug-console-skin/focus-frame: frame
    debug-console-skin/focus-index: value
]


locals: function [return: <none>] [
    print [debug-console-skin/focus-frame]
]


debug-console: adapt :console [
    resumable: true

    ; The debug skin is made as a global object, so changes to the skin will
    ; persist between invocations for BREAKPOINTs or STEPs.
    ;
    skin: debug-console-skin
]


; !!! INTERRUPT shouldn't actually be exported; but the way it's run currently
; it is only looked up in the lib context, since it is running outside of
; any native in this module (e.g. it's running from an evaluator hook).  So
; to be found, it has to be exported for now.  The hook will need some amount
; of associated state at some point, which means that STEP (or whatever
; registers the hook) could make INTERRUPT part of that state at that time.
;
export [backtrace debug locals breakpoint interrupt]
