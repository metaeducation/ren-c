//
//  file: %n-series.c
//  summary: "native functions for series"
//  section: natives
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//  insert: native:generic [
//
//  "Inserts element(s); for series, returns just past the insert"
//
//      return: [
//          <null> any-series? port! map! object! bitset! port!
//          integer!  "!!! INSERT returns INTEGER! in ODBC, review this"
//      ]
//      series "At position (modified)"
//          [<opt-out> any-series? port! map! object! bitset! port!]
//      value "What to insert (antiform groups will splice, e.g. SPREAD)"
//          [<opt> element? splice!]
//      :part "Limits to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the insert a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(INSERT)  // Must be frame-compatible with APPEND, CHANGE
{
    INCLUDE_PARAMS_OF_INSERT;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(INSERT));
}


//
//  append: native:generic [
//
//  "Inserts element(s) at tail; for series, returns head"
//
//      return: [any-series? port! map! object! module! bitset!]
//      series "Any position (modified)"
//          [<opt-out> any-series? port! map! object! module! bitset!]
//      value "What to append (antiform groups will splice, e.g. SPREAD)"
//          [<opt> element? splice!]
//      :part "Limits to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the insert a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(APPEND)  // Must be frame-compatible with CHANGE, INSERT
{
    INCLUDE_PARAMS_OF_APPEND;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(APPEND));
}


//
//  change: native:generic [
//
//  "Replaces element(s); returns just past the change"
//
//      return: [any-series? port!]
//      series "At position (modified)"
//          [<opt-out> any-series? port!]
//      value "The new value (antiform groups will splice, e.g. SPREAD)"
//          [<opt> element? splice!]
//      :part "Limits the amount to change to a given length or position"
//          [any-number? any-series? pair!]
//      :dup "Duplicates the change a specified number of times"
//          [any-number? pair!]
//      :line "Data should be its own line (formatting cue if ANY-LIST?)"
//  ]
//
DECLARE_NATIVE(CHANGE)  // Must be frame-compatible with APPEND, INSERT
{
    INCLUDE_PARAMS_OF_CHANGE;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(CHANGE));
}


//
//  take: native:generic [
//
//  "Removes and returns one or more elements"
//
//      return: [any-stable?]  ; !!! Variadic TAKE may evaluate, rethink
//      series "At position (modified)"
//          [<opt-out> any-series? port! varargs!]
//      :part "Specifies a length or end position"
//          [any-number? any-series? pair!]
//      :deep "Also copies series values within the block"
//      :last "Take it from the tail end"
//  ]
//
DECLARE_NATIVE(TAKE)
{
    INCLUDE_PARAMS_OF_TAKE;

    Element* series = Element_ARG(SERIES);
    return Dispatch_Generic(TAKE, series, LEVEL);
}


//
//  remove: native:generic [
//
//  "Removes element(s); returns same position"
//
//      return: [any-series? map! port! bitset!]
//      series "At position (modified)"
//          [<opt-out> any-series? map! port! bitset!]
//      :part "Removes multiple elements or to a given position"
//          [any-number? any-series? pair! char?]
//  ]
//
DECLARE_NATIVE(REMOVE)
{
    INCLUDE_PARAMS_OF_REMOVE;

    Element* series = Element_ARG(SERIES);
    return Dispatch_Generic(REMOVE, series, LEVEL);
}


//
//  clear: native:generic [
//
//  "Removes elements from current position to tail; returns at new tail"
//
//      return: [any-series? port! map! bitset!]
//      series "At position (modified)"
//          [<opt-out> any-series? port! map! bitset!]
//  ]
//
DECLARE_NATIVE(CLEAR)
{
    INCLUDE_PARAMS_OF_CLEAR;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(CLEAR));
}


//
//  swap: native:generic [
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
    INCLUDE_PARAMS_OF_SWAP;

    Element* series1 = Element_ARG(SERIES1);
    return Run_Generic_Dispatch(series1, LEVEL, CANON(SWAP));
}


//
//  reverse: native:generic [
//
//  "Reverses the order of elements; returns at same position"
//
//      return: [any-series? any-sequence? pair!]
//      series "At position (modified)"
//          [<opt-out> any-series? any-sequence? pair!]
//      :part "Limits to a given length or position"
//          [any-number? any-series?]
//  ]
//
DECLARE_NATIVE(REVERSE)
{
    INCLUDE_PARAMS_OF_REVERSE;

    Element* series = Element_ARG(SERIES);
    return Dispatch_Generic(REVERSE, series, LEVEL);
}


//
//  reverse-of: native:generic [
//
//  "Give a copy of the reversal of a value (works on immutable types)"
//
//      return: [fundamental?]
//      value "At position if series"
//          [<opt-out> fundamental?]
//      :part "Limits to a given length or position"
//          [any-number? any-series?]
//  ]
//
DECLARE_NATIVE(REVERSE_OF)
{
    INCLUDE_PARAMS_OF_REVERSE_OF;

    Element* v = Element_ARG(VALUE);

    Bounce bounce;
    if (Try_Dispatch_Generic(&bounce, REVERSE_OF, v, LEVEL))
        return bounce;

    const Stable* datatype = Datatype_Of_Fundamental(v);
    if (
        not Handles_Generic(REVERSE, datatype)
        or not Handles_Generic(COPY, datatype)
    ){
        panic (UNHANDLED);
    }

    Quotify(v);
    return rebDelegate(CANON(REVERSE), CANON(COPY), v);
}


//
//  sort: native:generic [
//
//  "Sorts a series; default sort order is ascending"
//
//      return: [any-series?]
//      series "<opt-out> At position (modified)"
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
    INCLUDE_PARAMS_OF_SORT;

    Element* series = Element_ARG(SERIES);
    return Dispatch_Generic(SORT, series, LEVEL);
}


//
//  skip: native:generic [
//
//  "Returns the series forward or backward from the current position"
//
//      return:
//          [<null> any-series? port!]
//      series [<opt-out> any-series? port!]
//      offset "Input skipped by offset, default to null if out of bounds"
//          [any-number? logic? pair!]
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
    return Dispatch_Generic(SKIP, series, LEVEL);
}


//
//  at: native:generic [
//
//  "Returns the series at the specified index"
//
//      return: [<null> any-series? port!]
//      series [<opt-out> any-series? port!]
//      index "Seeks to given index, not clipped to head/tail by default"
//          [any-number? logic? pair!]
//      :bounded "Return null if index is before tail or after head"
//  ]
//
DECLARE_NATIVE(AT)
{
    INCLUDE_PARAMS_OF_AT;

    Element* series = Element_ARG(SERIES);
    return Dispatch_Generic(AT, series, LEVEL);
}


//
//  find: native:generic [
//
//  "Searches for the position where a matching value is found"
//
//      return: [
//          ~[any-series? any-series?]~
//          "position found and tail of find"
//
//          <null> "if not found"
//      ]
//      series [<opt-out> any-series?]
//      pattern "What to find, if an action call as a predicate on each item"
//          [<opt-out> element? splice! action! datatype!]
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
    INCLUDE_PARAMS_OF_FIND;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(FIND));
}


//
//  select: native:generic [
//
//  "Searches for a value; returns the value that follows, else null"
//
//      return: [any-stable?]
//      series [<opt-out> any-series? any-context? map! bitset!]
//      value [<opt-out> any-stable?]
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
    INCLUDE_PARAMS_OF_SELECT;

    Element* series = Element_ARG(SERIES);
    return Run_Generic_Dispatch(series, LEVEL, CANON(SELECT));
}
