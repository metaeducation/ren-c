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

verify: function [
    {Verify all the conditions in the passed-in block are conditionally true}

    return: <elide>
    conditions [block!]
        {Conditions to check}
    <local> result
][
    while [pos: evaluate/result conditions 'result] [
        if quoted? pos [  ; invisible that vaporized
            conditions: pos
            continue
        ]

        any [void? :result, not :result] then [
            ;
            ; including commas in the failure report looks messy, skip them
            ;
            while [', = first conditions] [conditions: my next]

            fail 'conditions make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [
                    ((copy/part conditions pos)) ** (case [
                        unset? 'result ['null]
                        void? result ['void]
                        blank? result ['blank]
                        result = false ['false]
                    ])
                ]
            ]
        ]

        conditions: pos   ; move expression position and continue
    ]
]


; Set up ASSERT as having a user-mode implementation matching VERIFY.
; This helps show people a pattern for implementing their own assert.
;
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
native-assert: copy :assert
hijack 'assert :verify


delta-time: function [
    {Returns the time it takes to evaluate the block}
    block [block!]
][
    ; Pre-specialize the timer function, and get the calculations lined up in
    ; a REDUCE.  This minimizes the amount of overhead that winds up in the
    ; result (e.g. not even setting variables is counted in the timing)
    ;
    timer: :lib/now/precise/time  ; Note: NOW comes from an Extension
    do reduce [:negate :subtract :timer :elide :do block :timer]
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
    start: make system/standard/stats []
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
                gunzip gzip tmp
            ]
            calc: [(length of tmp) * 10 / secs / 1900]
        ][
            count-up n 40 [
                change/dup tmp to-char n 500'000
            ]
            calc: [(length of tmp) * 40 / secs / 1024 / 1024]
        ][
            if not no-io [
                write file: %tmp-junk.txt "" ; force security request before timer
                tmp: make text! 32000 * 5
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

net-trace: function [
    "Switch between using a no-op or a print operation for net-tracing"

    return: <void>
    val [logic!]
][
    either val [
        hijack 'net-log func [txt /C /S][
            print [
                (if c ["C:"]) (if s ["S:"])
                    either block? txt [spaced txt] [txt]
            ]
            txt
        ]
        print "Net-trace is now on"
    ][
        hijack 'net-log func [txt /C /S][txt]
    ]
]
