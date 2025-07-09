//
//  file: %struct-sea.h
//  summary: "Sparse Symbol/Value Store Definitions preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    struct SeaOfVars : public Context {};  // variables hang off of Symbols
#else
    typedef Stub SeaOfVars;
#endif


#define STUB_MASK_SEA_NO_MARKING \
    (BASE_FLAG_BASE \
        | FLAG_FLAVOR(FLAVOR_SEA) \
        | 0 /* STUB_FLAG_LINK_NEEDS_MARK */  /* NextVirtual, maybe null */ \
        | 0 /* STUB_FLAG_MISC_NEEDS_MARK */  /* Adjunct, maybe null */ \
    )

#define MISC_SEA_ADJUNCT(sea)      STUB_MISC(sea)


//=//// "PATCHES" FOR MODULE VARIABLES ////////////////////////////////////=//
//
// 1. Module variables are in a circularly linked list that includes the
//    symbol series holding that variable's name.  This means the variable
//    can be looked up in that module by following the list reachable through
//    the symbol in a WORD!.  It also means the spelling can be found in
//    that list looking for the symbol.  When GC occurs, the Patch must be
//    removed from the Hitch list before the Patch gets destroyed.
//
// 2. While it may seem that context keeps the module alive and not vice-versa
//    (which marking the context in link might suggest) the reason for this is
//    when patches are cached in variables; then the variable no longer refers
//    directly to the module.
//

#if CPLUSPLUS_11
    struct Patch : public Stub {};
#else
    typedef Stub Patch;
#endif

#define STUB_MASK_PATCH ( \
    FLAG_FLAVOR(FLAVOR_PATCH) \
        | BASE_FLAG_BASE \
        | BASE_FLAG_MANAGED \
        | STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY  /* remove from hitches [1] */ \
        | STUB_FLAG_INFO_NEEDS_MARK  /* context, weird keepalive [2] */ \
        | not STUB_FLAG_LINK_NEEDS_MARK  /* reserved */ \
    )

#define LINK_PATCH_RESERVED(patch)  STUB_LINK_UNMANAGED(patch)
// MISC is used for MISC_HITCH() [1]
#define INFO_PATCH_SEA(patch)       STUB_INFO(patch)


//=//// "STUMPS" USED FOR BINDING /////////////////////////////////////////=//
//
// A "Stump" is a Stub that is ephemeral that is hitched directly onto a
// symbol.  It is used to build mappings from Symbols to indexes in a binder.
//
// 1. We mark the stub's Info* as being a symbol, but there's no actual
//    garbage collection that should be happening while the binder is in use.
//    So there are unlikely to be any GC runs that would see this, unless
//    it was a debug situation of some kind that wound up evaluating and
//    triggering a GC when it wasn't supposed to.

#if CPLUSPLUS_11
    struct Stump : public Stub {};
#else
    typedef Stub Stump;
#endif

#define STUB_MASK_STUMP ( \
    FLAG_FLAVOR(FLAVOR_STUMP) \
        | BASE_FLAG_BASE \
        | not STUB_FLAG_LINK_NEEDS_MARK  /* next stump (not managed) */ \
        | STUB_FLAG_INFO_NEEDS_MARK  /* symbol (but no GC runs!) [1] */ \
    )

#define LINK_STUMP_NEXT(stump)      STUB_LINK(stump)
#define INFO_STUMP_SYMBOL(stump)    STUB_INFO(stump)


//=//// "EXTRA HEART" FOR EXTENSION DATATYPES IS A PATCH //////////////////=//
//
// The system needs to canonize the pointer in the Cell.extra that represents
// an extension type.  It's also desirable for the system to be able to
// give back pointers to an instance of that type without having to allocate
// a cell each time to do it (e.g. Datatype_Of()).
//
// The SeaOfVars mechanism is ideal for this, providing a sparse mapping
// from Symbol* to Cell* with indefinite lifetime, held onto by a Patch.
//

typedef Patch ExtraHeart;
