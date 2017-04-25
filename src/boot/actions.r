REBOL [
    System: "REBOL [R3] Language Interpreter and Run-time Environment"
    Title: "Datatype action definitions"
    Rights: {
        Copyright 2012 REBOL Technologies
        REBOL is a trademark of REBOL Technologies
    }
    License: {
        Licensed under the Apache License, Version 2.0.
        See: http://www.apache.org/licenses/LICENSE-2.0
    }
    Note: [
        "This list is order dependent!"
        "Used to generate C enums and tables"
        "Boot bind attributes are SET and not DEEP"
        "Todo: before beta release remove extra/unused refinements"
    ]
]

;-- Binary Math & Logic

add: action [
    {Returns the addition of two values.}
    value1 [any-scalar! date! binary!]
    value2
]

subtract: action [
    {Returns the second value subtracted from the first.}
    value1 [any-scalar! date! binary!]
    value2 [any-scalar! date!]
]

multiply: action [
    {Returns the first value multiplied by the second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

divide: action [
    {Returns the first value divided by the second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

remainder: action [
    {Returns the remainder of first value divided by second.}
    value1 [any-scalar!]
    value2 [any-scalar!]
]

power: action [
    {Returns the first number raised to the second number.}
    number [any-number!]
    exponent [any-number!]
]

and~: action [
    {Returns the first value ANDed with the second.}
    value1 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
    value2 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
]

or~: action [
    {Returns the first value ORed with the second.}
    value1 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
    value2 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
]

xor~: action [
    {Returns the first value exclusive ORed with the second.}
    value1 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
    value2 [logic! integer! char! tuple! binary! bitset! typeset! datatype!]
]

;-- Unary

negate: action [
    {Changes the sign of a number.}
    number [any-number! pair! money! time! bitset!]
]

complement: action [
    {Returns the one's complement value.}
    value [logic! integer! tuple! binary! bitset! typeset! image!]
]

absolute: action [
    {Returns the absolute value.}
    value [any-number! pair! money! time!]
]

round: action [
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

random: action [
    {Returns a random value of the same datatype; or shuffles series.}
    return: [<opt> any-value!]
    value   {Maximum value of result (modified when series)}
    /seed   {Restart or randomize}
    /secure {Returns a cryptographically secure random number}
    /only   {Pick a random value from a series}
]

odd?: action [
    {Returns TRUE if the number is odd.}
    number [any-number! char! date! money! time! pair!]
]

even?: action [
    {Returns TRUE if the number is even.}
    number [any-number! char! date! money! time! pair!]
]

;-- Series Navigation

head-of: action [
    {Returns the series at its beginning.}
    series [any-series! gob! port!]
]

tail-of: action [
    {Returns the series just past its end.}
    series [any-series! gob! port!]
]

head?: action [
    {Returns TRUE if a series is at its beginning.}
    series [any-series! gob! port!]
]

tail?: action [
    {Returns TRUE if series is at or past its end; or empty for other types.}
    series [any-series! object! gob! port! bitset! map! blank! varargs!]
]

past?: action [
    {Returns TRUE if series is past its end.}
    series [any-series! gob! port!]
]

skip: action [
    {Returns the series forward or backward from the current position.}
    series [any-series! gob! port!]
    offset [any-number! logic! pair!]
]

at: action [
    {Returns the series at the specified index.}
    series [any-series! gob! port!]
    index [any-number! logic! pair!]
]

index-of: action [
    {Returns the current position (index) of the series.}
    series [any-series! gob! port! blank!]
    /xy {Returns index as an XY pair offset}
]

length-of: action [
    {Returns the length (from the current position for series.)}
    series [any-series! port! map! tuple! bitset! object! gob! struct! any-word! blank!]
]

;-- Series Search

find: action [
    {Searches for a value; for series returns where found, else blank.}
    return: [any-series! blank! logic!]
    series [any-series! any-context! map! gob! bitset! typeset! blank!]
    value [<opt> any-value!]
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

select*: action [
    {Searches for a value; returns the value that follows, else void.}
    return: [<opt> any-value!]
    series [any-series! any-context! map! blank!]
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

;;;;!!! MATCH

reflect: action [
    {Returns specific details about a datatype.}
    value [any-value!]
    field [word!] "Such as: spec, body, words, values, title"
]

;-- Making, copying, modifying

copy: action [
    {Copies a series, object, or other value.}

    return: [any-value!]
        {Return type will match the input type.}
    value [any-value!]
        {If an ANY-SERIES!, it is only copied from its current position}
    /part
        {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /deep
        {Also copies series values within the block}
    /types
        {What datatypes to copy}
    kinds [typeset! datatype!]
]

take*: action [
    {Removes and returns one or more elements.}
    return: [<opt> any-value!]
    series [any-series! port! gob! blank! varargs!] {At position (modified)}
    /part {Specifies a length or end position}
    limit [any-number! any-series! pair! bar!]
    /deep {Also copies series values within the block}
    /last {Take it from the tail end}
]

insert: action [
    {Inserts element(s); for series, returns just past the insert.}
    series [any-series! port! map! gob! object! bitset! port!] {At position (modified)}
    value [<opt> any-value!] {The value to insert}
    /part {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only insert a block as a single value (not the contents of the block)}
    /dup {Duplicates the insert a specified number of times}
    count [any-number! pair!]
]

append: action [
    {Inserts element(s) at tail; for series, returns head.}
    series [any-series! port! map! gob! object! module! bitset!]
        {Any position (modified)}
    value [<opt> any-value!] {The value to insert}
    /part {Limits to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only insert a block as a single value (not the contents of the block)}
    /dup {Duplicates the insert a specified number of times}
    count [any-number! pair!]
]

remove: action [
    {Removes element(s); returns same position.}
    series [any-series! map! gob! port! bitset! blank!] {At position (modified)}
    /part {Removes multiple elements or to a given position}
    limit [any-number! any-series! pair! char!]
    /map {Remove key from map}
    key
]

change: action [
    {Replaces element(s); returns just past the change.}
    series [any-series! gob! port! struct!]{At position (modified)}
    value [<opt> any-value!] {The new value}
    /part {Limits the amount to change to a given length or position}
    limit [any-number! any-series! pair!]
    /only {Only change a block as a single value (not the contents of the block)}
    /dup {Duplicates the change a specified number of times}
    count [any-number! pair!]
]

clear: action [
    {Removes elements from current position to tail; returns at new tail.}
    series [any-series! port! map! gob! bitset! blank!] {At position (modified)}
]

trim: action [
    {Removes spaces from strings or blanks from blocks or objects.}
    series [any-series! object! error! module!] {Series (modified) or object (made)}
    /head {Removes only from the head}
    /tail {Removes only from the tail}
    /auto {Auto indents lines relative to first line}
    /lines {Removes all line breaks and extra spaces}
    /all  {Removes all whitespace}
    /with str [char! string! binary! integer!] {Same as /all, but removes characters in 'str'}
]

swap: action [
    {Swaps elements between two series or the same series.}
    series1 [any-series! gob!] {At position (modified)}
    series2 [any-series! gob!] {At position (modified)}
]

reverse: action [
    {Reverses the order of elements; returns at same position.}
    series [any-series! gob! tuple! pair!] {At position (modified)}
    /part {Limits to a given length or position}
    limit [any-number! any-series!]
]

sort: action [
    {Sorts a series; default sort order is ascending.}
    series [any-series!] {At position (modified)}
    /case {Case sensitive sort}
    /skip {Treat the series as records of fixed size}
    size [integer!] {Size of each record}
    /compare  {Comparator offset, block or function}
    comparator [integer! block! function!]
    /part {Sort only part of a series}
    limit [any-number! any-series!] {Length of series to sort}
    /all {Compare all fields}
    /reverse {Reverse sort order}
]

;-- Port actions:

create: action [
    {Send port a create request.}
    port [port! file! url! block!]
]

delete: action [
    {Send port a delete request.}
    port [port! file! url! block!]
]

open: action [
    {Opens a port; makes a new port from a specification if necessary.}
    spec [port! file! url! block!]
    /new   {Create new file - if it exists, reset it (truncate)}
    /read  {Open for read access}
    /write {Open for write access}
    /seek  {Optimize for random access}
    /allow {Specifies protection attributes}
        access [block!]
]

close: action [
    {Closes a port/library.}
    return: [<opt> any-value!]
    port [port! library!]
]

read: action [
    {Read from a file, URL, or other port.}
    source [port! file! url! block!]
    /part {Partial read a given number of units (source relative)}
        limit [any-number!]
    /seek {Read from a specific position (source relative)}
        index [any-number!]
    /string {Convert UTF and line terminators to standard text string}
    /lines {Convert to block of strings (implies /string)}
;   /as {Convert to string using a specified encoding}
;       encoding [blank! any-number!] {UTF number (0 8 16 -16)}
]

write: action [
    {Writes to a file, URL, or port - auto-converts text strings.}
    destination [port! file! url! block!]
    data [binary! string! block! object!] ; !!! CHAR! support?
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

open?: action [
    {Returns TRUE if port is open.}
    port [port!]
]

query: action [
    {Returns information about a port, file, or URL.}
    target [port! file! url! block!]
    /mode "Get mode information"
    field [word! blank!] "NONE will return valid modes for port type"
]

modify: action [
    {Change mode or control for port or file.}
    target [port! file!]
    field [word! blank!]
    value
]

update: action [
    {Updates external and internal states (normally after read/write).}
    port [port!]
]

rename: action [
    {Rename a file.}
    from [port! file! url! block!]
    to [port! file! url! block!]
]

;-- Expectation is that evaluation ends with no result, empty GROUP! does that
()
