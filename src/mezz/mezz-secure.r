REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL Mezzanine: Security"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

secure: function [
    "Set security policies (use SECURE help for more information)."
    return: [<opt> any-value!]
    'policy [<opt> word! lit-word! block!]
        "Set single or multiple policies (or HELP)"

    <static>

    ; Permanent values and sub-functions of SECURE:

    acts ([allow ask throw quit])

    assert-policy (
        func [tst kind arg] [
            if not tst [cause-error 'access 'security-error reduce [kind arg]]
        ]
    )

    make-policy (function [
        ; Build the policy tuple used by lower level code.
        target ; "For special cases: eval, memory"
        pol ; word number or block
    ][
        ; Special cases: [eval 100000]
        if find [eval memory] target [
            assert-policy any-number? pol target pol
            limit-usage target pol ; pol is a number here
            return 3.3.3 ; always quit
        ]
        ; The set all case: [file allow]
        if word? pol [
            n: find acts pol
            assert-policy n target pol
            return (index of n) - 1 * 1.1.1
        ]
        ; Detailed case: [file [allow read throw write]]
        flags: 0.0.0
        assert-policy block? pol target pol
        for-each [act perm] pol [
            n: find acts act
            assert-policy n target act
            m: select [read 1.0.0 write 0.1.0 execute 0.0.1] perm
            assert-policy m target perm
            flags: (index of n) - 1 * m or+ flags
        ]
        flags
    ])

    set-policy (function [
        ; Set the policy as tuple or block:
        target
        pol
        pol-obj
    ][
        case [
            file? target [
                val: file-to-local/full target
                ; This string must have OS-local encoding, because
                ; the check is done at a lower level of I/O.
                if system/version/4 !== 3 [val: to binary! val]
                target: 'file
            ]
            url? target [val: target  target: 'net]
        ]
        old: select pol-obj target
        assert-policy old target pol
        either val [
            ; Convert tuple to block if needed:
            if tuple? old [old: reduce [target old]]
            remove/part find old val 2  ; can be in list only once
            insert old reduce [val pol]
        ][
            old: pol
        ]
        set in pol-obj target old
    ])

    word-policy (function [pol][
        ; Convert lower-level policy tuples to words:
        if all [pol/1 is pol/2 | pol/2 is pol/3][
            return pick acts 1 + pol/1
        ]
        blk: make block! 4
        n: 1
        for-each act [read write execute] [
            join blk [pick acts 1 + pol/:n act]
            n: me + 1
        ]
        blk
    ])

] append bind [

    "Two funcs bound to private system/state/policies with protect/hide after."
    set-policies: func [p] [set 'policies p]
    get-policies: func [] [copy/deep policies]

] system/state [

    policy: default ['help]

    if policy is 'None [policy: 'allow] ; note: NONE is a word here (like R2)

    pol-obj: get-policies ; a deep copy

    if policy is 'Help [
        print "You can set policies for:"
        for-each [target pol] pol-obj [print ["  " target]]
        print "These can be set to:"
        for-each [t d] [
            allow "no security"
            ask   "ask user for permission"
            throw "throw as an error"
            quit  "exit the program immediately"
            file  "a file path"
            url   "a file path"
            other "other value, such as integer"
        ] [print [space space t "-" d]]
        print "Settings for read, write, and execute are also available."
        print "Type: help/doc secure for detailed documentation and examples."
        return null
    ]

    if policy is 'Query [
        out: make block! 2 * length of pol-obj
        for-each [target pol] pol-obj [
            case [
                ; file 0.0.0 (policies)
                tuple? pol [join out [target word-policy pol]]
                ; file [allow read quit write]
                block? pol [
                    for-each [item pol] pol [
                        if binary? item [item: to-text item] ; utf-8 decode
                        if text? item [item: local-to-file item]
                        join out [item word-policy pol]
                    ]
                ]
            ]
        ]
        new-line/skip out on 2
        return out
    ]

    ; Check if SECURE is secured:
    if pol-obj/secure !== 0.0.0 [
        if pol-obj/secure == 'throw [cause-error 'access 'security :policy]
        quit/now/return 101 ; an arbitrary code
    ]

    ; Bulk-set all policies:
    if word? policy [
        n: make-policy 'all policy
        for-each word words of pol-obj [set word n]
        set-policies pol-obj
        return null
    ]

    ; Set each policy target separately:
    for-each [target pol] policy [
        ensure [word! file! url!] target
        ensure [block! word! integer!] pol
        set-policy target make-policy target pol pol-obj
    ]

    ; ADD: check for policy level reductions!
    set-policies pol-obj
    return null
]


if system/options/secure isn't 'Allow [
    ; Remove all other access to the policies:
    protect/hide in system/state 'policies
]

protect-system-object: func [
    "Protect the system object and selected sub-objects."
][
    protect 'system
    protect system

    "full protection:"
    protect/words/deep [
        system/catalog
        ;system/standard
        system/dialects
        ;system/intrinsic
    ]

    "mild protection:"
    protect/words [
        system/license
        system/contexts
    ]

    unprotect/words [
        system/script
        ;system/schemes
        ;system/ports   ; should not be modified, fix this
        system/options ; some are modified by scripts
        system/view    ; should not be modified!
    ]
]
