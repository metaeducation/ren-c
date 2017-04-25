REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "REBOL 3 Boot Base: Series Functions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: {
        This code is evaluated just after actions, natives, sysobj, and other lower
        levels definitions. This file intializes a minimal working environment
        that is used for the rest of the boot.
    }
]

first: redescribe [
    {Returns the first value of a series.}
](
    specialize 'pick [picker: 1]
)

first+: func [
    {Return the FIRST of a series then increment the series index.}
    return: [<opt> any-value!]
    'word [word!] "Word must refer to a series"
    /local prior
][
    also (pick prior: get word 1) (set word next prior)
]

second: redescribe [
    {Returns the second value of a series.}
](
    specialize 'pick [picker: 2]
)

third: redescribe [
    {Returns the third value of a series.}
](
    specialize 'pick [picker: 3]
)

fourth: redescribe [
    {Returns the fourth value of a series.}
](
    specialize 'pick [picker: 4]
)

fifth: redescribe [
    {Returns the fifth value of a series.}
](
    specialize 'pick [picker: 5]
)

sixth: redescribe [
    {Returns the sixth value of a series.}
](
    specialize 'pick [picker: 6]
)

seventh: redescribe [
    {Returns the seventh value of a series.}
](
    specialize 'pick [picker: 7]
)

eighth: redescribe [
    {Returns the eighth value of a series.}
](
    specialize 'pick [picker: 8]
)

ninth: redescribe [
    {Returns the ninth value of a series.}
](
    specialize 'pick [picker: 9]
)

tenth: redescribe [
    {Returns the tenth value of a series.}
](
    specialize 'pick [picker: 10]
)

last: func [
    {Returns the last value of a series.}
    return: [<opt> any-value!]
    value [any-series! tuple! gob!]
    <local> len
][
    case* [ ;-- returns <opt>, can't use "blankifying" convention

        any-series? value [pick back tail value 1]
        tuple? value [pick value length-of value]
        gob? value [
            ; The C code effectively used 'pick value t' with:
            ;
            ; t = GOB_PANE(VAL_GOB(val)) ? GOB_LEN(VAL_GOB(val)) : 0;
            ; VAL_GOB_INDEX(val) = 0;
            ;
            ; Try getting same result with what series does.  :-/

            pick back tail value 1
        ]
        'else [
            ; C code said "let the action throw the error", but by virtue
            ; of type checking this case should not happen.
            ;
            pick value 0
        ]
    ]
]

;
; !!! End of functions that used to be natives, now mezzanine
;


repend: redescribe [
    "APPEND a reduced value to a series."
](
    adapt 'append [
        if set? 'value [
            value: reduce :value
        ]
    ]
)


; REPEND very literally does what it says, which is to reduce the argument
; and call APPEND.  This is not necessarily the most useful operation.
; Note that `x: 10 | repend [] 'x` would give you `[x]` in R3-Alpha
; and not 10.  The new JOIN (temporarily ADJOIN) and JOIN-OF operations  
; can take more license with their behavior if it makes the function more
; convenient, and not be beholden to the behavior that the name REPEND would
; seem to suggest.
;
join: func [ ;-- renamed to ADJOIN in %sys-start.r for user context, temporary
    "Concatenates values to the end of a series."
    return: [any-series! port! map! gob! object! module! bitset!]
    series [any-series! port! map! gob! object! module! bitset!]
    value [<opt> any-value!]
][
    case [
        block? :value [repend series :value]
        group? :value [
            fail/where "Can't JOIN a GROUP! onto a series (use APPEND)."
        ]
        function? :value [
            fail/where "Can't JOIN a FUNCTION! onto a series (use APPEND)."
        ]
    ] else [
        append/only series :value ;-- paths, words, not in block
    ]
]

join-of: redescribe [
    "Concatenates values to the end of a copy of a series."
](
    adapt 'join [
        series: copy series
    ]
)

append-of: redescribe [
    "APPEND variation that copies the input series first."
](
    adapt 'append [
        series: copy series
    ]
)
