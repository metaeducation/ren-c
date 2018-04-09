; Better-than-nothing REDO tests

; REDO via a direct FRAME! value
(
    foo: func [n] [
        frame: context of 'n
        if n = 0 [
            return <success>
        ]
        n: n - 1
        redo frame
    ]

    <success> = foo 100
)

; REDO via extraction of FRAME! from an ANY-WORD!
; (has binding to a FRAME! to lookup variable value)
(
    foo: func [n] [
        if n = 0 [
           return <success>
        ]
        n: n - 1
        redo 'n
    ]

    <success> = foo 100
)

; REDO locals clearing test
; (locals should be cleared on each redo)
(
    foo: func [n <local> unset-me] [
        if set? 'unset-me [
            return "local not cleared"
        ]
        if n = 0 [
            return <success>
        ]
        n: n - 1
        unset-me: #some-junk
        redo 'return
    ]

    <success> = foo 100
)

; REDO type checking test
; (args and refinements must pass function's type checking)
(
    foo: func [n i [integer!]] [
        if n = 0 [
            return <success> ;-- impossible for this case
        ]
        n: n - 1
        i: #some-junk ;-- type check should fail on redo
        redo 'return
    ]

    error? trap [foo 100]
)

; REDO phase test
; (shared frame compositions should redo the appropriate "phase")
(
    inner: func [n] [
        if n = 0 [
            return <success>
        ]
        n: 0
        redo 'n ;-- should redo INNER, not outer
    ]

    outer: adapt 'inner [
        if n = 0 [
            return "outer phase run by redo"
        ]
        ;-- fall through to inner, using same frame
    ]

    <success> = outer 1
)
(
    inner: func [n /captured-frame f] [
        if n = 0 [
           return "inner phase run by redo"
        ]
        n: 0
        redo f ;-- should redo OUTER, not INNER
    ]

    outer: adapt 'inner [
        if n = 0 [
            return <success>
        ]

        f: context of 'n
        captured-frame: true

        ;-- fall through to inner
        ;-- it is running in the same frame's memory, but...
        ;-- F is a FRAME! value that stowed outer's "phase"
    ]

    <success> = outer 1
)

; "Sibling" tail-call with compatible function
;
; (CHAINs are compatible with functions at head of CHAIN
;  ADAPTs are compatible with functions they adapt
;  SPECIALIZEs are compatible with functions they specialize...etc.)
;
; If LOG is set to DUMP the following will output:
;
;     --- C ---
;     n: => 11
;     delta: => 0
;     --- S ---
;     n: => 11
;     delta: => 10
;     --- BASE ---
;     n: => 11
;     delta: => 10
;     --- C ---
;     n: => 1
;     delta: => 10
;     --- S ---
;     n: => 1
;     delta: => 10
;     --- BASE ---
;     n: => 10
;     delta: => 10
;
; C is called and captures its frame into F.  Then it uses REDO/OTHER to
; reuse the frame to call S.  S gets the variables and args that its knows
; about as C left them--such as N and a captured frame F--but values it takes
; for granted are reset, which includes specialized delta of 10.
;
; (The need to reset specializations for consistency is similar to how locals
; must be reset--they're not part of the interface of the function, so to
; reach beneath them does something illegal in terms of parameterization.)
;
; S doesn't have any effect besides resetting delta, so it falls through as
; an adaptation to the base function.  BASE subtracts DELTA from N to get 1,
; which isn't an exit condition.  The F frame which was set in C and was
; preserved as an argument to S is then used by BASE to REDO and get back
; up to the start of C again.
;
; Once again C captures its frame and does a REDO to start up S, which now
; notices that N is 1 so it bumps it up to 10.  (It cannot set DELTA to 1,
; because as a specialized argument DELTA is not visible to it.)  This time
; when it falls through to BASE, the subtraction of DELTA from N yields
; zero so that BASE returns completion.
;
; Since the function we originally called and built a frame for was a CHAIN,
; the REDO is effectively REDO-finishing the frame for the adaptation of
; BASE that sits at the head of the frame.  That delegation has now finished
; bouncing around on that single frame and come to a completion, which means
; the chained functions will get that result.  The string is translated to
; a tag and signals success.
(
    log: (
        func ['x] [] ;-- no-op
        elide (:dump) ;-- un-elide to get output
    )

    base: func [n delta /captured-frame f [frame!]] [
        log [{BASE} n delta]

        n: n - delta
        if n < 0 [return "base less than zero"]
        if n = 0 [return "base done"]
        if captured-frame [redo f]
        return "base got no frame"
    ]

    c: chain [
        adapt 'base [
           log [{C} n delta]

           f: context of 'n
           captured-frame: true
           redo/other 'n :s

           ;-- fall through to base
        ]
            |
        func [x] [
            if x = "base done" [
                <success>
            ] else [
                spaced ["base exited with" x]
            ]
        ]
    ]

    s: specialize adapt 'base [
        log [{S} n delta]

        if n = 1 [n: 10]
    ][
        delta: 10
    ]

    <success> = c 11 0
)
