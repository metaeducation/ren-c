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

#if CPLUSPLUS_11
    struct HashList : public Flex {};  // list of integers
    struct PairList : public Array {};  // keys and values

    struct Map : public Flex {};  // the "pairlist" is the identity
#else
    typedef Flex HashList;
    typedef Flex PairList;

    typedef Flex Map;
#endif
