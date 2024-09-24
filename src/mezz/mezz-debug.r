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

verify: func [
    {Verify all the conditions in the passed-in block are conditionally true}

    return: [nihil?]
    conditions "Conditions to check"
        [block!]
    /handler "Optional code to run if the assertion fails, receives condition"
        [<unrun> block! frame!]
    <local> pos result
][
    while [[pos /result]: evaluate/step conditions] [
        all [
            not void? :result
            not :result

            if handler [  ; may or may-not take two arguments
                let reaction: ^ if block? handler [
                    eval handler
                ] else [
                    apply/relax handler [  ; arity 0 or 1 is okay
                        copy/part conditions pos
                        result
                    ]
                ]

                ; If the handler doesn't itself fail--and does not return
                ; ~ignore~, then we go ahead and fail.  This lets you
                ; write simple handlers that just print you a message...like
                ; some context for the assert.
                ;
                reaction != '~ignore~
            ]
        ] then [
            fail/blame make error! [
                type: 'Script
                id: 'assertion-failure
                arg1: compose [
                    (spread copy/part conditions pos) ** (reify result)
                ]
            ] $conditions
        ]

        conditions: pos   ; move expression position and continue
    ]
    return nihil
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
native-assert: runs copy unrun assert/
hijack assert/ verify/


delta-time: func [
    {Returns the time it takes to evaluate the block}
    block [block!]
][
    let timer: unrun get $lib/now/precise  ; Note: NOW comes from an Extension
    results: reduce reduce [  ; resolve word lookups first, run fetched items
        timer
        (unrun elide/) (unrun eval/) block
        timer
    ]
    return difference results.2 results.1
]

delta-profile: func [
    {Delta-profile of running a specific block.}
    block [block!]
    <local> start end
][
    start: values of stats/profile
    eval block
    end: values of stats/profile
    for-each 'num start [
        change end end/1 - num
        end: next end
    ]
    start: make system.standard.stats []
    set start head of end
    start
]

speed?: func [
    "Returns approximate speed benchmarks [eval cpu memory file-io]."
    /no-io "Skip the I/O test"
    /times "Show time for each test"
][
    let result: copy []
    let calc
    for-each 'block [
        [
            repeat 100'000 [
                ; measure more than just loop func
                ; typical load: 1 set, 2 data, 1 op, 4 trivial funcs
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
                x: 1 * index of back next "x"
            ]
            calc: [100'000 / secs / 100] ; arbitrary calc
        ][
            let tmp: make binary! 500'000
            insert/dup tmp "abcdefghij" 50000
            repeat 10 [
                random tmp
                gunzip gzip tmp
            ]
            calc: [(length of tmp) * 10 / secs / 1900]
        ][
            count-up 'n 40 [
                change/dup tmp to-char n 500'000
            ]
            calc: [(length of tmp) * 40 / secs / 1024 / 1024]
        ][
            if not no-io [
                let file: %tmp-junk.txt
                write file "" ; force security request before timer
                let tmp: make text! 32000 * 5
                insert/dup tmp "test^/" 32000
                repeat 100 [
                    write file tmp
                    read file
                ]
                delete file
                calc: [(length of tmp) * 100 * 2 / secs / 1024 / 1024]
            ]
        ]
    ][
        let secs: now/precise
        calc: 0
        recycle
        eval block
        secs: to decimal! difference now/precise secs
        append result to integer! eval calc
        if times [append result secs]
    ]
    return result
]

net-log: lambda [txt /C /S][txt]

net-trace: func [
    "Switch between using a no-op or a print operation for net-tracing"

    return: [~]
    val [logic?]
][
    either val [
        hijack net-log/ func [txt /C /S][
            print [
                (if c ["C:"]) (if s ["S:"])
                    either block? txt [spaced txt] [txt]
            ]
            txt
        ]
        print "Net-trace is now on"
    ][
        hijack net-log/ func [txt /C /S][txt]
    ]
]
