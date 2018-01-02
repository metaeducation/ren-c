REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: Debug"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

; !!! Set up ASSERT as having a user-mode implementation matching VERIFY.
; Rather than using the implementation of verify directly, this helps to
; show people a pattern for implementing their own assert.
;
; This should really only be done in debug modes.  R3-Alpha did not have the
; idea of running in release mode at all, so there is some code that depends
; on the side-effects of an assert...but it's nice to have the distinction
; available so that people can add tests.
;
; This ASSERT has basic features of being able to treat issues as instructions
; for enablement.  By default, if an issue label is used, then an assert
; will not run unless e.g. `assert/meta [#heavy-checks on]` is performed.
; The same is available for TAG!, except the default is that a tagged assert
; will be run unless disabled.
;
; In order to facilitate reuse and chaining, the assert can be told not to
; actually report its error, but to give the failing assert back as a block!
;
; As a special enhancement, there is an understanding of the specific
; construct of `assert [x = blah blah blah]` which will report what x
; actually was.  This is only an example of what is possible with a true
; assert or logging dialect.

live-asserts-map: make map! []

assert-debug: function [
    return: [<opt> any-value!]
    conditions [logic! block!]
        {Conditions to check (or meta instructions if /META)}
    /quiet
        {Return void on success or a BLOCK! of the failure condition if failed}
    /meta
        {Block is enablement and disablement, e.g. [#heavy-checks on]}
][
    if meta [
        rules: [any [
            any bar!
            set option: [issue! | tag!]
            set value: [word! | logic!]
            (
                if word? value [value: get value]

                unless logic? value [
                    fail ["switch must be LOGIC! true or false for" option]
                ]

                either value [
                    either tag? option [
                        remove/map live-asserts-map option ; enable implicit
                    ][
                        live-asserts-map/(option): true ; must be explicit
                    ]
                ][
                    either issue? option [
                        remove/map live-asserts-map option ; disable implicit
                    ][
                        live-asserts-map/(option): false ; must be explicit
                    ]
                ]
            )
        ]]

        unless parse conditions rules [
            fail [
                "/META options must be pairs, e.g. [#heavy-checks on]"
                conditions
            ]
        ]
        return ()
    ]

    failure-helper: procedure [
        expr [logic! block!]
            {The failing expression (or just FALSE if a LOGIC!)}
        bad-result [<opt> any-value!]
            {What the FALSE? or void that triggered failure was}
        <with> return
    ][
        if quiet [
            ;
            ; Due to <with> return this imports the return from ASSERT-DEBUG
            ; overall.  This result is not going to be very useful for a
            ; plain FALSE return, and a proper logging mechanism would need
            ; some information about the source location of failure.
            ;
            return expr ;-- due to `<with> return`
        ]

        fail [
            "Assertion condition returned"
             (case [
                (not set? 'bad-result) "void"
                    |
                (blank? bad-result) "blank"
                    |
                (bad-result = false) "false"
            ])
            ":"
            expr
        ]
    ]

    either logic? conditions [
        if not conditions [
            failure-helper false false
        ]
    ][
        ; Otherwise it's a block!
        active: true
        until [tail? conditions] [
            if option: maybe [issue! tag!] :conditions/1 [
                unless active: select live-asserts-map option [
                    ;
                    ; if not found in the map, go with default behavior.
                    ; (disabled for #named tests, enabled for <tagged>)
                    ;
                    active: tag? option
                ]
            ]

            result: do/next conditions quote pos:
            if active and (any [not set? 'result | not :result]) [
                failure-helper (copy/part conditions pos) :result
            ]

            conditions: pos ;-- move expression position and continue

            ; including BAR!s in the failure report looks messy
            while [bar? :conditions/1] [conditions: next conditions]
        ]
    ]

    return if quiet [true] ;-- void is return default
]


; !!! If a debug mode were offered, you'd want to be able to put back ASSERT
; in such a way as to cost basically nothing.
;
; !!! Note there is a layering problem, in that if people make a habit of
; hijacking ASSERT, and it's used in lower layer implementations, it could
; recurse.  e.g. if file I/O writing used ASSERT, and you added a logging
; feature via HIJACK that wrote to a file.  Implications of being able to
; override a system-wide assert in this way should be examined, and perhaps
; copies of the function made at layer boundaries.
;
native-assert: hijack 'assert :assert-debug


delta-time: function [
    {Delta-time - returns the time it takes to evaluate the block.}
    block [block!]
][
    start: stats/timer
    do block
    stats/timer - start
]

delta-profile: func [
    {Delta-profile of running a specific block.}
    block [block!]
    <local> start end
][
    start: values of stats/profile
    do block
    end: values of stats/profile
    for-each num start [
        change end end/1 - num
        end: next end
    ]
    start: construct system/standard/stats []
    set start head of end
    start
]

speed?: function [
    "Returns approximate speed benchmarks [eval cpu memory file-io]."
    /no-io "Skip the I/O test"
    /times "Show time for each test"
][
    result: copy []
    for-each block [
        [
            loop 100'000 [
                ; measure more than just loop func
                ; typical load: 1 set, 2 data, 1 op, 4 trivial funcs
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
            ]
            calc: [100'000 / secs / 100] ; arbitrary calc
        ][
            tmp: make binary! 500'000
            insert/dup tmp "abcdefghij" 50000
            loop 10 [
                random tmp
                decompress compress tmp
            ]
            calc: [(length of tmp) * 10 / secs / 1900]
        ][
            repeat n 40 [
                change/dup tmp to-char n 500'000
            ]
            calc: [(length of tmp) * 40 / secs / 1024 / 1024]
        ][
            unless no-io [
                write file: %tmp-junk.txt "" ; force security request before timer
                tmp: make string! 32000 * 5
                insert/dup tmp "test^/" 32000
                loop 100 [
                    write file tmp
                    read file
                ]
                delete file
                calc: [(length of tmp) * 100 * 2 / secs / 1024 / 1024]
            ]
        ]
    ][
        secs: now/precise
        calc: 0
        recycle
        do block
        secs: to decimal! difference now/precise secs
        append result to integer! do calc
        if times [append result secs]
    ]
    result
]

net-log: func [txt /C /S][txt]

net-trace: procedure [
    "Switch between using a no-op or a print operation for net-tracing"
    val [logic!]
][
    either val [
        hijack 'net-log func [txt /C /S][
            if c [print/only "C: "]
            if s [print/only "S: "]
            print/eval txt
            txt
        ]
        print "Net-trace is now on"
    ][
        hijack 'net-log func [txt /C /S][txt]
    ]
]
