//
//  file: %stub-api.h
//  summary: "Definitions for allocating Value* API handles"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// API cells live in the single-Cell-worth of content of a "compact" Stub.
// But they aren't kept alive by references from other Cells, the way that an
// Array Stub used by a BLOCK! is kept alive.  They are kept alive by being
/// "roots" (currently implemented with a flag NODE_FLAG_ROOT, but it could
// also mean living in a distinct pool from other Stubs).
//
// The Stub.link and Stub.misc slots point to the next and previous API handles
// which are owned by the same Level* (if the handle is owned by a Level* at
// all, and has not been rebUnmanage()'d).  These are a circularly linked list,
// which terminates with the Level* itself.
//
// Stub.info is currently free, and there are several API header flags that
// are available.  This could be useful in a language binding (e.g. C++ could
// leverage the available space in the API handle stub for something like
// a reference count for a smart pointer wrapper)
//

// The API Action Details can be built manually by things like the TCC
// extension.  It doesn't want to use rebFunction() because it allows a weird
// behavior of defining a function and then having it compiled on demand
// into something that uses the Api_Function_Dispatcher(), and it wants to
// reuse the paramlist it already has.
//
enum {
    IDX_API_ACTION_CFUNC = 1,  // HANDLE! of RebolActionCFunction*
    IDX_API_ACTION_BINDING_BLOCK,  // BLOCK! so binding is GC marked
    MAX_IDX_API_ACTION = IDX_API_ACTION_BINDING_BLOCK
};


#define LINK_API_STUB_NEXT(stub) \
    *x_cast(Node**, &ensure_flavor(FLAVOR_API, (stub))->link.node)

#define MISC_API_STUB_PREV(stub) \
    *x_cast(Node**, &ensure_flavor(FLAVOR_API, (stub))->misc.node)


// The rebR() function can be used with an API handle to tell a variadic
// function to release that handle after encountering it.
//
#define API_FLAG_RELEASE \
    STUB_SUBCLASS_FLAG_24


// What distinguishes an API value is that it has both the NODE_FLAG_CELL and
// NODE_FLAG_ROOT bits set.
//
INLINE bool Is_Api_Value(const Value* v) {
    Assert_Cell_Readable(v);
    return Is_Node_Root_Bit_Set(v);
}

INLINE bool Is_Atom_Api_Value(const Atom* v) {
    Assert_Cell_Readable(v);
    return Is_Node_Root_Bit_Set(v);
}

// 1. The head of the list isn't null, but points at the level, so that
//    API freeing operations can update the head of the list in the level
//    when given only the node pointer.
//
INLINE void Connect_Api_Handle_To_Level(Stub* stub, Level* L)
{
    MISC_API_STUB_PREV(stub) = L;  // back pointer for doubly linked list [1]

    bool empty_list = (L->alloc_value_list == L);

    if (not empty_list) {  // head of list exists, take its spot at the head
        Stub* head = cast(Stub*, L->alloc_value_list);
        MISC_API_STUB_PREV(head) = stub;  // link back
    }

    LINK_API_STUB_NEXT(stub) = L->alloc_value_list;  // forward pointer
    L->alloc_value_list = stub;
}

INLINE void Disconnect_Api_Handle_From_Level(Stub* stub)
{
    Node* prev_node = MISC_API_STUB_PREV(stub);
    Node* next_node = LINK_API_STUB_NEXT(stub);
    bool at_head = Is_Node_A_Cell(prev_node);
    bool at_tail = Is_Node_A_Cell(next_node);

    if (at_head) {
        Level* L = cast(Level*, prev_node);
        L->alloc_value_list = next_node;

        if (not at_tail) {  // only set next item's backlink if it exists
            Stub* next = cast(Stub*, next_node);
            MISC_API_STUB_PREV(next) = L;
        }
    }
    else {
        Stub* prev = cast(Stub*, prev_node);  // not at head, api val before us
        LINK_API_STUB_NEXT(prev) = next_node;  // forward prev next to our next

        if (not at_tail) {  // only set next item's backlink if it exists
            Stub* next = cast(Stub*, next_node);
            MISC_API_STUB_PREV(next) = prev_node;
        }
    }

    Corrupt_Pointer_If_Debug(stub->link.corrupt);
    Corrupt_Pointer_If_Debug(stub->misc.corrupt);
}


// 1. We are introducing the containing node for this cell to the GC and can't
//    leave it uninitialized.  e.g. if `Do_Eval_Into(Alloc_Value(), ...)`
//    is used, there might be a recycle during the evaluation that sees it.
//
// 2. We link the API handle into a doubly linked list maintained by the
//    topmost level at the time the allocation happens.  This level will
//    be responsible for marking the node live, freeing the node in case
//    of a panic() that interrupts the level, and reporting any leaks.
//
// 3. Giving the cell itself NODE_FLAG_ROOT lets a Value* be discerned as
//    either a "public" API handle or not.  We don't want evaluation targets
//    to have this flag, because it's legal for the Level's ->out cell to be
//    erased--not legal for API values.  So if an evaluation is done into an
//    API handle, the flag has to be off...and then added later.
//
//    Having NODE_FLAG_ROOT is still tolerated as a "fresh" state for
//    purposes of init.  The flag is not copied by Copy_Cell().

#define CELL_MASK_API_INITABLE \
    (CELL_MASK_UNREADABLE | NODE_FLAG_ROOT)

INLINE Init(Value) Alloc_Value_Core(Flags flags)
{
    Stub* stub = Make_Untracked_Stub(
        FLAG_FLAVOR(API) | NODE_FLAG_ROOT | NODE_FLAG_MANAGED
    );

    Cell* cell = Stub_Cell(stub);
    cell->header.bits = flags;  // can't be corrupt [1]

    Connect_Api_Handle_To_Level(stub, TOP_LEVEL);  // [2]

    return cell;
}

#define Alloc_Value() \
    TRACK(Alloc_Value_Core(CELL_MASK_API_INITABLE))  // not eval target! [3]

#define Alloc_Element() \
    Init_Quasar(Alloc_Value_Core(CELL_MASK_API_INITABLE))  // same [3]

INLINE void Free_Value(Value* v)
{
    Stub* stub = Compact_Stub_From_Cell(v);
    assert(Stub_Flavor(stub) == FLAVOR_API);
    assert(Is_Node_Root_Bit_Set(stub));

    if (Is_Node_Managed(stub))
        Disconnect_Api_Handle_From_Level(stub);

    Force_Poison_Cell(v);  // do last (removes NODE_FLAG_ROOT if set)
    stub->leader.bits = STUB_MASK_NON_CANON_UNREADABLE;
    GC_Kill_Stub(stub);
}


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
    if (Not_Node_Managed(r))
        rebRelease(x_cast(Value*, r));
}
