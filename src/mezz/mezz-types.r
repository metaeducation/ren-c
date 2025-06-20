Rebol [
    system: "Rebol [R3] Language Interpreter and Run-time Environment"
    title: "REBOL 3 Mezzanine: To-Type Helpers"
    rights: --[
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    ]--
    license: --[
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    ]--
    description: --[
        Create TO-XXX functions for all the XXX! datatypes, e.g. TO-FILE
        as a shorthand for TO FILE!

        !!! Carl wrote "Are we sure we really want all these?" as a comment.
        But discussion of eliminating the TO-XXX functions in favor of TO XXX!
        resolved to say that many people prefer them...and that they also may
        serve a point by showing a list of legal conversion types in the help.
        They also could have refinements giving slightly different abilities
        than the default unrefined TO XXX! behavior would give.
    ]--
]

; 1. Note that TO-TEXT is currently its own native (with a :RELAX refinement)
;    and thus should not be overwritten here.  This may not be permanent.
;
for-each [typename type] system.contexts.datatypes [
    if antiform?:type type [continue]  ; dont make TO-SPLICE etc.

    let stem: to text! typename
    take:last stem

    let word: join word! ["to-" stem]

    if has lib word [continue]  ; e.g. TO-TEXT is currently its own native [1]

    set extend lib word redescribe compose [
        (spaced ["Converts to" typename "value."])
    ](
        specialize to/ [type: type]
    )
]
