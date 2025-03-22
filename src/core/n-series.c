//
//  File: %n-series.c
//  Summary: "native functions for series"
//  Section: natives
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  /insert: native:generic [
//
//  "Inserts element(s); for series, returns just past the insert"
//
//      return: "Just past the insert"
//          [any-series? port! map! object! bitset! port!
//          integer!]  ; !!! INSERT returns INTEGER! in ODBC, review this
//      series "At position (modified)"
//          [<maybe> any-series? port! map! object! bitset! port!]
//      value "What to insert (antiform groups will splice, e.g. SPREAD)"
//          [~void~ element? splice!]
//      :part "Limits to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the insert a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(INSERT)  // Must be frame-compatible with APPEND, CHANGE
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(INSERT));
}


//
//  /append: native:generic [
//
//  "Inserts element(s) at tail; for series, returns head"
//
//      return: [any-series? port! map! object! module! bitset!]
//      series "Any position (modified)"
//          [<maybe> any-series? port! map! object! module! bitset!]
//      value "What to append (antiform groups will splice, e.g. SPREAD)"
//          [~void~ element? splice!]
//      :part "Limits to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the insert a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(APPEND)  // Must be frame-compatible with CHANGE, INSERT
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(APPEND));
}


//
//  /change: native:generic [
//
//  "Replaces element(s); returns just past the change"
//
//      return: [any-series? port!]
//      series "At position (modified)"
//          [<maybe> any-series? port!]
//      value "The new value (antiform groups will splice, e.g. SPREAD)"
//          [~void~ element? splice!]
//      :part "Limits the amount to change to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the change a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(CHANGE)  // Must be frame-compatible with APPEND, INSERT
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(CHANGE));
}


//
//  /take: native:generic [
//
//  "Removes and returns one or more elements"
//
//      return: [any-value?]  ; !!! Variadic TAKE may evaluate, rethink
//      series "At position (modified)"
//          [blank! any-series? port! varargs!]
//      :part "Specifies a length or end position"
//          [any-number? any-series? pair!]
//      :deep "Also copies series values within the block"
//      :last "Take it from the tail end"
//  ]
//
DECLARE_NATIVE(TAKE)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(TAKE, series, LEVEL);
}


//
//  /remove: native:generic [
//
//  "Removes element(s); returns same position"
//
//      return: [any-series? map! port! bitset!]
//      series "At position (modified)"
//          [<maybe> any-series? map! port! bitset!]
//      :part "Removes multiple elements or to a given position"
//          [any-number? any-series? pair! char?]
//  ]
//
DECLARE_NATIVE(REMOVE)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(REMOVE, series, LEVEL);
}


//
//  /clear: native:generic [
//
//  "Removes elements from current position to tail; returns at new tail"
//
//      return: [any-series? port! map! bitset!]
//      series "At position (modified)"
//          [<maybe> any-series? port! map! bitset!]
//  ]
//
DECLARE_NATIVE(CLEAR)
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(CLEAR));
}


//
//  /swap: native:generic [
//
//  "Swaps elements between two series or the same series"
//
//      return: [any-series?]
//      series1 [any-series?] "At position (modified)"
//      series2 [any-series?] "At position (modified)"
//  ]
//
DECLARE_NATIVE(SWAP)
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(SWAP));
}


//
//  /reverse: native:generic [
//
//  "Reverses the order of elements; returns at same position"
//
//      return: [any-series? any-sequence? pair!]
//      series "At position (modified)"
//          [<maybe> any-series? any-sequence? pair!]
//      :part "Limits to a given length or position"
//          [any-number? any-series?]
//  ]
//
DECLARE_NATIVE(REVERSE)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(REVERSE, series, LEVEL);
}


//
//  /reverse-of: native:generic [
//
//  "Give a copy of the reversal of a value (works on immutable types)"
//
//      return: [fundamental?]
//      element "At position if series"
//          [<maybe> fundamental?]
//      :part "Limits to a given length or position"
//          [any-number? any-series?]
//  ]
//
DECLARE_NATIVE(REVERSE_OF)
{
    Element* elem = cast(Element*, ARG_N(1));

    Bounce bounce;
    if (Try_Dispatch_Generic(&bounce, REVERSE_OF, elem, LEVEL))
        return bounce;

    Heart heart = Heart_Of_Fundamental(elem);
    if (
        not Handles_Generic(REVERSE, heart)
        or not Handles_Generic(COPY, heart)
    ){
        return UNHANDLED;
    }

    Quotify(elem);
    return rebDelegate(CANON(REVERSE), CANON(COPY), elem);
}


//
//  /sort: native:generic [
//
//  "Sorts a series; default sort order is ascending"
//
//      return: [any-series?]
//      series "<maybe> At position (modified)"
//          [any-series?]
//      :case "Case sensitive sort"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//      :compare "Comparator offset, block or action"
//          [<unrun> integer! block! frame!]
//      :part "Sort only part of a series (by length or position)"
//          [any-number? any-series?]
//      :all "Compare all fields"
//      :reverse "Reverse sort order"
//  ]
//
DECLARE_NATIVE(SORT)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(SORT, series, LEVEL);
}


//
//  /skip: native:generic [
//
//  "Returns the series forward or backward from the current position"
//
//      return: "Input skipped by offset, or null if out of bounds"
//          [~null~ any-series? port!]
//      series [<maybe> any-series? port!]
//      offset [any-number? logic? pair!]
//      :unbounded "Return out of bounds series if before tail or after head"
//  ]
//
DECLARE_NATIVE(SKIP)
//
// !!! SKIP has a meaning for ANY-SERIES? that's different from what it means
// when used with ports.  Right now we make the port case go through the old
// generic dispatch, but this points to a bunch of design work to do.  :-(
{
    INCLUDE_PARAMS_OF_SKIP;

    Element* series = Element_ARG(SERIES);
    USED(ARG(OFFSET));  // other args get passed via LEVEL
    USED(ARG(UNBOUNDED));

    return Dispatch_Generic(SKIP, series, LEVEL);
}


//
//  /at: native:generic [
//
//  "Returns the series at the specified index"
//
//      return: "Input at the given index, not clipped to head/tail by default"
//          [~null~ any-series? port!]
//      series [<maybe> any-series? port!]
//      index [any-number? logic? pair!]
//      :bounded "Return null if index is before tail or after head"
//  ]
//
DECLARE_NATIVE(AT)
{
    Element* series = cast(Element*, ARG_N(1));
    return Dispatch_Generic(AT, series, LEVEL);
}


//
//  /find: native:generic [
//
//  "Searches for the position where a matching value is found"
//
//      return: "position found and tail of find, else null"
//          [~null~ ~[any-series? any-series?]~]
//      series [<maybe> blank! any-series?]
//      pattern "What to find, if an action call as a predicate on each item"
//          [<maybe> element? splice! action!]
//      :part "Limits the search to a given length or position"
//          [any-number? any-series? pair!]
//      :case "Characters are case-sensitive"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//      :match "Performs comparison and returns the tail of the match"
//  ]
//
DECLARE_NATIVE(FIND)  // Must be frame-compatible with SELECT
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(FIND));
}


//
//  /select: native:generic [
//
//  "Searches for a value; returns the value that follows, else null"
//
//      return: [any-value?]
//      series [<maybe> blank! any-series? any-context? map! bitset!]
//      value [<maybe> element? splice! action!]
//      :part "Limits the search to a given length or position"
//          [any-number? any-series? pair!]
//      :case "Characters are case-sensitive"
//      :skip "Treat the series as records of fixed size"
//          [integer!]
//      :match  ; for frame compatibility with FIND
//  ]
//
DECLARE_NATIVE(SELECT)  // Must be frame-compatible with FIND
{
    Element* series = cast(Element*, ARG_N(1));
    return Run_Generic_Dispatch(series, LEVEL, CANON(SELECT));
}
