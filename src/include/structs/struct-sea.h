//
//  File: %struct-sea.h
//  Summary: "Sparse Symbol/Value Store Definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2024 Ren-C Open Source Contributors
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
// In order to make MODULE more friendly to the idea of very large number of
// words, variable instances for a module are stored not in an indexed block
// form like a VarList...but distributed as individual Stub allocations which
// are reachable from the symbol for the word.  This is referred to as the
// "Sea of Variables" approach:
//
//                             SYMBOL HASH TABLE
//  +-----------------+------------------+-----------------+-----------------+
//  |     Symbol*     |     (Vacant)     |     Symbol*     |     Symbol*     |
//  +-----------------+------------------+-----------------+-----------------+
//           |
//           v
//  +-----------------+    +-------------------+    +-------------------+
//  |     SYMBOL      |    | Module3's "PATCH" |    | Module7's "PATCH" |
//  |    ["hitch"] ------->|     ["hitch"] -------->|     ["hitch"] --------v
//  |                 |    |                   |    |                   |   |
//  +-----------------+    |  CELL: [*|*|*|*]  |    |  CELL: [*|*|*|*]  |   |
//           ^             |                   |    |                   |   |
//           |             +-------------------+    +-------------------+   |
//           |                                                              |
//           |       hitch list is circularly linked back to symbol         |
//           ^--------------------------------------------------------------+
//
// So if you have a symbol in your hand, you can go directly to the linked
// list of "patches" for that symbol, and find instances of variables with
// that symbol in modules.
//
// Enumerating all the variables in a "Sea" is not fast: you have to walk
// the entire symbol hash table...and then for each symbol look through the
// circularly-linked "hitch list" for any patches that say they are part
// of that module.  Fortunately, enumerating variables in a module is an
// infrequent operation.
//
// This approach works well for modules...because there's relatively few of
// them, and collisions of the names used in them isn't overly frequent.  But
// if you create hundreds or thousands of objects with many identical keys
// this won't work well.
//

#if CPLUSPLUS_11
    struct SeaOfVars : public Stub {};  // actual variables hang off of Symbols
#else
    typedef Stub SeaOfVars;
#endif


#define FLEX_MASK_SEA \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(SEA) \
        | STUB_FLAG_LINK_NODE_NEEDS_MARK  /* NextVirtual */ \
        | STUB_FLAG_MISC_NODE_NEEDS_MARK  /* Adjunct metadata */)

#define MISC_SEA_ADJUNCT(sea)      STUB_MISC(sea)


//=//// "PATCHES" FOR MODULE VARIABLES ////////////////////////////////////=//
//
// 1. Module variables are in a circularly linked list that includes the
//    symbol series holding that variable's name.  This means the variable
//    can be looked up in that module by following the list reachable through
//    the symbol in a WORD!.  It also means the spelling can be found in
//    that list looking for the symbol.

#define LINK_PATCH_RESERVED(patch)  STUB_LINK_UNMANAGED(patch)
// MISC is used for MISC_HITCH() [1]
#define INFO_PATCH_SEA(patch)       STUB_INFO(patch)
