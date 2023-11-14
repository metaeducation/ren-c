//
//  File: %sys-map.h
//  Summary: {Definitions for REBMAP}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Maps are implemented as a light hashing layer on top of an array.  The
// hash indices are stored in the series node's "misc", while the values are
// retained in pairs as `[key val key val key val ...]`.
//
// When there are too few values to warrant hashing, no hash indices are
// made and the array is searched linearly.  This is indicated by the hashlist
// being NULL.
//
// Though maps are not considered a series in the "ANY-SERIES!" value sense,
// they are implemented using series--and hence are in %sys-series.h, at least
// until a better location for the definition is found.
//
// !!! Should there be a MAP_LEN()?  Current implementation has VOID in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.
//

#define SERIES_MASK_PAIRLIST \
    (FLAG_FLAVOR(PAIRLIST) \
        | SERIES_FLAG_LINK_NODE_NEEDS_MARK  /* hashlist */)



// See LINK() macro for how this is used.
//
#define LINK_Hashlist_TYPE          Series(*)
#define LINK_Hashlist_CAST          SER
#define HAS_LINK_Hashlist           FLAVOR_PAIRLIST

inline static Array(*) MAP_PAIRLIST(Map(const_if_c*) map)
  { return x_cast(Array(*), map); }

#if CPLUSPLUS_11
    inline static Array(const*) MAP_PAIRLIST(Map(const*) map)
      { return cast(Array(const*), map); }
#endif

#define MAP_HASHLIST(m) \
    LINK(Hashlist, MAP_PAIRLIST(m))

#define MAP_HASHES(m) \
    Series_Head(MAP_HASHLIST(m))


inline static Map(const*) VAL_MAP(NoQuote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_MAP);

    Array(*) a = ARR(VAL_NODE1(v));
    if (Get_Series_Flag(a, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());

    return MAP(a);
}

#define VAL_MAP_Ensure_Mutable(v) \
    m_cast(Map(*), VAL_MAP(Ensure_Mutable(v)))

#define VAL_MAP_Known_Mutable(v) \
    m_cast(Map(*), VAL_MAP(Known_Mutable(v)))

inline static REBLEN Length_Map(Map(const*) map)
{
    Cell(const*) tail = Array_Tail(MAP_PAIRLIST(map));
    const REBVAL *v = SPECIFIC(Array_Head(MAP_PAIRLIST(map)));

    REBLEN count = 0;
    for (; v != tail; v += 2) {
        if (not Is_Nulled(v + 1))
            ++count;
    }

    return count;
}
