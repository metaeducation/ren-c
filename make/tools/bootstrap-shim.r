REBOL [
    Title: "Shim to bring old executables up to date to use for bootstrapping"
    Rights: {
        Rebol 3 Language Interpreter and Run-time Environment
        "Ren-C" branch @ https://github.com/metaeducation/ren-c

        Copyright 2012-2018 Rebol Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Purpose: {
        Ren-C "officially" supports two executables for doing a bootstrap
        build.  One is a frozen "stable" version (`8994d23`) which was
        committed circa Dec-2018:

        https://github.com/metaeducation/ren-c/commit/dcc4cd03796ba2a422310b535cf01d2d11e545af

        The only other executable that is guaranteed to work is the *current*
        build.  This is ensured by doing a two-step build in the continuous
        integration, where 8994d23 is used to make the first one, and then
        the build is started over using that product.

        This shim is for 8994d23, in order to bring it up to compatibility
        for any new features used in the bootstrap code that were introduced
        since it was created.  This is facilitated by Ren-C's compositional
        operations, like ADAPT, CHAIN, SPECIALIZE, and ENCLOSE.
    }
]

; The snapshotted Ren-C existed right before <blank> was legal to mark an
; argument as meaning a function returns null if that argument is blank.
; See if this causes an error, and if so assume it's the old Ren-C, not a
; new one...?
;
; What this really means is that we are only catering the shim code to the
; snapshot.  (It would be possible to rig up shim code for pretty much any
; specific other version if push came to shove, but it would be work for no
; obvious reward.)
;
trap [
    func [i [<blank> integer!]] [...]
] else [
    QUIT
]

print "== SHIMMING OLDER R3 TO MODERN LANGUAGE DEFINITIONS =="

; COLLECT was changed back to default to returning an empty block on no
; collect, but it is built on a null collect lower-level primitive COLLECT*
;
collect*: :collect
collect: :collect-block

modernize-action: function [
    "Account for the <blank> annotation as a usermode feature"
    return: [block!]
    spec [block!]
    body [block!]
][
    blankers: copy []
    spec: collect [
        iterate spec [
            ;
            ; Find ANY-WORD!s (args/locals)
            ;
            if keep w: match any-word! spec/1 [
                ;
                ; Feed through any TEXT!s following the ANY-WORD!
                ;
                while [if (tail? spec: my next) [break] | text? spec/1] [
                    keep/only spec/1
                ]

                ; Substitute BLANK! for any <blank> found, and save some code
                ; to inject for that parameter to return null if it's blank
                ;
                if find (try match block! spec/1) <blank> [
                    keep/only replace copy spec/1 <blank> 'blank!
                    append blankers compose [
                        if blank? (as get-word! w) [return null]
                    ]
                    continue
                ]
            ]
            keep/only spec/1
        ]
    ]
    body: compose [
        (blankers)
        (as group! body)
    ]
    return reduce [spec body]
]

func: adapt 'func [set [spec body] modernize-action spec body]
function: adapt 'function [set [spec body] modernize-action spec body]

meth: enfix adapt 'meth [set [spec body] modernize-action spec body]
method: enfix adapt 'method [set [spec body] modernize-action spec body]

trim: adapt 'trim [ ;; there's a bug in TRIM/AUTO in 8994d23
    if auto [
        while [(not tail? series) and [series/1 = LF]] [
            take series
        ]
    ]
]

mutable: func [x [any-value!]] [
    ;
    ; Some cases which did not notice immutability in the bootstrap build
    ; now do, e.g. MAKE OBJECT! on a block that you LOAD.  This is a no-op
    ; in the older build, but should run MUTABLE in the new build when it
    ; emerges as being needed.
    ;
    :x
]

lit: :quote ;; Renamed due to the QUOTED! datatype
quote: null
uneval: func [x [<opt> any-value!]] [
    switch type of x [
        null [lit ()]
        word! [to lit-word! x]
        path! [to lit-path! x]

        fail "UNEVAL can only work on WORD!, PATH!, NULL in old Rebols"
    ]
]

join: :join-of
join-of: func [] [
    fail 'return [
        "JOIN has returned to Rebol2 semantics, JOIN-OF is no longer needed"
        https://forum.rebol.info/t/its-time-to-join-together/1030
    ]
]

; https://forum.rebol.info/t/has-hasnt-worked-rethink-construct/1058
has: null

; Simple "divider-style" thing for remarks.  At a certain verbosity level,
; it could dump those remarks out...perhaps based on how many == there are.
; (This is a good reason for retaking ==, as that looks like a divider.)
;
===: func [:remarks [any-value! <...>]] [
    until [
        equal? '=== take remarks
    ]
]

const?: func [x] [return false]
