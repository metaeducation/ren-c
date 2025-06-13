//
//  file: %sys-stub.h
//  summary: "Stub Definitions AFTER %tmp-internals.h}
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
// See notes in %struct-stub.h for the definition of the Stub structure.
//


//=//// STUB "TOUCH" FOR DEBUGGING ////////////////////////////////////////=//
//
// **IMPORTANT** - This is defined early before code that does manipulation
// on Stub, because it can be very useful in debugging the low-level code.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's nice to be able to trigger a debug_break() after-the-fact on some kind
// of guard which can show the stack where it was set.  Generally, Stubs get
// this guard put on at allocation time.  But if you want to mark a moment
// later as notable to trace back to, you can.
//
// This works with Address Sanitizer or with Valgrind, but the config flag to
// enable it only comes automatically with address sanitizer.
//
// 1. In the general case, you can't assume the incoming stub has valid data,
//    as the default is to call it after only the header bits are set.  But
//    in case it helps, the s->guard is set to nullptr by Alloc_Stub(), so
//    conditional instrumentation here can distinguish fresh from valid.

#if DEBUG_STUB_ORIGINS
    INLINE void Touch_Stub(Stub *s)  // if alloc, only header valid [1]
    {
        s->guard = cast(Byte*, malloc(sizeof(Byte)));  // smallest allocation
        *s->guard = FREE_POOLUNIT_BYTE;  // irrelevant, but disruptive choice
        free(s->guard);

        s->tick = TICK;  // 0 if not TRAMPOLINE_COUNTS_TICKS
    }

    #define Touch_Stub_If_Debug(s) Touch_Stub(s)
#else
    #define Touch_Stub_If_Debug(s) NOOP
#endif


//=//// ERASED STUBS //////////////////////////////////////////////////////=//
//
// Like Cells, Stubs consider the state where their header bits are all 0
// to be "erased".  This is used for restoring stubs to the global init
// state...
//
// 1. !!! Typically nodes aren't zeroed out when they are freed.  Should we
//    do that for this?

INLINE bool Is_Stub_Erased(const Stub* s)
  { return s->leader.bits == STUB_MASK_0; }

INLINE void Erase_Stub(Stub* s) {
    s->leader.bits = STUB_MASK_0;  // just the header, is that all? [1]
    Touch_Stub_If_Debug(s);
}


//=//// STUB "FLAG" BITS //////////////////////////////////////////////////=//
//
// See definitions of STUB_FLAG_XXX.
//
// 1. Avoid cost that inline functions (even constexpr) add to checked builds
//    by "typechecking" via finding the name ->leader.bits in (f).  (The name
//    "leader" is chosen to prevent calls with cells, which use "header".)
//
// 2. Stub flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.  We do this on the HeaderUnion
//    vs. x_cast() on the (f) to get the typechecking of [1]

#define Get_Stub_Flag(f,name) \
    (((f)->leader.bits & STUB_FLAG_##name) != 0)

#define Not_Stub_Flag(f,name) \
    (((f)->leader.bits & STUB_FLAG_##name) == 0)

#define Set_Stub_Flag(f,name) \
    m_cast(HeaderUnion*, &(f)->leader)->bits |= STUB_FLAG_##name

#define Clear_Stub_Flag(f,name) \
    m_cast(HeaderUnion*, &(f)->leader)->bits &= ~STUB_FLAG_##name


//=//// STUB FLAVOR ACCESSORS /////////////////////////////////////////////=//
//
// Most accesses of series via Flex_At(...) and Array_At(...) macros already
// know at the callsite the size of the access.  The width is only a double
// check in RUNTIME_CHECKS builds, used at allocation time and other moments
// when the system has to know the size but doesn't yet know the type.  Hence
// this doesn't need to be particularly fast...so a lookup table is probably
// not needed.  Still, the common cases (array and strings) are put first.


INLINE Flavor Flavor_From_Flags(Flags flags)
  { return u_cast(Flavor, SECOND_BYTE(&flags)); }

#define Stub_Flavor_Unchecked(s) \
    u_cast(Flavor, TASTE_BYTE(s))

#if NO_RUNTIME_CHECKS
    #define Stub_Flavor  Stub_Flavor_Unchecked
#else
    INLINE Flavor Stub_Flavor(const Stub *s) {
        assert(Is_Node_Readable(s));
        assert(TASTE_BYTE(s) != FLAVOR_0);
        return Stub_Flavor_Unchecked(s);
    }
#endif

INLINE Size Wide_For_Flavor(Flavor flavor) {
    assert(flavor != FLAVOR_0);
    if (flavor <= MAX_FLAVOR_HOLDS_CELLS)
        return sizeof(Cell);
    if (flavor >= MIN_FLAVOR_BYTESIZE)
        return 1;
    if (flavor == FLAVOR_BOOKMARKLIST)
        return sizeof(Bookmark);
    if (flavor == FLAVOR_HASHLIST)
        return sizeof(REBLEN);
    if (flavor == FLAVOR_DISPATCHERTABLE)
        return sizeof(DispatcherAndQuerier);
    return sizeof(void*);
}

#define Flex_Wide(f) \
    Wide_For_Flavor(Stub_Flavor(f))


#define Stub_Holds_Cells(f)         (Stub_Flavor(f) <= MAX_FLAVOR_HOLDS_CELLS)

#define Is_Stub_Source(f)           (Stub_Flavor(f) == FLAVOR_SOURCE)

#define Is_Stub_String(f)           (Stub_Flavor(f) >= MIN_FLAVOR_STRING)
#define Is_Stub_Symbol(f)           (Stub_Flavor(f) == FLAVOR_SYMBOL)
#define Is_Stub_Non_Symbol(f)       (Stub_Flavor(f) == FLAVOR_NONSYMBOL)
#define Is_Stub_Stump(f)            (Stub_Flavor(f) == FLAVOR_STUMP)

#define Is_Stub_Let(f)              (Stub_Flavor(f) == FLAVOR_LET)
#define Is_Stub_Use(f)              (Stub_Flavor(f) == FLAVOR_USE)

#define Is_Stub_Patch(f)            (Stub_Flavor(f) == FLAVOR_PATCH)
#define Is_Stub_Sea(f)              (Stub_Flavor(f) == FLAVOR_SEA)

#define Is_Stub_Keylist(f)          (Stub_Flavor(f) == FLAVOR_KEYLIST)
#define Is_Stub_Varlist(f)          (Stub_Flavor(f) == FLAVOR_VARLIST)

#define Is_Stub_Pairlist(f)         (Stub_Flavor(f) == FLAVOR_PAIRLIST)
#define Is_Stub_Details(f)          (Stub_Flavor(f) == FLAVOR_DETAILS)


//=//// STUB FLAVOR-SPECIFIC FLAGS ////////////////////////////////////////=//
//
// In the checked build, ensure_flavor() checks if a Stub matches the expected
// FLAVOR_XXX, and asserts if it does not.  This is used by the subclass
// testing macros as a check that you are testing the flag for the
// Flavor that you expect.
//
// 1. See Set_Stub_Flag()/Clear_Stub_Flag() for why implicit mutability.

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define ensure_flavor(flavor,stub) \
        (stub)  // no-op in release build
#else
    template<typename T>
    INLINE T ensure_flavor(Flavor flavor, T stub) {
        if (Stub_Flavor(stub) != flavor) {
            Flavor actual_flavor = Stub_Flavor(stub);
            USED(actual_flavor);
            assert(!"Stub Flavor did not match what caller expected");
        }
        return stub;
    }
#endif

#define Get_Flavor_Flag(subclass,stub,name) \
    ((ensure_flavor(FLAVOR_##subclass, (stub))->leader.bits \
        & subclass##_FLAG_##name) != 0)

#define Not_Flavor_Flag(subclass,stub,name) \
    ((ensure_flavor(FLAVOR_##subclass, (stub))->leader.bits \
        & subclass##_FLAG_##name) == 0)

#define Set_Flavor_Flag(subclass,stub,name) \
    m_cast(HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (stub))->leader)->bits \
        |= subclass##_FLAG_##name

#define Clear_Flavor_Flag(subclass,stub,name)\
    m_cast(HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (stub))->leader)->bits \
        &= ~subclass##_FLAG_##name


//=//// STUB CELL ACCESS //////////////////////////////////////////////////=//
//
// Certain flavors of Compact stubs indicate to the GC that Compact their data
// contains a Cell that needs to be marked.
//

INLINE Value* Stub_Cell(const_if_c Stub* s) {
    assert(Not_Stub_Flag(s, DYNAMIC));
    assert(Stub_Holds_Cells(s));
    assert(Is_Node_Readable(s));
    return x_cast(Value*, &s->content.fixed.cell);
}

#if CPLUSPLUS_11
    INLINE const Value* Stub_Cell(const Stub* s) {
        assert(Not_Stub_Flag(s, DYNAMIC));
        assert(Stub_Holds_Cells(s));
        assert(Is_Node_Readable(s));
        return u_cast(const Value*, &s->content.fixed.cell);
    }
#endif

INLINE Stub* Compact_Stub_From_Cell(const Cell* v) {
    Stub* s = u_cast(Stub*,  // DEBUG_CHECK_CASTS checks Array
        cast(void*,
            cast(Byte*, m_cast(Cell*, v))
            - offsetof(Stub, content)
        )
    );
    assert(Not_Stub_Flag(s, DYNAMIC));
    return s;
}


// Out of the 8 platform pointers that comprise a Flex Stub, only 3 actually
// need to be initialized to get a functional non-dynamic Flex or Array of
// length 0!  Only two are set here.
//
INLINE Stub* Prep_Stub(Flags flags, void *preallocated) {
    assert(not (flags & NODE_FLAG_CELL));

    Stub *s = u_cast(Stub*, preallocated);
    s->leader.bits = flags | NODE_FLAG_NODE;  // #1

  #if (NO_RUNTIME_CHECKS)
    s->info.flags = FLEX_INFO_MASK_NONE;  // #7
  #else
    Corrupt_Pointer_If_Debug(s->link.corrupt);  // #2
    Mem_Fill(&s->content.fixed, 0xBD, sizeof(s->content));  // #3 - #6
    if (flags & STUB_FLAG_INFO_NODE_NEEDS_MARK)
        Corrupt_Pointer_If_Debug(s->info.node);  // #7
    else
        s->info.flags = FLEX_INFO_MASK_NONE;  // #7
    Corrupt_Pointer_If_Debug(s->misc.corrupt);  // #8

  #if DEBUG_STUB_ORIGINS
    s->guard = nullptr;  // so Touch_Stub() can tell data is invalid
  #endif

    Touch_Stub_If_Debug(s);  // tag current C stack as Flex origin in ASAN
  #endif

  #if DEBUG_COLLECT_STATS
    g_mem.num_flex_made += 1;
  #endif

    return s;
}


// This is a lightweight alternative to Alloc_Singular() when the stub being
// created does not need to be tracked.  It replaces a previous hack of
// allocating the singular as NODE_FLAG_MANAGED so it didn't get into the
// manuals tracking list, but then clearing the bit immediately afterward.
//
// (Because this leaks easily, it should really only be used by low-level code
// that really knows what it's doing, and needs the performance.)
//
INLINE Stub* Make_Untracked_Stub(Flags flags) {
    Flavor flavor = Flavor_From_Flags(flags);
    assert(flavor != FLAVOR_0 and flavor <= MAX_FLAVOR);
    UNUSED(flavor);
    assert(not (flags & (STUB_FLAG_DYNAMIC | FLEX_FLAG_FIXED_SIZE)));
    Stub* s = Prep_Stub(flags | FLEX_FLAG_FIXED_SIZE, Alloc_Stub());
    Force_Erase_Cell(&s->content.fixed.cell);  // should callers do?
    return s;
}


//=////////////////////////////////////////////////////////////////////////=//
//
// STUB COLORING API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha re-used the same marking flag from the GC in order to do various
// other bit-twiddling tasks when the GC wasn't running.  This is an
// unusually dangerous thing to be doing...because leaving a stray mark on
// during some other traversal could lead the GC to think it had marked
// things reachable from that Stub or Flex when it had not--thus freeing
// something that was still in use.
//
// While leaving a stray mark on is a bug either way, GC bugs are particularly
// hard to track down.  So one doesn't want to risk them if not absolutely
// necessary.  Not to mention that sharing state with the GC that you can
// only use when it's not running gets in the way of things like background
// garbage collection, etc.
//
// Ren-C keeps the term "mark" for the GC, since that's standard nomenclature.
// A lot of basic words are taken other places for other things (tags, flags)
// so this just goes with a Stub "color" of black or white, with white as
// the default.  Checked builds keep a count of how many black Flexes there
// are and asserts it's 0 by the time each evaluation ends, to ensure balance.
//

INLINE bool Is_Stub_Black(const Stub* f)
  { return Get_Stub_Flag(f, BLACK); }

INLINE bool Is_Stub_White(const Stub* f)
  { return Not_Stub_Flag(f, BLACK); }

INLINE void Flip_Stub_To_Black(const Stub* f) {
    assert(Not_Stub_Flag(f, BLACK));
    Set_Stub_Flag(f, BLACK);
  #if RUNTIME_CHECKS
    g_mem.num_black_stubs += 1;
  #endif
}

INLINE void Flip_Stub_To_White(const Stub* f) {
    assert(Get_Stub_Flag(f, BLACK));
    Clear_Stub_Flag(f, BLACK);
  #if RUNTIME_CHECKS
    g_mem.num_black_stubs -= 1;
  #endif
}


//=//// STUB_LINK, STUB_MISC, STUB_INFO, STUB_BONUS ///////////////////////=//
//
// These are useful for pointing out in one place how stubs use their slots.
//
// (Try to put those definitions in the %struct-xxx.h files.  Those come
// before these definitions are final, but if you just use macros it should
// work...and keep the definitions alongside STUB_MASK_XXX and FLEX_MASK_XXX
// so a cohesive picture of how the Stub uses slots can be edited together.)
//

#if (! DEBUG_CHECK_GC_HEADER_FLAGS)
    #define Ensure_Stub_Link_Managed(s)  (s)
    #define Ensure_Stub_Misc_Managed(s)  (s)
    #define Ensure_Stub_Info_Managed(s)  (s)
    #define Ensure_Stub_Bonus_Managed(s)  (s)
#else
    INLINE Stub* Ensure_Stub_Link_Managed(const_if_c Stub* s)
      { assert(Get_Stub_Flag(s, LINK_NODE_NEEDS_MARK)); return s; }

    INLINE Stub* Ensure_Stub_Misc_Managed(const_if_c Stub* s)
      { assert(Get_Stub_Flag(s, MISC_NODE_NEEDS_MARK)); return s; }

    INLINE Stub* Ensure_Stub_Info_Managed(const_if_c Stub* s)
      { assert(Get_Stub_Flag(s, INFO_NODE_NEEDS_MARK)); return s; }

    INLINE Stub* Ensure_Stub_Bonus_Managed(const_if_c Stub* s)
      { return s; }  // review: needs check

  #if CPLUSPLUS_11
    INLINE const Stub* Ensure_Stub_Link_Managed(const Stub* s)
      { assert(Get_Stub_Flag(s, LINK_NODE_NEEDS_MARK)); return s; }

    INLINE const Stub* Ensure_Stub_Misc_Managed(const Stub* s)
      { assert(Get_Stub_Flag(s, MISC_NODE_NEEDS_MARK)); return s; }

    INLINE const Stub* Ensure_Stub_Info_Managed(const Stub* s)
      { assert(Get_Stub_Flag(s, INFO_NODE_NEEDS_MARK)); return s; }

    INLINE const Stub* Ensure_Stub_Bonus_Managed(const Stub* s)
      { return s; }  // review: needs check
  #endif
#endif

#define STUB_LINK(s)  Ensure_Stub_Link_Managed(s)->link.node
#define STUB_MISC(s)  Ensure_Stub_Misc_Managed(s)->misc.node
#define STUB_INFO(s)  Ensure_Stub_Info_Managed(s)->info.node
#define STUB_BONUS(s) Ensure_Stub_Bonus_Managed(s)->content.dynamic.bonus.node

#define STUB_LINK_UNMANAGED(s)  (s)->link.node
#define STUB_MISC_UNMANAGED(s)  (s)->misc.node
#define STUB_INFO_UNMANAGED(s)  (s)->info.node
#define STUB_BONUS_UNMANAGED(s) (s)->content.dynamic.bonus.node


//=//// STUB CLEANER //////////////////////////////////////////////////////=//
//
// See STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY for more information.
//

typedef void (StubCleaner)(Stub*);

#define MISC_STUB_CLEANER(s)  (s)->misc.cfunc

INLINE StubCleaner* Stub_Cleaner(const Stub* s) {
    assert(Get_Stub_Flag(s, CLEANS_UP_BEFORE_GC_DECAY));
    assert(MISC_STUB_CLEANER(s) != nullptr);
    return cast(StubCleaner*, MISC_STUB_CLEANER(s));
}

INLINE void Tweak_Stub_Cleaner(Stub* s, StubCleaner* cleaner) {
    assert(Get_Stub_Flag(s, CLEANS_UP_BEFORE_GC_DECAY));
    MISC_STUB_CLEANER(s) = cast(CFunction*, cleaner);
}
