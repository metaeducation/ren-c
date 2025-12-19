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
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// A. INSERT, APPEND, and CHANGE were "frame-compatible generics" in R3-Alpha.
//    They did not have independent native entry points (they were just cases
//    in per-type switch statements), but in Ren-C they actually have their
//    own entry points where common work can be done that apply to all types.
//    This is taken advantage of by having them do things like the ARG(PART)
//    processing in common, and then dispatching to CHANGE as the generic
//    to do the common work.
//
//    It's a bit of a mess due to the historical design--and it's sort of not
//    clear how much work should be done by the "front end" vs. "back end";
//    e.g. if handling voids is done on the front end then that means code
//    that reuses the internals but bypasses the native entry points will not
//    get that handling.  Generally speaking, we probably want most all the
//    code to be going through the native entry points and just endeavor to
//    make that as fast as possible.  But for the moment it's still messy.
//

#include "sys-core.h"


// This is for the specific cases of INSERT and APPEND interacting with :PART,
// implementing a somewhat controversial behavior of only accepting an
// INTEGER! and only speaking in terms of units limited to:
//
// https://github.com/rebol/rebol-issues/issues/2096
// https://github.com/rebol/rebol-issues/issues/2383
//
// Note: the calculation for CHANGE is done based on the series being changed,
// not the properties of the argument:
//
// https://github.com/rebol/rebol-issues/issues/1570
//
static bool Part_Limit_Append_Insert(Option(Stable*) part_arg) {
    if (not part_arg)
        return false;  // will be treated as no limit (decoded as UNLIMITED)

    Stable* part = unwrap part_arg;
    if (Is_Integer(part)) {
        REBINT i = Int32(part);
        if (i < 0) { // Clip negative numbers to mean 0
            Init_Integer(part, 0);  // !!! Would it be better to warn?
            return true;
        }
        return false;
    }

    panic ("APPEND and INSERT only take :PART limit as INTEGER!");
}


// Most routines that take a PART assume that if one is not provided then you
// want to operate on the entire length of the thing you would have otherwise
// been specifying a PART for.  But historical Rebol did not do this for the
// CHANGE function specifically, instead choosing to make the amount replaced
// depend on the size of the value being used to change with.
//
// The traditional way of making that guess wasn't done at a high level, but
// rather was baked into the lower-level service routines.  Ren-C avoids the
// lower-level guessing and instead provides a more limited high-level guess
// that lets the :PART be fully specified at the native level.
//
// 1. This function is allowed to coerce the value just to demonstrate that
//    some of the more "unpopular" :PART behaviors of CHANGE can be achieved
//    with an a-priori determination of the length, if it were deemed to be
//    truly important for these odd cases (I do not believe they are).
//
// 2. Using a SPLICE! to provide a conscious count of how many items to
//    change is straightforward:
//
//        change [a b c] ~(d e)~ => [d e c]
//
//    But do notice that in string or binary cases, the actual amount of
//    material that is spliced in may may be more than the :PART length, e.g.
//    a PART=3 change of "abc" here splices in "ghijkl":
//
//        change "abcdef" ~(g "hi" jkl)~ => "ghijkldef"
//
static REBLEN Guess_Part_Len_For_Change_May_Coerce(
    const Element* series,
    Stable* v
){
    if (Is_Splice(v))
        return Series_Len_At(v);  // [2]

    if (Any_List(series))  // want :PART in items (Length)
        return 1;  // change [a b c] [d e] => [[d e] b c]

    if (Any_String(series)) {  // want :PART in codepoints (Length)
        if (Any_Utf8(v)) {
            Length len;
            Cell_Utf8_Len_Size_At(&len, nullptr, v);
            return len;  // change "abc" "de" => "dec"
        }

        if (Is_Blob(v)) {
            Stable* as_text = rebStable(CANON(AS), CANON(TEXT_X), v);
            Copy_Cell(v, as_text);
            rebRelease(as_text);
            return String_Len_At(v);  // change "abc" #{64 65} => "dec"
        }

        if (Is_Integer(v)) {
            Stable* molded = rebStable(CANON(MOLD), v);
            Copy_Cell(v, molded);
            rebRelease(molded);
            return String_Len_At(v);  // change "abcdef" 100 => "100def"
        }

        panic ("CHANGE length guessing is limited at this time.");
    }

    assert(Is_Blob(series));  // want :PART in bytes (Size)

    if (Is_Integer(v))
        return 1;

    if (Is_Blob(v)) {
        Size size;
        Blob_Size_At(&size, v);  // change #{1234} #{56} => #{5634}
        return size;
    }

    if (Any_Utf8(v)) {
        Size size;
        Cell_Utf8_Len_Size_At(nullptr, &size, v);
        return size;  // change #{1234} #d => #{64 34}
    }

    panic ("CHANGE length guessing is limited at this time.");
}


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
//      {limit}  ; CHANGE expects value limit to be here
//  ]
//
DECLARE_NATIVE(INSERT)  // Must be frame-compatible with CHANGE [A]
{
    INCLUDE_PARAMS_OF_INSERT;

    Element* series = Element_ARG(SERIES);
    if (not Any_Series(series))
        goto handle_non_series;

  handle_series: {

    bool limit_zero = Part_Limit_Append_Insert(ARG(PART));
    Count dups = not ARG(DUP) ? 1 : VAL_UINT32(unwrap ARG(DUP));

    if (limit_zero or dups == 0 or not ARG(VALUE))
        return COPY(series);  // don't panic on read only if would be a no-op

    Copy_Cell(LOCAL(LIMIT), LOCAL(PART));  // :PART acts as CHANGE's LIMIT
    Init_Nulled(LOCAL(PART));
    Init_Integer(LOCAL(DUP), dups);

    STATE = ST_MODIFY_INSERT;
    return Dispatch_Generic(CHANGE, series, LEVEL);  // CHANGE is "MODIFY" [A]

} handle_non_series: { ///////////////////////////////////////////////////////

    return Run_Generic_Dispatch(series, LEVEL, CANON(INSERT));
}}


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
//      {limit}  ; CHANGE expects value limit to be here
//  ]
//
DECLARE_NATIVE(APPEND)
{
    INCLUDE_PARAMS_OF_CHANGE;  // must be frame compatible with CHANGE [A]

    Element* series = Element_ARG(SERIES);
    if (not Any_Series(series))
        goto handle_non_series;

  handle_series: {

    bool limit_zero = Part_Limit_Append_Insert(ARG(PART));
    Count dups = not ARG(DUP) ? 1 : VAL_UINT32(unwrap ARG(DUP));
    Index index = SERIES_INDEX_UNBOUNDED(series);

    if (limit_zero or dups == 0 or not ARG(VALUE)) {
        Copy_Cell(OUT, series);
        goto return_original_position;
    }

    Copy_Cell(LOCAL(LIMIT), LOCAL(PART));  // :PART acts as CHANGE's LIMIT
    Init_Nulled(LOCAL(PART));
    Init_Integer(LOCAL(DUP), dups);

  dispatch_to_generic_modify: {

    SERIES_INDEX_UNBOUNDED(series) = Series_Len_Head(series);  // TAIL

    STATE = ST_MODIFY_INSERT;  // CHANGE is "MODIFY" [A]

    Bounce b = Dispatch_Generic(CHANGE, series, LEVEL);
    b = opt Irreducible_Bounce(LEVEL, b);
    if (b)
        panic ("APPEND is built on INSERT, should not return Bounce");

    goto return_original_position;

} return_original_position: { ////////////////////////////////////////////////

  // Historical Redbol treated (APPEND X Y) as (HEAD INSERT TAIL X Y), but it
  // is arguably more valuable if APPEND always gives back the series it was
  // passed at the position it was passed.  It's easy enough to get the HEAD
  // if that's what you want, but if X was an expression then you'd lose the
  // position if APPEND did the HEAD for you.

    dont(SERIES_INDEX_UNBOUNDED(OUT) = 0);  // old behavior, HEAD OF
    SERIES_INDEX_UNBOUNDED(OUT) = index;

    return OUT;  // don't panic on read only if would be a no-op

}} handle_non_series: { ///////////////////////////////////////////////////////

    return Run_Generic_Dispatch(series, LEVEL, CANON(APPEND));
}}


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
//      :limit "How much of value to use"
//          [any-number? any-series? pair!]
//  ]
//
DECLARE_NATIVE(CHANGE)  // Must be frame-compatible with APPEND, INSERT [A]
{
    INCLUDE_PARAMS_OF_CHANGE;

    Element* series = Element_ARG(SERIES);
    if (not Any_Series(series))
        goto handle_non_series;

  handle_series: {

  // 1. R3-Alpha and Rebol2 say (change/dup/part "abcdef" "g" 0 2) will give
  //    you "ggcdef", but Red will leave it as "abcdef", which seems better.
  //
  // 2. The service routines implementing CHANGE/INSERT/APPEND only accept the
  //    antiform of SPLICE!, so void/null is converted into that here...since
  //    unlike INSERT and APPEND a change with void/null isn't a no-op.

    Count dups = not ARG(DUP) ? 1 : VAL_UINT32(unwrap ARG(DUP));
    if (dups == 0)
        return COPY(series);  // Treat CHANGE as no-op if zero dups [1]

    Stable* v;
    if (not ARG(VALUE))
        v = Init_Hole(LOCAL(VALUE));  // e.g. treat <opt> as empty splice [2]
    else
        v = unwrap ARG(VALUE);

    Length len;
    if (ARG(PART))
        len = Part_Len_May_Modify_Index(series, ARG(PART));
    else
        len = Guess_Part_Len_For_Change_May_Coerce(series, v);  // see notes

    possibly(len == 0);  // CHANGE is not a no-op just due to 0 len

    Init_Integer(LOCAL(PART), len);
    Init_Integer(LOCAL(DUP), dups);

    STATE = ST_MODIFY_CHANGE;
    return Dispatch_Generic(CHANGE, series, LEVEL);  // CHANGE is "MODIFY" [A]

} handle_non_series: { ///////////////////////////////////////////////////////

    return Run_Generic_Dispatch(series, LEVEL, CANON(CHANGE));
}}


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
