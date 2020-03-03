REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Generic function interface definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        Copyright 2012-2018 Rebol Open Source Contributors
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

;-- Binary Math & Logic

add: generic [
    {Returns the addition of two values.}
    value1 [any-scalar! date! binary!]
    value2
]

subtract: generic [
    {Returns the second value subtracted from the first.}
    value1 [any-scalar! date! binary!]
    value2 [any-scalar! date!]
]

multiply: generic [
    {Returns the first value multiplied by the second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

divide: generic [
    {Returns the first value divided by the second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

remainder: generic [
    {Returns the remainder of first value divided by second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

power: generic [
    {Returns the first number raised to the second number.}
    number [any-number!]
    exponent [any-number!]
]


intersect: generic [
    {Returns the intersection (AND) of two values.}
    value1 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
    ]
    value2 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
    ]
    /case
        "Uses case-sensitive comparison"
    /skip
        "Treat the series as records of fixed size"
    size [integer!]
]

union: generic [
    {Returns the union (OR) of two values.}
    value1 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
    ]
    value2 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
    ]
    /case
        "Use case-sensitive comparison"
    /skip
        "Treat the series as records of fixed size"
    size [integer!]
]

difference: generic [
    {Returns the special difference (XOR) of two values.}
    value1 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
        date! ;-- !!! Under review, this really doesn't fit
    ]
    value2 [
        logic! integer! char! tuple! ;-- math
        any-array! any-string! bitset! typeset! ;-- sets
        binary! ;-- ???
        date! ;-- !!! Under review, this really doesn't fit
    ]
    /case
        "Uses case-sensitive comparison"
    /skip
        "Treat the series as records of fixed size"
    size [integer!]
]


;-- Unary

negate: generic [
    {Changes the sign of a number.}
    number [any-number! pair! money! time! bitset!]
]

complement: generic [
    {Returns the one's complement value.}
    value [logic! integer! tuple! binary! bitset! typeset! image!]
]

absolute: generic [
    {Returns the absolute value.}
    value [any-number! pair! money! time!]
]

round: generic [
    {Rounds a numeric value; halves round up (away from zero) by default.}
    value [any-number! pair! money! time!] "The value to round"
    /to "Return the nearest multiple of the scale parameter"
    scale [any-number! money! time!] "Must be a non-zero value"
    /even      "Halves round toward even results"
    /down      "Round toward zero, ignoring discarded digits. (truncate)"
    /half-down "Halves round toward zero"
    /floor     "Round in negative direction"
    /ceiling   "Round in positive direction"
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

;-- Series Navigation

skip: generic [
    {Returns the series forward or backward from the current position.}
    return: [<opt> blank! any-series! gob! port!]
        {Input skipped by the given offset, clipped to head/tail if not /ONLY}
    series [blank! any-series! gob! port!]
    offset [any-number! logic! pair!]
    /only
        {Don't clip to the boundaries of the series (return blank if beyond)}
]

at: generic [
    {Returns the series at the specified index.}
    return: [<opt> blank! any-series! gob! port!]
        {Input at the given index, clipped to head/tail if not /ONLY}
    series [blank! any-series! gob! port!]
    index [any-number! logic! pair!]
    /only
        {Don't clip to the boundaries of the series (return blank if beyond)}

]

;-- Series Search

find: generic [
    {Searches for the position where a matching value is found}
    return: {position found, else blank (void if series is itself blank)}
        [<opt> any-series! blank! bar!]
    series [<blank> any-series! any-context! map! gob! bitset! typeset!]
    value [any-value!]
    /part {Limits the search to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Treats a series value as only a single value}
    /case {Characters are case-sensitive}
    /skip {Treat the series as records of fixed size}
    size [integer!]
    /last {Backwards from end of series}
    /reverse {Backwards from the current position}
    /tail {Returns the end of the series}
    /match {Performs comparison and returns the tail of the match}
]

select: generic [
    {Searches for a value; returns the value that follows, else void.}
    return: [<opt> any-value!]
    series [<blank> any-series! any-context! map!]
    value [any-value!]
    /part {Limits the search to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Treats a series value as only a single value}
    /case {Characters are case-sensitive}
    /skip {Treat the series as records of fixed size}
    size [integer!]
    /last {Backwards from end of series}
    /reverse {Backwards from the current position}
    /tail ;-- for frame compatibility with FIND
    /match ;-- for frame compatibility with FIND

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

;-- Making, copying, modifying

copy: generic [
    {Copies a series, object, or other value.}

    return: {Return type will match the input type, or void if blank}
        [<opt> any-value!]
    value {If an ANY-SERIES!, it is only copied from its current position}
        [any-value!]
    /part {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /deep {Also copies series values within the block}
    /types {What datatypes to copy}
    kinds [typeset! datatype!]
]

take: generic [
    {Removes and returns one or more elements}

    return: [<opt> any-value!]
    series [any-series! port! gob! blank! varargs!] {At position (modified)}
    /part {Specifies a length or end position}
    limit [any-number! any-series! pair! bar!]
    /deep {Also copies series values within the block}
    /last {Take it from the tail end}
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
insert: generic [
    {Inserts element(s); for series, returns just past the insert.}
    return: {Just past the insert (<opt> needed for COLLECT/KEEP, see notes)}
        [<opt> any-value!]
    series [any-series! port! map! gob! object! bitset! port!] {At position (modified)}
    value [<opt> any-value!] {The value to insert}
    /part {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only insert a block as a single value (not the contents of the block)}
    /dup {Duplicates the insert a specified number of times}
    count [any-number! pair!]
    /line {Data should be its own line (use as formatting cue if ANY-ARRAY!)}
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
append: generic [
    {Inserts element(s) at tail; for series, returns head.}
    series [any-series! port! map! gob! object! module! bitset!]
        {Any position (modified)}
    value [<opt> any-value!] {The value to insert}
    /part {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only insert a block as a single value (not the contents of the block)}
    /dup {Duplicates the insert a specified number of times}
    count [any-number! pair!]
    /line {Data should be its own line (use as formatting cue if ANY-ARRAY!)}
]

; !!! INSERT, APPEND, CHANGE expect to have compatible frames...same params
; at the same position, with same types!
;
change: generic [
    {Replaces element(s); returns just past the change.}
    series [any-series! gob! port! struct!]{At position (modified)}
    value [<opt> any-value!] {The new value}
    /part {Limits the amount to change to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only change a block as a single value (not the contents of the block)}
    /dup {Duplicates the change a specified number of times}
    count [any-number! pair!]
    /line {Data should be its own line (use as formatting cue if ANY-ARRAY!)}
]

remove: generic [
    {Removes element(s); returns same position.}
    series [any-series! map! gob! port! bitset!] {At position (modified)}
    /part {Removes multiple elements or to a given position}
    limit [any-number! any-series! pair! char!]
    /map {Remove key from map}
    key
]

clear: generic [
    {Removes elements from current position to tail; returns at new tail.}
    series [any-series! port! map! gob! bitset!] {At position (modified)}
]

swap: generic [
    {Swaps elements between two series or the same series.}
    series1 [any-series! gob!] {At position (modified)}
    series2 [any-series! gob!] {At position (modified)}
]

reverse: generic [
    {Reverses the order of elements; returns at same position.}
    series [any-series! gob! tuple! pair!] {At position (modified)}
    /part {Limits to a given length or position}
    limit [any-number! any-series!]
]

sort: generic [
    {Sorts a series; default sort order is ascending.}
    series [any-series!] {At position (modified)}
    /case {Case sensitive sort}
    /skip {Treat the series as records of fixed size}
    size [integer!] {Size of each record}
    /compare  {Comparator offset, block or action}
    comparator [integer! block! action!]
    /part {Sort only part of a series}
    limit [any-number! any-series!] {Length of series to sort}
    /all {Compare all fields}
    /reverse {Reverse sort order}
]

;-- Port actions:

create: generic [
    {Send port a create request.}
    port [port! file! url! block!]
]

delete: generic [
    {Send port a delete request.}
    port [port! file! url! block!]
]

open: generic [
    {Opens a port; makes a new port from a specification if necessary.}
    spec [port! file! url! block!]
    /new   {Create new file - if it exists, reset it (truncate)}
    /read  {Open for read access}
    /write {Open for write access}
    /seek  {Optimize for random access}
    /allow {Specifies protection attributes}
        access [block!]
]

close: generic [
    {Closes a port/library.}
    return: [<opt> any-value!]
    port [port! library!]
]

read: generic [
    {Read from a file, URL, or other port.}
    return: [<opt> binary! text! block! port!]
        "null on (some) failures !!! REVIEW as part of port model review"
    source [port! file! url! block!]
    /part {Partial read a given number of units (source relative)}
        limit [any-number!]
    /seek {Read from a specific position (source relative)}
        index [any-number!]
    /string {Convert UTF and line terminators to standard text string}
    /lines {Convert to block of strings (implies /string)}
]

write: generic [
    {Writes to a file, URL, or port - auto-converts text strings.}
    destination [port! file! url! block!]
    data [binary! text! block! object!] ; !!! CHAR! support?
        {Data to write (non-binary converts to UTF-8)}
    /part {Partial write a given number of units}
        limit [any-number!]
    /seek {Write at a specific position}
        index [any-number!]
    /append {Write data at end of file}
    /allow {Specifies protection attributes}
        access [block!]
    /lines {Write each value in a block as a separate line}
;   /as {Convert string to a specified encoding}
;       encoding [blank! any-number!] {UTF number (0 8 16 -16)}
]

query: generic [
    {Returns information about a port, file, or URL.}
    return: [<opt> object!]
    target [port! file! url! block!]
    /mode "Get mode information"
    field [word! blank!] "blank will return valid modes for port type"
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
