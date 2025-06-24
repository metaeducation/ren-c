//
//  file: %stub-map.h
//  summary: "Definitions for Map PairList and HashList"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// See %struct-map.h for more information.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * Due to the desire to have a `map` function, its frequently considered
//   that MAP! should be renamed to DICTIONARY!.  Higher priority issues
//   in the design have taken priority over such questions... and this is
//   why the bad names like MAP_PAIRLIST() and MAP_HASHLIST() have not been
//   searched and replaced (as a reminder...)
//
// * It is also being considered that maps be ordered.  Python committed to
//   "OrderedDict" semantics and it may be the right choice.  Again: many
//   other higher priority design questions...
//

// "Zombie" keys in map, represent missing or deleted entries.
//
// We use unreadable (vs. void or null) because it's not an antiform, and we'd
// like to keep the arrays backing a MAP! free of antiforms (vs. making one
// exception for the zombie).  Also, unreadable has nice properties of erroring
// if you try to read it in the checked build.
//
#define Is_Zombie Not_Cell_Readable
#define Init_Zombie Init_Unreadable


INLINE PairList* MAP_PAIRLIST(const_if_c Map* map)
  { return cast(PairList*, map); }

#if CPLUSPLUS_11
    INLINE const PairList* MAP_PAIRLIST(const Map* map)
      { return cast(const PairList*, map); }
#endif

INLINE HashList* Link_Hashlist(const Array* pairlist) {
    assert(Is_Stub_Pairlist(pairlist));
    return cast(HashList*, LINK_PAIRLIST_HASHLIST_NODE(pairlist));
}

INLINE void Tweak_Link_Hashlist(Array* pairlist, HashList* hashlist) {
    assert(Is_Stub_Pairlist(pairlist));
    LINK_PAIRLIST_HASHLIST_NODE(pairlist) = hashlist;
}

#define MAP_HASHLIST(m) \
    Link_Hashlist(MAP_PAIRLIST(m))

#define MAP_HASHES(m) \
    Flex_Head(MAP_HASHLIST(m))

#if CPLUSPLUS_11  // use Hashlist_Num_Slots() instead (see description)
    INLINE Length Flex_Used(const HashList*) = delete;
#endif

// Because the hashlist length is used in a modular calculation, Clang Static
// Analyzer was noticing that Flex_Used() could return 0, so to the best of
// its knowledge that could result in dividing by 0.  This separate entry
// point lets us show the analyzer that we never return 0.
//
INLINE Count Hashlist_Num_Slots(HashList* hashlist) {
    Count used = Flex_Used(cast(Flex*, hashlist));  // subvert C++ deletion
  #if DEBUG_STATIC_ANALYZING
    if (used < 7)  // g_primes[0]
        crash ("Hashlist must have a minimal prime number of entries");
  #else
    assert(used >= 7);  // g_primes[0]
  #endif
    return used;
}
