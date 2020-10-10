REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Mezzanine: To-Type Helpers"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
]

; !!! Carl wrote "Are we sure we really want all these?" as a comment here.
; Discussion of eliminating the TO-XXX functions in favor of TO XXX! resolved
; to say that many people prefer them...and that they also may serve a point
; by showing a list of legal conversion types in the help.  They also could
; have refinements giving slightly different abilities than the default
; unrefined TO XXX! behavior would give.

; These must be listed explicitly in order for the words to be collected
; as legal "globals" for the mezzanine context (otherwise SET would fail)

; Note that TO-LOGIC and TO-TEXT are currently their own natives (even with
; additional refinements), and thus should not be overwritten here.

to-integer: to-decimal: to-percent: to-money: to-pair:
to-tuple: to-time: to-date: to-binary: to-file: to-email: to-url: to-tag:
to-bitset: to-image: to-vector: to-block: to-group:
to-path: to-set-path: to-get-path: to-map: to-datatype: to-typeset:
to-word: to-set-word: to-get-word: to-issue:
to-function: to-object: to-module: to-error: to-port:
to-gob: to-event:
    void

; Auto-build the functions for the above TO-* words.
use [word] [
    for-each type system/catalog/datatypes [
        word: make word! head of remove back tail of unspaced ["to-" type]

        ; The list above determines what will be made here, but we must not
        ; overwrite any NATIVE! implementations.  (e.g. TO-INTEGER is a
        ; native with a refinement for interpreting as unsigned.)

        if (word: in lib word) and [undefined? word] [
            set word redescribe compose [
                (spaced ["Converts to" form type "value."])
            ](
                specialize 'to [type: get type]
            )
        ]
    ]
]


; !!! Refinements are actually PATH! now, but TO PATH! of a WORD! assumes you
; want a 2-element path with a blank at the head that looks like a refinement
;
to-refinement: :to-path
