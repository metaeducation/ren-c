REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generic function interface definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Ren-C Open Source Contributors
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Description: {
        The sense of the term "generic" used here is that of a function which
        has no default implementation--rather each data type supplies its own
        implementation.  The code that runs is based on the argument types:

        https://en.wikipedia.org/wiki/Generic_function
        http://factor-language.blogspot.com/2007/08/mixins.html

        At the moment, only the first argument's type is looked at to choose
        the the dispatch.  This is how common verbs like APPEND or ADD are
        currently implemented.

        !!! The dispatch model in Rebol, and how it might be extended beyond
        the list here (to either more generics, or to user-defined datatypes)
        was not fleshed out, and needs to get attention at some point.
    }
    Notes: {
        Historical Rebol called generics "ACTION!"--a term that has been
        retaken for the "one function datatype":

        https://forum.rebol.info/t/596

        This file is executed during the boot process, after collecting its
        top-level SET-WORD! and binding them into the LIB context.  GENERIC
        is an action which quotes its left-hand side--it does this so that
        it knows the symbol that it is being assigned to.  That symbol is
        what is passed in to the "type dispatcher", so each datatype can
        have its own implementation of the generic function.

        The build process scans this file for the SET-WORD!s also, in order
        to add SYM_XXX constants to the word list--so that switch() statements
        in C can be used during dispatch.
    }
]

; Binary Math & Logic

add: generic [
    {Returns the addition of two values.}
    return: [char! any-scalar! date! binary!]
    value1 [char! any-scalar! date! binary!]
    value2 [char! any-scalar! date!]
]

subtract: generic [
    {Returns the second value subtracted from the first.}
    return: [char! any-scalar! date! binary!]
    value1 [char! any-scalar! date! binary!]
    value2 [char! any-scalar! date!]
]

multiply: generic [
    {Returns the first value multiplied by the second.}
    return: [char! any-scalar!]
    value1 [char! any-scalar!]
    value2 [char! any-scalar!]
]

divide: generic [
    {Returns the first value divided by the second.}
    return: [char! any-scalar!]
    value1 [char! any-scalar!]
    value2 [char! any-scalar!]
]

remainder: generic [
    {Returns the remainder of first value divided by second.}
    return: [char! any-scalar!]
    value1 [char! any-scalar!]
    value2 [char! any-scalar!]
]

power: generic [
    {Returns the first number raised to the second number.}
    return: [any-number!]
    number [any-number!]
    exponent [any-number!]
]


bitwise-and: generic [
    {Bitwise AND of two values}
    return: [logic! integer! char! tuple! binary!]
    value1 [logic! integer! char! tuple! binary!]
    value2 [logic! integer! char! tuple! binary!]
]

bitwise-or: generic [
    {Bitwise OR of two values}
    return: [logic! integer! char! tuple! binary!]
    value1 [logic! integer! char! tuple! binary!]
    value2 [logic! integer! char! tuple! binary!]
]

bitwise-xor: generic [
    {Bitwise XOR of two values}
    return: [logic! integer! char! tuple! binary!]
    value1 [logic! integer! char! tuple! binary!]
    value2 [logic! integer! char! tuple! binary!]
]

bitwise-and-not: generic [
    {Bitwise AND NOT of two values}
    return: [logic! integer! char! tuple! binary!]
    value1 [logic! integer! char! tuple! binary!]
    value2 [logic! integer! char! tuple! binary!]
]


intersect: generic [
    {Returns the intersection (AND) of two sets}

    value1 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
    ]
    value2 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
    ]
    /case "Uses case-sensitive comparison"
    /skip "Treat the series as records of fixed size"
        [integer!]
]

union: generic [
    {Returns the union (OR) of two sets}

    value1 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
    ]
    value2 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
    ]
    /case "Use case-sensitive comparison"
    /skip "Treat the series as records of fixed size"
        [integer!]
]

difference: generic [
    {Returns the special difference (XOR) of two sets}

    value1 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
        date!  ; !!! Under review, this really doesn't fit
    ]
    value2 [
        logic! integer! char! tuple!  ; math
        any-array! any-string! bitset! typeset!  ; sets
        binary!  ; ???
        date!  ; !!! Under review, this really doesn't fit
    ]
    /case "Uses case-sensitive comparison"
    /skip "Treat the series as records of fixed size"
        [integer!]
]

exclude: generic [
    {Returns the first data set less the second data set.}

    data "original data"
        [any-array! any-string! binary! bitset! typeset!]
    exclusions "data to exclude from series"
        [any-array! any-string! binary! bitset! typeset!]
    /case "Uses case-sensitive comparison"
    /skip "Treat the series as records of fixed size"
        [integer!]
]


; Unary

negate: generic [
    {Changes the sign of a number.}
    number [any-number! pair! money! time! bitset!]
]

bitwise-not: generic [
    {Returns the one's complement value.}
    value [logic! integer! tuple! binary!]
]

complement: generic [
    {Returns the inversion of a set}
    value [bitset! typeset!]
]

unique: generic [
    {Returns the data set with duplicates removed}

    series [any-array! any-string! binary! bitset! typeset!]
    dummy:  ; unused, makes frame-compatible with INTERSECT/UNIQUE/etc.
    /case "Use case-sensitive comparison (except bitsets)"
    /skip "Treat the series as records of fixed size"
        [integer!]
]

absolute: generic [
    {Returns the absolute value.}
    value [any-number! pair! money! time!]
]

round: generic [
    {Rounds a numeric value; halves round up (away from zero) by default}

    value "The value to round"
        [any-number! pair! money! time!]
    /to "Return the nearest multiple of the parameter (must be non-zero)"
        [any-number! money! time!]
    /even "Halves round toward even results"
    /down "Round toward zero, ignoring discarded digits. (truncate)"
    /half-down "Halves round toward zero"
    /floor "Round in negative direction"
    /ceiling "Round in positive direction"
    /half-ceiling "Halves round in positive direction"
]

random: generic [
    {Returns a random value of the same datatype; or shuffles series.}
    return: [<opt> any-value!]
    value   {Maximum value of result (modified when series)}
    /seed   {Restart or randomize}
    /secure {Returns a cryptographically secure random number}
    /only   {Pick a random value from a series}
]

odd?: generic [
    {Returns TRUE if the number is odd.}
    number [any-number! char! date! money! time! pair!]
]

even?: generic [
    {Returns TRUE if the number is even.}
    number [any-number! char! date! money! time! pair!]
]

; Series Navigation

skip: generic [
    {Returns the series forward or backward from the current position.}
    return: "Input skipped by offset, or null if out of bounds"
        [<opt> any-series! port!]
    series [<blank> any-series! port!]
    offset [any-number! logic! pair!]
    /unbounded "Return out of bounds series if before tail or after head"
]

at: generic [
    {Returns the series at the specified index.}
    return: "Input at the given index, not clipped to head/tail by default"
        [<opt> any-series! port!]
    series [<blank> any-series! port!]
    index [any-number! logic! pair!]
    /bounded "Return null if index is before tail or after head"
]

; Series Search

find: generic [
    {Searches for the position where a matching value is found}

    return: "position found, else null - logic true if non-positional find"
        [<opt> any-series! logic!]
    series [
        <blank> any-series! any-context! map! bitset! typeset!
    ]
    pattern [any-value!]
    /part "Limits the search to a given length or position"
        [any-number! any-series! pair!]
    /only "Treats a series value as only a single value"
    /case "Characters are case-sensitive"
    /skip "Treat the series as records of fixed size"
        [integer!]
    /tail "Returns the end of the series"
    /match "Performs comparison and returns the tail of the match"
    /reverse "Deprecated: https://forum.rebol.info/t/1126"
    /last "Deprecated: https://forum.rebol.info/t/1126"
]

select: generic [
    {Searches for a value; returns the value that follows, else null}

    return: [<opt> any-value!]
    series [<blank> any-series! any-context! map!]
    value [any-value!]
    /part "Limits the search to a given length or position"
        [any-number! any-series! pair!]
    /only "Treats a series value as only a single value"
    /case "Characters are case-sensitive"
    /skip "Treat the series as records of fixed size"
        [integer!]
    /tail  ; for frame compatibility with FIND
    /match  ; for frame compatibility with FIND
    /reverse "Deprecated: https://forum.rebol.info/t/1126"
    /last "Deprecated: https://forum.rebol.info/t/1126"
]

; !!! PUT was added by Red as the complement to SELECT, which offers a /CASE
; refinement for adding keys to MAP!s case-sensitively.  The name may not
; be ideal, but it's something you can't do with path access, so adopting it
; for the time-being.  Only implemented for MAP!s at the moment
;
put: generic [
    {Replaces the value following a key, and returns the new value.}
    return: [<opt> any-value!]
    series [map!]
    key [any-value!]
    value [<opt> any-value!]
    /case {Perform a case-sensitive search}
]

; Making, copying, modifying

copy: generic [
    {Copies a series, object, or other value.}

    return: "Return type will match the input type, or void if blank"
        [<opt> any-value!]
    value "If an ANY-SERIES!, it is only copied from its current position"
        [any-value!]
    /part "Limits to a given length or position"
        [any-number! any-series! pair!]
    /deep "Also copies series values within the block"
    /types "What datatypes to copy"
        [typeset! datatype!]
]

take: generic [
    {Removes and returns one or more elements}

    return: [<opt> any-value!]
    series "At position (modified)"
        [any-series! port! varargs!]
    /part "Specifies a length or end position"
        [any-number! any-series! pair!]
    /deep "Also copies series values within the block"
    /last "Take it from the tail end"
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
insert: generic [
    {Inserts element(s); for series, returns just past the insert.}

    return: "Just past the insert"
        [any-series! port! map! object! bitset! port!
        integer!]  ; !!! INSERT returns INTEGER! in ODBC, review this
    series "At position (modified)"
        [any-series! port! map! object! bitset! port!]
    value [<opt> any-value!] {The value to insert}
    /part "Limits to a given length or position"
        [any-number! any-series! pair!]
    /only "Insert a block as a single value (not the contents of the block)"
    /dup "Duplicates the insert a specified number of times"
        [any-number! pair!]
    /line "Data should be its own line (use as formatting cue if ANY-ARRAY!)"
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
append: generic [
    {Inserts element(s) at tail; for series, returns head.}

    return: [any-series! port! map! object! module! bitset!]
    series "Any position (modified)"
        [any-series! port! map! object! module! bitset!]
    value [<opt> any-value!]
    /part "Limits to a given length or position"
        [any-number! any-series! pair!]
    /only "Insert a block as a single value (not the contents of the block)"
    /dup "Duplicates the insert a specified number of times"
        [any-number! pair!]
    /line "Data should be its own line (use as formatting cue if ANY-ARRAY!)"
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
change: generic [
    {Replaces element(s); returns just past the change}

    return: [any-series! port!]
    series "At position (modified)"
        [any-series! port!]
    value [<opt> any-value!] {The new value}
    /part "Limits the amount to change to a given length or position"
        [any-number! any-series! pair!]
    /only "Change a block as a single value (not the contents of the block)"
    /dup "Duplicates the change a specified number of times"
        [any-number! pair!]
    /line "Data should be its own line (use as formatting cue if ANY-ARRAY!)"
]

remove: generic [
    {Removes element(s); returns same position}

    return: [any-series! map! port! bitset!]
    series "At position (modified)"
        [any-series! map! port! bitset!]
    /part "Removes multiple elements or to a given position"
        [any-number! any-series! pair! char!]
]

clear: generic [
    {Removes elements from current position to tail; returns at new tail}

    series "At position (modified)"
        [any-series! port! map! bitset!]
]

swap: generic [
    {Swaps elements between two series or the same series}

    series1 [any-series!] {At position (modified)}
    series2 [any-series!] {At position (modified)}
]

reverse: generic [
    {Reverses the order of elements; returns at same position}

    series "At position (modified)"
        [any-series! tuple! pair!]
    /part "Limits to a given length or position"
        [any-number! any-series!]
]

sort: generic [
    {Sorts a series; default sort order is ascending}

    series "At position (modified)"
        [any-series!]
    /case "Case sensitive sort"
    /skip "Treat the series as records of fixed size"
        [integer!]
    /compare "Comparator offset, block or action"
        [integer! block! action!]
    /part "Sort only part of a series (by length or position)"
        [any-number! any-series!]
    /all "Compare all fields"
    /reverse "Reverse sort order"
]

; Port actions:

create: generic [
    {Send port a create request.}
    port [port! file! url! block!]
]

delete: generic [
    {Send port a delete request.}
    port [port! file! url! block!]
]

open: generic [
    {Opens a port; makes a new port from a specification if necessary}

    spec [port! file! url! block!]
    /new "Create new file - if it exists, reset it (truncate)"
    /read "Open for read access"
    /write "Open for write access"
    /seek "Optimize for random access"
    /allow "Specifies protection attributes"
        [block!]
]

close: generic [
    {Closes a port/library.}
    return: [<opt> any-value!]
    port [port!]  ; !!! See Extend_Generics_Someday() for why LIBRARY! works
]

read: generic [
    {Read from a file, URL, or other port.}
    return: "null on (some) failures (REVIEW as part of port model review)" [
        <opt> binary!  ; should all READ return a BINARY!?
        text!  ; READ/STRING returned TEXT!
        block!  ; READ/LINES returned BLOCK!
        port!  ; asynchronous READ on PORT!s returned the PORT!
        tuple!  ; READ/DNS returned tuple!
        void!  ; !!! You get if READ is Ctrl-C'd in nonhaltable API calls, ATM
    ]
    source [port! file! url! block!]
    /part "Partial read a given number of units (source relative)"
        [any-number!]
    /seek "Read from a specific position (source relative)"
        [any-number!]
    /string "Convert UTF and line terminators to standard text string"
    /lines "Convert to block of strings (implies /string)"
]

write: generic [
    {Writes to a file, URL, or port - auto-converts text strings}

    destination [port! file! url! block!]
    data "Data to write (non-binary converts to UTF-8)"
        [binary! text! block! object!]  ; !!! should this support CHAR!?
    /part "Partial write a given number of units"
        [any-number!]
    /seek "Write at a specific position"
        [any-number!]
    /append "Write data at end of file"
    /allow "Specifies protection attributes"
        [block!]
    /lines "Write each value in a block as a separate line"
]

query: generic [
    {Returns information about a port, file, or URL}

    return: [<opt> object!]
    target [port! file! url! block!]
    /mode "Get mode information (blank will return valid modes for port type)"
        [word! blank!]
]

modify: generic [
    {Change mode or control for port or file.}
    return: [logic!]
        "TRUE if successful, FALSE if unsuccessful (!!! REVIEW)"
    target [port! file!]
    field [word! blank!]
    value
]

; This action seems to only be dispatched to *native* ports, and only as part
; of the WAKE-UP function.  It used to have the name UPDATE, but for Ren-C it
; was felt this term would be better applied as a complement to DEFAULT.
; There were no apparent user-facing references in the repo, but it turns out
; to be important it can be called something else.  For now, it's given a
; name most relevant to what it does internally.
;
on-wake-up: generic [
    {Updates external and internal states (normally after read/write).}
    return: [<opt>]
    port [port!]
]

rename: generic [
    {Rename a file.}
    from [port! file! url! block!]
    to [port! file! url! block!]
]
