//
//  File: %sys-roots.h
//  Summary: {Definitions for allocating Value* API handles}
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
// API cells live in singular arrays (which fit within an array Stub, that
// is the size of 2 REBVALs).  But they aren't kept alive by references from
// other values, like the way that an Array* used by a BLOCK! is kept alive.
// They are kept alive by being roots (currently implemented with a flag
// NODE_FLAG_ROOT, but it could also mean living in a distinct pool from
// other series nodes).
//
// The API value content is in the single cell, with LINK().owner holding
// a Context* of the FRAME! that controls its lifetime, or EMPTY_ARRAY.  This
// link field exists in the pointer immediately prior to the Value*, which
// means it can be sniffed as NODE_FLAG_CELL, distinguished from handles that
// were given back with rebAlloc(), so routines can discern them.
//
// MISC() is currently unused, but could serve as a reference count or other
// purpose.  It's not particularly necessary to have API handles use array
// stubs--though the 2*sizeof(Cell) provides some optimality, and it
// means that API stubs can be recycled for more purposes.  But it would
// potentially be better to have them in their own pools, because being
// roots could be discovered without a "pre-pass" in the GC.
//


#define LINK_ApiNext_TYPE       Node*
#define HAS_LINK_ApiNext        FLAVOR_API

#define MISC_ApiPrev_TYPE       Node*
#define HAS_MISC_ApiPrev        FLAVOR_API


// The rebR() function can be used with an API handle to tell a variadic
// function to release that handle after encountering it.
//
#define API_FLAG_RELEASE \
    FLEX_FLAG_24


// What distinguishes an API value is that it has both the NODE_FLAG_CELL and
// NODE_FLAG_ROOT bits set.
//
// !!! Note: The FLAVOR_API state can be converted to an instruction for
// releasing the handle...so beware using FLAVOR_API for detection.
//
INLINE bool Is_Api_Value(const Cell* v) {
    return did (v->header.bits & NODE_FLAG_ROOT);
}

INLINE void Link_Api_Handle_To_Level(Stub* stub, Level* L)
{
    // The head of the list isn't null, but points at the level, so that
    // API freeing operations can update the head of the list in the level
    // when given only the node pointer.

    MISC(ApiPrev, stub) = L;  // back pointer for doubly linked list

    bool empty_list = L->alloc_value_list == L;

    if (not empty_list) {  // head of list exists, take its spot at the head
        Stub* head = cast(Stub*, L->alloc_value_list);
        if (Is_Api_Value(Stub_Cell(head)))
            assert(not Is_Nulled(Stub_Cell(head)));
        MISC(ApiPrev, head) = stub;  // link back
    }

    LINK(ApiNext, stub) = L->alloc_value_list;  // forward pointer
    L->alloc_value_list = stub;
}

INLINE void Unlink_Api_Handle_From_Level(Stub* stub)
{
    Node* prev_node = MISC(ApiPrev, stub);
    Node* next_node = LINK(ApiNext, stub);
    bool at_head = Is_Node_A_Cell(prev_node);
    bool at_tail = Is_Node_A_Cell(next_node);

    if (at_head) {
        Level* L = cast(Level*, prev_node);
        L->alloc_value_list = next_node;

        if (not at_tail) {  // only set next item's backlink if it exists
            Stub* next = cast(Stub*, next_node);
            if (Is_Api_Value(Stub_Cell(next)))
                assert(not Is_Nulled(Stub_Cell(next)));
            MISC(ApiPrev, next) = L;
        }
    }
    else {
        Stub* prev = cast(Stub*, prev_node);  // not at head, api val before us
        if (Is_Api_Value(Stub_Cell(prev)))
            assert(not Is_Nulled(Stub_Cell(prev)));
        LINK(ApiNext, prev) = next_node;  // forward prev's next to our next

        if (not at_tail) {  // only set next item's backlink if it exists
            Stub* next = cast(Stub*, next_node);
            if (Is_Api_Value(Stub_Cell(next)))
                assert(not Is_Nulled(Stub_Cell(next)));
            MISC(ApiPrev, next) = prev_node;
        }
    }

    Corrupt_Pointer_If_Debug(stub->link.any.corrupt);
    Corrupt_Pointer_If_Debug(stub->misc.any.corrupt);
}


// 1. We are introducing the containing node for this cell to the GC and can't
//    leave it uninitialized.  e.g. if `Do_Eval_Into(Alloc_Value(), ...)`
//    is used, there might be a recycle during the evaluation that sees it.
//
// 2. We link the API handle into a doubly linked list maintained by the
//    topmost level at the time the allocation happens.  This level will
//    be responsible for marking the node live, freeing the node in case
//    of a fail() that interrupts the level, and reporting any leaks.
//
// 3. Giving the cell itself NODE_FLAG_ROOT lets a Value* be discerned as
//    either a "public" API handle or not.  We don't want evaluation targets
//    to have this flag, because it's legal for the Level's ->out cell to be
//    nulled--not legal for API values.  So if an evaluation is done into an
//    API handle, the flag has to be off...and then added later.
//
//    Having NODE_FLAG_ROOT is still tolerated as a "fresh" state for
//    purposes of init.  The flag is not copied by Copy_Cell().
//
INLINE Value* Alloc_Value_Core(Flags flags)
{
    Stub* stub = Alloc_Singular(
        FLAG_FLAVOR(API)
            |  NODE_FLAG_ROOT | NODE_FLAG_MANAGED | FLEX_FLAG_FIXED_SIZE
    );

    Cell* cell = Stub_Cell(stub);
    cell->header.bits = flags;  // can't be corrupt [1]

    Link_Api_Handle_To_Level(stub, TOP_LEVEL);  // [2]

    return cast(Value*, cell);
}

#define Alloc_Value() \
    TRACK(Alloc_Value_Core(CELL_MASK_0_ROOT))  // don't use as eval target [3]

INLINE void Free_Value(Value* v)
{
    Stub* stub = Singular_From_Cell(v);
    assert(FLAVOR_BYTE(stub) == FLAVOR_API);
    assert(Is_Node_Root_Bit_Set(stub));

    if (Is_Node_Managed(stub))
        Unlink_Api_Handle_From_Level(stub);

    Poison_Cell(v);  // has to be last (removes NODE_FLAG_ROOT if set)
    GC_Kill_Flex(stub);
}


// If you're going to just fail() anyway, then loose API handles are safe to
// GC.  It's mildly inefficient to do so compared to generating a local cell:
//
//      DECLARE_ATOM (specific);
//      Derelativize(specific, Cell, specifier);
//      fail (Error_Something(specific));
//
// But assuming errors don't happen that often, it's cleaner to have one call.
//
INLINE Value* rebSpecific(const Cell* v, Specifier* specifier)
    { return Derelativize(Alloc_Value(), v, specifier);}


// The evaluator accepts API handles back from action dispatchers, and the
// path evaluator accepts them from path dispatch.  This code does common
// checking used by both, which includes automatic release of the handle
// so the dispatcher can write things like `return rebValue(...);` and not
// encounter a leak.
//
// !!! There is no protocol in place yet for the external API to throw,
// so that is something to think about.  At the moment, only L->out can
// hold thrown returns, and these API handles are elsewhere.
//
INLINE void Release_Api_Value_If_Unmanaged(const Atom* r) {
    assert(Is_Node_Root_Bit_Set(r));

    if (Is_Nulled(r))
        assert(!"Dispatcher returned nulled cell, not C nullptr for API use");

    if (Not_Node_Managed(r))
        rebRelease(x_cast(Value*, r));
}
