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

; Note that TO-LOGIC, TO-INTEGER are currently their own natives (even with
; additional refinements), and thus should not be overwritten here

to-decimal: to-percent: to-money: to-char: to-pair:
to-tuple: to-time: to-date: to-binary: to-file: to-email: to-url: to-tag:
to-string: to-bitset: to-image: to-vector: to-block: to-group:
to-path: to-set-path: to-get-path: to-lit-path: to-map: to-datatype: to-typeset:
to-word: to-set-word: to-get-word: to-lit-word: to-refinement: to-issue:
to-command: to-closure: to-function: to-object: to-module: to-error: to-port:
to-gob: to-event:
    blank

; Auto-build the functions for the above TO-* words.
use [word] [
    for-each type system/catalog/datatypes [
        word: make word! head of remove back tail of unspaced ["to-" type]

        ; The list above determines what will be made here, but we must not
        ; overwrite any NATIVE! implementations.  (e.g. TO-INTEGER is a
        ; native with a refinement for interpreting as unsigned.)

        if all [
            word: in lib word
            blank? get word
        ][
            set word make function! compose/deep [
                [(spaced ["Converts to" form type "value."]) value]
                [to (type) :value]
            ]
        ]
    ]
]
