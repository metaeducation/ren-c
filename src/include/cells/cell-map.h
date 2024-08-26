//
//  File: %cell-map.h
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
// !!! Should there be a MAP_LEN()?  Current implementation has VOID in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.
//

#define FLEX_MASK_PAIRLIST \
    (FLAG_FLAVOR(PAIRLIST) \
        | FLEX_FLAG_LINK_NODE_NEEDS_MARK  /* hashlist */)



// See LINK() macro for how this is used.
//
#define LINK_Hashlist_TYPE          Flex*
#define HAS_LINK_Hashlist           FLAVOR_PAIRLIST

INLINE Array* MAP_PAIRLIST(const_if_c Map* map)
  { return x_cast(Array*, map); }

#if CPLUSPLUS_11
    INLINE const Array* MAP_PAIRLIST(const Map* map)
      { return x_cast(const Array*, map); }
#endif

#define MAP_HASHLIST(m) \
    LINK(Hashlist, MAP_PAIRLIST(m))

#define MAP_HASHES(m) \
    Flex_Head(MAP_HASHLIST(m))


INLINE const Map* VAL_MAP(const Cell* v) {
    assert(Cell_Heart(v) == REB_MAP);
    if (Not_Node_Accessible(Cell_Node1(v)))
        fail (Error_Series_Data_Freed_Raw());

    return cast(Map*, Cell_Node1(v));  // pairlist
}

#define VAL_MAP_Ensure_Mutable(v) \
    m_cast(Map*, VAL_MAP(Ensure_Mutable(v)))

#define VAL_MAP_Known_Mutable(v) \
    m_cast(Map*, VAL_MAP(Known_Mutable(v)))

INLINE REBLEN Length_Map(const Map* map)
{
    const Value* tail = Flex_Tail(Value, MAP_PAIRLIST(map));
    const Value* v = Flex_Head(Value, MAP_PAIRLIST(map));

    REBLEN count = 0;
    for (; v != tail; v += 2) {
        if (not Is_Nulled(v + 1))
            ++count;
    }

    return count;
}
