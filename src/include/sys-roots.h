//
//  File: %sys-roots.h
//  Summary: {Definitions for allocating REBVAL* API handles}
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
// API REBVALs live in singular arrays (which fit inside a REBSER node, that
// is the size of 2 REBVALs).  But they aren't kept alive by references from
// other values, like the way that a REBARR used by a BLOCK! is kept alive.
// They are kept alive by being roots (currently implemented with a flag
// NODE_FLAG_ROOT, but it could also mean living in a distinct pool from
// other series nodes).
//
// The API value content is in the single cell, with LINK().owner holding
// a REBCTX* of the FRAME! that controls its lifetime, or EMPTY_ARRAY.  This
// link field exists in the pointer immediately prior to the REBVAL*, which
// means it can be sniffed as a REBNOD* and distinguished from handles that
// were given back with rebMalloc(), so routines can discern them.
//
// MISC() is currently unused, but could serve as a reference count or other
// purpose.  It's not particularly necessary to have API handles use REBSER
// nodes--though the 2*sizeof(REBVAL) provides some optimality, and it
// means that REBSER nodes can be recycled for more purposes.  But it would
// potentially be better to have them in their own pools, because being
// roots could be discovered without a "pre-pass" in the GC.
//


#define LINK_ApiNext_TYPE       REBNOD*
#define LINK_ApiNext_CAST       // none, just use node (NOD() complains)
#define HAS_LINK_ApiNext        FLAVOR_API

#define MISC_ApiPrev_TYPE       REBNOD*
#define MISC_ApiPrev_CAST       // none, just use node (NOD() complains)
#define HAS_MISC_ApiPrev        FLAVOR_API


// The rebR() function can be used with an API handle to tell a variadic
// function to release that handle after encountering it.
//
#define API_FLAG_RELEASE \
    SERIES_FLAG_24


// What distinguishes an API value is that it has both the NODE_FLAG_CELL and
// NODE_FLAG_ROOT bits set.
//
// !!! Note: The FLAVOR_API state can be converted to an instruction for
// releasing the handle...so beware using FLAVOR_API for detection.
//
inline static bool Is_Api_Value(const Cell *v) {
    assert(v->header.bits & NODE_FLAG_CELL);
    return did (v->header.bits & NODE_FLAG_ROOT);
}

inline static void Link_Api_Handle_To_Frame(REBARR *a, REBFRM *f)
{
    // The head of the list isn't null, but points at the frame, so that
    // API freeing operations can update the head of the list in the frame
    // when given only the node pointer.

    mutable_MISC(ApiPrev, a) = f;  // back pointer for doubly linked list

    bool empty_list = f->alloc_value_list == f;

    if (not empty_list) {  // head of list exists, take its spot at the head
        assert(Is_Api_Value(ARR_SINGLE(ARR(f->alloc_value_list))));
        mutable_MISC(ApiPrev, SER(f->alloc_value_list)) = a;  // link back
    }

    mutable_LINK(ApiNext, a) = f->alloc_value_list;  // forward pointer
    f->alloc_value_list = a;
}

inline static void Unlink_Api_Handle_From_Frame(REBARR *a)
{
    bool at_head = did (
        *cast(REBYTE*, MISC(ApiPrev, a)) & NODE_BYTEMASK_0x01_CELL
    );
    bool at_tail = did (
        *cast(REBYTE*, LINK(ApiNext, a)) & NODE_BYTEMASK_0x01_CELL
    );

    if (at_head) {
        REBFRM *f = FRM(MISC(ApiPrev, a));
        f->alloc_value_list = LINK(ApiNext, a);

        if (not at_tail) {  // only set next item's backlink if it exists
            assert(Is_Api_Value(ARR_SINGLE(ARR(LINK(ApiNext, a)))));
            mutable_MISC(ApiPrev, SER(LINK(ApiNext, a))) = f;
        }
    }
    else {
        // we're not at the head, so there is a node before us, set its "next"
        assert(Is_Api_Value(ARR_SINGLE(ARR(MISC(ApiPrev, a)))));
        mutable_LINK(ApiNext, SER(MISC(ApiPrev, a))) = LINK(ApiNext, a);

        if (not at_tail) {  // only set next item's backlink if it exists
            assert(Is_Api_Value(ARR_SINGLE(ARR(LINK(ApiNext, a)))));
            mutable_MISC(ApiPrev, SER(LINK(ApiNext, a))) = MISC(ApiPrev, a);
        }
    }
}


// We are introducing the containing node for this cell to the GC and can't
// leave it trash.  If a pattern like `Do_Evaluation_Into(Alloc_Value(), ...)`
// is used, then there might be a recycle during the evaluation that sees it.
// Low-level allocation already pulled off making it END with just three
// assignments, see Alloc_Series_Node() for that magic.
//
inline static REBVAL *Alloc_Value(void)
{
    REBARR *a = Alloc_Singular(
        FLAG_FLAVOR(API) |  NODE_FLAG_ROOT | NODE_FLAG_MANAGED
    );

    // Giving the cell itself NODE_FLAG_ROOT lets a REBVAL* be discerned as
    // either an API handle or not.  The flag is not copied by Copy_Cell().
    //
    REBVAL *v = SPECIFIC(ARR_SINGLE(a));

    assert(Is_Stale_Void(v));  // see above, trash could trip up Recycle()
    v->header.bits |= NODE_FLAG_ROOT;  // it's stale (can't use SET_CELL_FLAGS)

    // We link the API handle into a doubly linked list maintained by the
    // topmost frame at the time the allocation happens.  This frame will
    // be responsible for marking the node live, freeing the node in case
    // of a fail() that interrupts the frame, and reporting any leaks.
    //
    Link_Api_Handle_To_Frame(a, FS_TOP);

    return v;
}

inline static void Free_Value(REBVAL *v)
{
    assert(Is_Api_Value(v));

    REBARR *a = Singular_From_Cell(v);
    RESET(ARR_SINGLE(a));

    if (GET_SERIES_FLAG(a, MANAGED))
        Unlink_Api_Handle_From_Frame(a);

    GC_Kill_Series(a);
}


// If you're going to just fail() anyway, then loose API handles are safe to
// GC.  It's mildly inefficient to do so compared to generating a local cell:
//
//      DECLARE_LOCAL (specific);
//      Derelativize(specific, Cell, specifier);
//      fail (Error_Something(specific));
//
// But assuming errors don't happen that often, it's cleaner to have one call.
//
inline static REBVAL *rebSpecific(const Cell *v, REBSPC *specifier)
    { return Derelativize(Alloc_Value(), v, specifier);}


// The evaluator accepts API handles back from action dispatchers, and the
// path evaluator accepts them from path dispatch.  This code does common
// checking used by both, which includes automatic release of the handle
// so the dispatcher can write things like `return rebValue(...);` and not
// encounter a leak.
//
// !!! There is no protocol in place yet for the external API to throw,
// so that is something to think about.  At the moment, only f->out can
// hold thrown returns, and these API handles are elsewhere.
//
inline static void Release_Api_Value_If_Unmanaged(const REBVAL* r) {
    assert(GET_CELL_FLAG(r, ROOT));

    if (VAL_TYPE_UNCHECKED(r) == REB_NULL)  // tolerate isotopes
        assert(!"Dispatcher returned nulled cell, not C nullptr for API use");

    if (NOT_CELL_FLAG(r, MANAGED))
        rebRelease(r);
}
