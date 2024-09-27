//
//  File: %struct-context.h
//  Summary: "Context structure definitions preceding %tmp-internals.h"
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
// See %sys-action.h for information about the workings of REBACT and CONTEXT!
// This file just defines basic structures and flags.
//


// Conceptually a Context maps from Key (Symbols) to values.  But what data
// structure is used depends on the instance.  `Context` is the superclass of
// all the variants that convey Key->Value relationships.
//
// (It may be that someday Pairings or Cell* are considered for binding, but
// right now all instances are derived from Stub.)
//
typedef Stub Context;


//=//// LET: STUB STORAGE FOR JUST ONE VARIABLE ///////////////////////////=//
//
// The LET structure is optimized to store a variable cell and key in a Stub,
// which is the size of two cells.  Because it is a Stub, words can bind
// directly to it.
//
typedef Context Let;


//=//// VARLIST: SIMPLE ARRAY-BASED KEY/VALUE STORE ///////////////////////=//
//
// A "VarList" is the abstraction behind OBJECT!, PORT!, FRAME!, ERROR!, etc.
// It maps keys to values using two parallel Flexes, whose indices line up in
// correspondence:
//
//   "KEYLIST" - a Flex of pointer-sized elements holding Symbol* pointers
//
//   "VARLIST" - an Array which holds an archetypal ANY-CONTEXT? value in its
//   [0] element, and then a cell-sized slot for each variable.
//
// A `VarList*` is an alias of the varlist's `Array*`, and keylists are
// reached through the `->link` of the varlist.  The reason varlists
// are used as the identity of the context is that keylists can be shared
// between contexts.
//
// Indices into the arrays are 0-based for keys and 1-based for values, with
// the [0] elements of the varlist used an archetypal value:
//
//    VARLIST ARRAY (aka VarList*)  --Link--+
//  +------------------------------+        |
//  +          "ROOTVAR"           |        |
//  | Archetype ANY-CONTEXT? Value |        v         KEYLIST SERIES
//  +------------------------------+        +-------------------------------+
//  |         Value Cell 1         |        |         Symbol* Key 1         |
//  +------------------------------+        +-------------------------------+
//  |         Value Cell 2         |        |         Symbol* key 2         |
//  +------------------------------+        +-------------------------------+
//  |         Value Cell ...       |        |         Symbol* key ...       |
//  +------------------------------+        +-------------------------------+
//
// (For executing frames, the ---Link--> is actually to its Level* structure
// so the paramlist of the CTX_FRAME_PHASE() must be consulted.  When the
// frame stops running, the paramlist is written back to the link again.)
//
// The "ROOTVAR" is a canon value image of an ANY-CONTEXT?'s cell.  This
// trick allows a single VarList* pointer to be passed around rather than the
// cell struct which is 4x larger, yet use existing memory to make a Value*
// when needed (using Varlist_Archetype()).  ACTION!s have a similar trick.
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's Array pointer.  Then they cache the index of that
// word's symbol in the context's KeyList, for a fast lookup to get to the
// corresponding var.
//
#if CPLUSPLUS_11
    struct KeyList : public Flex {};
    struct VarList : public Context {};  // Array is implementation detail
#else
    typedef Flex KeyList;
    typedef Flex VarList;
#endif


//=//// SEA OF VARIABLES: SPARSE KEY/VALUE STORE //////////////////////////=//
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
    struct SeaOfVars : public VarList {};  // Variables hang off their Symbol
#else
    typedef Flex SeaOfVars;
#endif


//=//// USE: CONTAINER FOR PUTTING CONTEXTS IN BINDING CHAINS /////////////=//
//
// VarLists, SeaOfVars, and Lets all have pointers by which they can be linked
// into a binding chain.  But that only allows them to be linked into one
// chain--so a Use is a small container that can hold a reference to a
// context as well as a link to the next thing in the binding chain.
//
typedef Context Use;


//=//// ERROR VARLIST SUBLCASS ////////////////////////////////////////////=//
//
// Several implementation functions (e.g. Trap_XXX()) will return an optional
// error.  This isn't very clear as Option(VarList*), so although "Error" is
// a word that conflates the Stub with the ERROR! cell, we go along with
// Option(Error*) as the pragmatically cleanest answer.
//
#if CPLUSPLUS_11
    struct Error : public VarList {};
#else
    typedef VarList Error;
#endif


#define MISC_VarlistAdjunct_TYPE      VarList*
#define HAS_MISC_VarlistAdjunct       FLAVOR_VARLIST


//=//// PARAMLIST_HAS_RETURN //////////////////////////////////////////////=//
//
// See ACT_HAS_RETURN() for remarks.  Note: This is a flag on PARAMLIST, not
// on DETAILS...e.g. the varlist of an exemplar context.
//
#define VARLIST_FLAG_PARAMLIST_HAS_RETURN \
    FLEX_FLAG_24


//=//// FRAME_HAS_BEEN_INVOKED ////////////////////////////////////////////=//
//
// It is intrinsic to the design of Redbols that they are allowed to mutate
// their argument cells.  Hence if you build a frame and then EVAL it, the
// arguments will very likely be changed.  Being able to see these changes
// from the outside in non-debugging cases is dangerous, since it's part of
// the implementation detail of the function (like how it handles locals)
// and is not part of the calling contract.
//
// This is why you can't say things like `loop 2 [do frame]`...the first time
// you do the frame it could be arbitrarily corrupted.  Instead you must copy
// the frame on all but the last time (e.g. `do copy frame, do frame`)
//
// The initial implementation of EVAL of FRAME! would actually create a new
// varlist node and move the data to it--expiring the old node.  That is
// expensive, so the cheaper way to do it is to set a flag on the frame.
// Then, if a frame is archetypal (no phase) it can check this flag before
// an EVAL and say the frame can't be run again...nor can fields be assigned
// or read any longer.
//
// !!! This may not be the best place to put this flag, review.
//
#define VARLIST_FLAG_FRAME_HAS_BEEN_INVOKED \
    FLEX_FLAG_24


//=//// VARLIST_FLAG_PARAMLIST_QUOTES_FIRST ///////////////////////////////=//
//
// This is a calculated property, which is cached by Make_Action().
//
// This is another cached property, needed because lookahead/lookback is done
// so frequently, and it's quicker to check a bit on the function than to
// walk the parameter list every time that function is called.
//
#define VARLIST_FLAG_PARAMLIST_QUOTES_FIRST \
    FLEX_FLAG_25


//=//// VARLIST_FLAG_26 ///////////////////////////////////////////////////=//
//
#define VARLIST_FLAG_26 \
    FLEX_FLAG_26


// These are the flags which are scanned for and set during Make_Action
//
#define PARAMLIST_MASK_CACHED \
    (PARAMLIST_FLAG_QUOTES_FIRST)


#define CELL_MASK_ANY_CONTEXT \
    (CELL_FLAG_FIRST_IS_NODE  /* varlist */ \
        | CELL_FLAG_SECOND_IS_NODE  /* phase (for FRAME!) */)



// A context's varlist is always allocated dynamically, in order to speed
// up variable access--no need to test USED_BYTE_OR_255 for 255.
//
// !!! Ideally this would carry a flag to tell a GC "shrinking" process not
// to reclaim the dynamic memory to make a singular cell...but that flag
// can't be FLEX_FLAG_FIXED_SIZE, because most varlists can expand.
//
#define FLEX_MASK_VARLIST \
    (NODE_FLAG_NODE | FLEX_FLAG_DYNAMIC \
        | FLAG_FLAVOR(VARLIST) \
        | FLEX_FLAG_LINK_NODE_NEEDS_MARK  /* NextVirtual */ \
        | FLEX_FLAG_MISC_NODE_NEEDS_MARK  /* Adjunct */)

#define FLEX_MASK_KEYLIST \
    (NODE_FLAG_NODE  /* NOT always dynamic */ \
        | FLAG_FLAVOR(KEYLIST) \
        | FLEX_FLAG_LINK_NODE_NEEDS_MARK  /* ancestor */ )


#define Varlist_Array(ctx) \
    x_cast(Array*, ensure(VarList*, ctx))
