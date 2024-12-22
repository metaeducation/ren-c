//
//  File: %struct-map.h
//  Summary: "Map structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
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
// Maps are implemented as a light hashing layer on top of an array.  The
// hash indices are stored in the series node's "misc", while the values are
// retained in pairs as `[key val key val key val ...]`.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * MAP! was new to R3-Alpha, and the code was brittle and mostly untested.
//   Little attention has been paid to it in Ren-C at time of writing,
//   though the ability to use any value as a key by locking it from being
//   mutated was added.
//
// * In R3-Alpha, when there are too few values in a map to warrant hashing,
//   no hash indices were made and the array was searched linearly.  This was
//   indicated by the hashlist being NULL.  @giuliolunati removed this due
//   to recurring bugs and the relative rarity of maps historically.  Ren-C
//   has many tools for enforcing things like this rigorously, so this
//   feature should be restored...though there are other questions about the
//   implementation of the map (e.g. ordered invariant) that are bigger.
//

#if CPLUSPLUS_11
    struct HashList : public Flex {};  // list of integers
    struct PairList : public Array {};  // keys and values

    struct Map : public Flex {};  // the "pairlist" is the identity
#else
    typedef Flex HashList;
    typedef Flex PairList;

    typedef Flex Map;
#endif


#define FLEX_MASK_PAIRLIST \
    (FLAG_FLAVOR(PAIRLIST) \
        | STUB_FLAG_LINK_NODE_NEEDS_MARK  /* hashlist */)

#define LINK_PAIRLIST_HASHLIST_NODE(pairlist)  STUB_LINK(pairlist)
// MISC is unused
// INFO is normal flags
// BONUS is unused currently, as the pairlist array is never biased
