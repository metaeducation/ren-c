//
//  File: %sys-series.h
//  Summary: {any-series! defs AFTER %tmp-internals.h (see: %sys-rebser.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: the word "Series" is overloaded in Rebol to refer to two related but
// distinct concepts:
//
// * The internal system datatype, also known as a Stub.  It's a low-level
//   implementation of something similar to a vector or an array in other
//   languages.  It is an abstraction which represents a contiguous region
//   of memory containing equally-sized elements.
//
// * The user-level value type ANY-SERIES!.  This might be more accurately
//   called ITERATOR!, because it includes both a pointer to a Stub of
//   data and an index offset into that data.  Attempts to reconcile all
//   the naming issues from historical Rebol have not yielded a satisfying
//   alternative, so the ambiguity has stuck.
//
// This file regards the first meaning of the word "series" and covers the
// low-level implementation details of a Stub and its subclasses.  For info
// about the higher-level ANY-SERIES! value type and its embedded index,
// see %sys-value.h in the definition of `struct Reb_Any_Series`.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A Flex is a contiguous-memory structure with an optimization of behaving
// like a kind of "double-ended queue".  It is able to reserve capacity at
// both the tail and the head, and when data is taken from the head it will
// retain that capacity...reusing it on later insertions at the head.
//
// The space at the head is called the "bias", and to save on pointer math
// per-access, the stored data pointer is actually adjusted to include the
// bias.  This biasing is backed out upon insertions at the head, and also
// must be subtracted completely to free the pointer using the address
// originally given by the allocator.
//
// The element size in a Flex is known as the "width".  It is designed
// to support widths of elements up to 255 bytes.  (See note on SER_FREED
// about accomodating 256-byte elements.)
//
// A Flex may be either manually memory managed or delegated to the garbage
// collector.  Free_Unmanaged_Flex() may only be called on manual series.
// See Manage_Flex()/Push_GC_Guard() for remarks on how to work safely
// with pointers to garbage-collected series, to avoid having them be GC'd
// out from under the code while working with them.
//
// Flex subclasses Array, VarList, REBACT, REBMAP are defined which are
// type-incompatible with Series for safety.  (In C++ they would be derived
// classes, so common operations would not require casting...but it is seen
// as worthwhile to offer some protection even compiling as C.)  The
// subclasses are explained where they are defined in separate header files.
//
// Notes:
//
// * For the struct definition underlying Flex, see Stub in %sys-rebser.h
//
// * It is desirable to have series subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a Flex* would actually be handled in the same
//   way for a Array.  Plus, just because a VarList is implemented as an
//   Array with a link to another Array doesn't mean most clients should
//   be accessing the array--in a C++ build this would mean it would have some
//   kind of protected inheritance scheme.
//


//
// For debugging purposes, it's nice to be able to crash on some kind of guard
// for tracking the call stack at the point of allocation if we find some
// undesirable condition that we want a trace from.  Generally, series get
// set with this guard at allocation time.  But if you want to mark a moment
// later, you can.
//
// This works with Address Sanitizer or with Valgrind, but the config flag to
// enable it only comes automatically with address sanitizer.
//
#if DEBUG_STUB_ORIGINS
    INLINE void Touch_Stub(Flex* s) {
        s->guard = cast(intptr_t*, malloc(sizeof(*s->guard)));
        free(s->guard);

        s->tick = TICK;
    }

    #define Touch_Stub_If_Debug(s) Touch_Stub(s)
#else
    #define Touch_Stub_If_Debug(s) NOOP
#endif


#if DEBUG_MONITOR_STUB
    INLINE void Monitor_Stub(Stub* stub) {
        printf(
            "Adding monitor to %p on tick #%d\n",
            cast(void*, stub),
            cast(int, TG_Tick)
        );
        fflush(stdout);
        Set_Flex_Info(stub, MONITOR_DEBUG);
    }
#endif


//
// The mechanics of the macros that get or set the length of a series are a
// little bit complicated.  This is due to the optimization that allows data
// which is sizeof(Cell) or smaller to fit directly inside the series node.
//
// If a series is not "dynamic" (e.g. has a full pooled allocation) then its
// length is stored in the header.  But if a series is dynamically allocated
// out of the memory pools, then without the data itself taking up the
// "content", there's room for a length in the node.
//

INLINE REBLEN Flex_Len(Flex* s) {
    Byte len_byte = LEN_BYTE_OR_255(s);
    return len_byte == 255 ? s->content.dynamic.len : len_byte;
}

INLINE void Set_Flex_Len(Flex* s, REBLEN len) {
    if (LEN_BYTE_OR_255(s) == 255)
        s->content.dynamic.len = len;
    else {
        assert(len < sizeof(s->content));
        LEN_BYTE_OR_255(s) = len;
    }
}


// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
INLINE Byte *Flex_Data(Flex* s) {
    // if updating, also update manual inlining in SER_AT_RAW

    // The Cell_Varlist(), Cell_Flex(), Cell_Array() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(not (s->info.bits & FLEX_INFO_INACCESSIBLE));

    return LEN_BYTE_OR_255(s) == 255
        ? cast(Byte*, s->content.dynamic.data)
        : cast(Byte*, &s->content);
}

INLINE Byte *Flex_Data_At(Byte w, Flex* s, REBLEN i) {
  #if RUNTIME_CHECKS
    if (w != Flex_Wide(s)) {
        //
        // This is usually a sign that the series was GC'd, as opposed to the
        // caller passing in the wrong width (freeing sets width to 0).  But
        // give some debug tracking either way.
        //
        Byte wide = Flex_Wide(s);
        if (wide == 0)
            printf("Flex_Data_At() asked on freed series\n");
        else
            printf("Flex_Data_At() asked %d on width=%d\n", w, Flex_Wide(s));
        panic (s);
    }
    // The Cell_Varlist(), Cell_Flex(), Cell_Array() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(not (s->info.bits & FLEX_INFO_INACCESSIBLE));
  #endif

    return ((w) * (i)) + ( // v-- inlining of Flex_Data
        (LEN_BYTE_OR_255(s) == 255)
            ? cast(Byte*, s->content.dynamic.data)
            : cast(Byte*, &s->content)
        );
}


//
// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `Flex_Head(t, s)` is the same as `Flex_At(t, s, 0)`
//
// Use C-style cast instead of cast() macro, as it will always be safe and
// this is used very frequently.

#define Flex_At(t,s,i) \
    ((t*)Flex_Data_At(sizeof(t), (s), (i)))

#define Flex_Head(t,s) \
    Flex_At(t, (s), 0)

INLINE Byte *Series_Data_Tail(size_t w, Flex* s) {
    return Flex_Data_At(w, s, Flex_Len(s));
}

#define Flex_Tail(t,s) \
    ((t*)Series_Data_Tail(sizeof(t), (s)))

INLINE Byte *Series_Data_Last(size_t w, Flex* s) {
    assert(Flex_Len(s) != 0);
    return Flex_Data_At(w, s, Flex_Len(s) - 1);
}

#define Series_Last(t,s) \
    ((t*)Series_Data_Last(sizeof(t), (s)))


#define Is_Flex_Full(s) \
    (Flex_Len(s) + 1 >= Flex_Rest(s))

#define Flex_Available_Space(s) \
    (Flex_Rest(s) - (Flex_Len(s) + 1)) // space available (minus terminator)

#define Flex_Fits(s,n) \
    ((Flex_Len(s) + (n) + 1) <= Flex_Rest(s))


//
// Optimized expand when at tail (but, does not reterminate)
//

INLINE void Expand_Flex_Tail(Flex* s, REBLEN delta) {
    if (Flex_Fits(s, delta))
        Set_Flex_Len(s, Flex_Len(s) + delta);
    else
        Expand_Flex(s, Flex_Len(s), delta);
}

//
// Termination
//

INLINE void Term_Non_Array_Flex(Flex* s) {
    assert(not Is_Flex_Array(s));
    memset(Flex_Data_At(Flex_Wide(s), s, Flex_Len(s)), 0, Flex_Wide(s));
}

INLINE void Term_Non_Array_Flex_Len(Flex* s, REBLEN len) {
    Set_Flex_Len(s, len);
    Term_Non_Array_Flex(s);
}

#if NO_RUNTIME_CHECKS
    #define Assert_Flex_Term(s) \
        NOOP
#else
    #define Assert_Flex_Term(s) \
        Assert_Flex_Term_Core(s)
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define Note_Flex_Maybe_Term(s) NOOP


//=////////////////////////////////////////////////////////////////////////=//
//
//  SERIES MANAGED MEMORY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a series is allocated by the Make_Flex() routine, it is not initially
// visible to the garbage collector.  To keep from leaking it, then it must
// be either freed with Free_Unmanaged_Flex or delegated to the GC to manage
// with Manage_Flex().
//
// (In debug builds, there is a test at the end of every Rebol function
// dispatch that checks to make sure one of those two things happened for any
// series allocated during the call.)
//
// The implementation of Manage_Flex() is shallow--it only sets a bit on that
// *one* series, not any series referenced by values inside of it.  This
// means that you cannot build a hierarchical structure that isn't visible
// to the GC and then do a single Manage_Flex() call on the root to hand it
// over to the garbage collector.  While it would be technically possible to
// deeply walk the structure, the efficiency gained from pre-building the
// structure with the managed bit set is significant...so that's how deep
// copies and the scanner/load do it.
//
// (In debug builds, if any unmanaged series are found inside of values
// reachable by the GC, it will raise an alert.)
//

#define Is_Flex_Managed(s) \
    (did ((s)->leader.bits & NODE_FLAG_MANAGED))

INLINE void Force_Flex_Managed(Flex* s) {
    if (not Is_Flex_Managed(s))
        Manage_Flex(s);
}

#if NO_RUNTIME_CHECKS
    #define Assert_Flex_Managed(s) NOOP
#else
    INLINE void Assert_Flex_Managed(Flex* s) {
        if (not Is_Flex_Managed(s))
            panic (s);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX COLORING API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha re-used the same marking flag from the GC in order to do various
// other bit-twiddling tasks when the GC wasn't running.  This is an
// unusually dangerous thing to be doing...because leaving a stray mark on
// during some other traversal could lead the GC to think it had marked
// things reachable from that Flex when it had not--thus freeing something
// that was still in use.
//
// While leaving a stray mark on is a bug either way, GC bugs are particularly
// hard to track down.  So one doesn't want to risk them if not absolutely
// necessary.  Not to mention that sharing state with the GC that you can
// only use when it's not running gets in the way of things like background
// garbage collection, etc.
//
// Ren-C keeps the term "mark" for the GC, since that's standard nomenclature.
// A lot of basic words are taken other places for other things (tags, flags)
// so this just goes with a Flex "color" of black or white, with white as
// the default.  The debug build keeps a count of how many black Flexes there
// are and asserts it's 0 by the time each evaluation ends, to ensure balance.
//

INLINE bool Is_Flex_Black(Flex* s)
  { return Get_Flex_Info(s, BLACK); }

INLINE bool Is_Flex_White(Flex* s)
 { return Not_Flex_Info(s, BLACK); }

INLINE void Flip_Flex_To_Black(Flex* s) {
    assert(Not_Flex_Info(s, BLACK));
    Set_Flex_Info(s, BLACK);
  #if RUNTIME_CHECKS
    ++TG_Num_Black_Flex;
  #endif
}

INLINE void Flip_Flex_To_White(Flex* s) {
    assert(Get_Flex_Info(s, BLACK));
    Clear_Flex_Info(s, BLACK);
  #if RUNTIME_CHECKS
    --TG_Num_Black_Flex;
  #endif
}


//
// Freezing and Locking
//

INLINE void Freeze_Non_Array_Flex(Flex* s) { // there is no unfreeze!
    assert(not Is_Flex_Array(s)); // use Deep_Freeze_Array
    Set_Flex_Info(s, FROZEN_DEEP);
}

INLINE bool Is_Flex_Frozen(Flex* s) {
    assert(not Is_Flex_Array(s)); // use Is_Array_Deeply_Frozen
    return Get_Flex_Info(s, FROZEN_DEEP);
}

INLINE bool Is_Flex_Read_Only(Flex* s) { // may be temporary...
    return did (
        s->info.bits &
        (FLEX_INFO_FROZEN_DEEP | FLEX_INFO_HOLD | FLEX_INFO_PROTECTED)
    );
}

// Gives the appropriate kind of error message for the reason the series is
// read only (frozen, running, protected, locked to be a map key...)
//
// !!! Should probably report if more than one form of locking is in effect,
// but if only one error is to be reported then this is probably the right
// priority ordering.
//
INLINE void Fail_If_Read_Only_Flex(Flex* s) {
    if (Is_Flex_Read_Only(s)) {
        if (Get_Flex_Info(s, AUTO_LOCKED))
            fail (Error_Series_Auto_Locked_Raw());

        if (Get_Flex_Info(s, HOLD))
            fail (Error_Series_Held_Raw());

        if (Get_Flex_Info(s, FROZEN_DEEP))
            fail (Error_Series_Frozen_Raw());

        assert(Get_Flex_Info(s, PROTECTED));
        fail (Error_Series_Protected_Raw());
    }
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING NODES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the evaluator runs (and also when
// ports are used).  So if a series has had Manage_Flex() run on it, the
// potential exists that any C pointers that are outstanding may "go bad"
// if the series wasn't reachable from the root set.  This is important to
// remember any time a pointer is held across a call that runs arbitrary
// user code.
//
// This simple stack approach allows pushing protection for a series, and
// then can release protection only for the last series pushed.  A parallel
// pair of macros exists for pushing and popping of guard status for values,
// to protect any series referred to by the value's contents.  (Note: This can
// only be used on values that do not live inside of series, because there is
// no way to guarantee a value in a series will keep its address besides
// guarding the series AND locking it from resizing.)
//
// The guard stack is not meant to accumulate, and must be cleared out
// before a command ends.
//

#define Push_GC_Guard(p) Push_Guard_Node(p)

#if NO_RUNTIME_CHECKS
    INLINE void Drop_Guard_Node(Node* n) {
        UNUSED(n);
        GC_Guarded->content.dynamic.len--;
    }

    #define Drop_GC_Guard(p) Drop_Guard_Node(p)
#else
    INLINE void Drop_Guard_Node_Debug(
        const Node* n,
        const char *file,
        int line
    ){
        if (n != *Series_Last(const Node*, GC_Guarded))
            panic_at (n, file, line);
        GC_Guarded->content.dynamic.len--;
    }

    #define Drop_GC_Guard(p) \
        Drop_Guard_Node_Debug((p), __FILE__, __LINE__)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

INLINE Flex* Cell_Flex(const Cell* v) {
    assert(Any_Series(v) or Is_Map(v));  // !!! gcc 5.4 -O2 bug
    Flex* s = v->payload.any_series.series;
    if (Get_Flex_Info(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return s;
}

INLINE void Set_Cell_Flex(Cell* v, Flex* s) {
    assert(not Is_Flex_Array(s));
    assert(Is_Flex_Managed(s));
    v->payload.any_series.series = s;
}

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define VAL_INDEX(v) \
        ((v)->payload.any_series.index)
#else
    // allows an assert, but also lvalue: `VAL_INDEX(v) = xxx`
    //
    INLINE REBLEN & VAL_INDEX(Cell* v) { // C++ reference type
        assert(Any_Series(v));
        return v->payload.any_series.index;
    }
    INLINE REBLEN VAL_INDEX(const Cell* v) {
        assert(Any_Series(v));
        return v->payload.any_series.index;
    }
#endif

#define VAL_LEN_HEAD(v) \
    Flex_Len(Cell_Flex(v))

INLINE REBLEN Cell_Series_Len_At(const Cell* v) {
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v))
        return 0; // avoid negative index
    return VAL_LEN_HEAD(v) - VAL_INDEX(v); // take current index into account
}

INLINE Byte *VAL_RAW_DATA_AT(const Cell* v) {
    return Flex_Data_At(Flex_Wide(Cell_Flex(v)), Cell_Flex(v), VAL_INDEX(v));
}

#define Init_Any_Series_At(v,t,s,i) \
    Init_Any_Series_At_Core((v), (t), (s), (i), UNBOUND)

#define Init_Any_Series(v,t,s) \
    Init_Any_Series_At((v), (t), (s), 0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BITSET!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! As written, bitsets use the Any_Series structure in their
// implementation, but are not considered to be an ANY-SERIES! type.
//

INLINE Binary* Cell_Bitset(const Cell* cell) {
    assert(Is_Bitset(cell));
    Flex* s = Cell_Flex(cell);
    assert(Flex_Wide(s) == 1);
    return cast(Binary*, s);
}

#define Init_Bitset(v,s) \
    Init_Any_Series((v), TYPE_BITSET, (s))


// Make a series of a given width (unit size).  The series will be zero
// length to start with, and will not have a dynamic data allocation.  This
// is a particularly efficient default state, so separating the dynamic
// allocation into a separate routine is not a huge cost.
//
INLINE Flex* Alloc_Flex_Stub(Flags flags) {
    assert(not (flags & NODE_FLAG_CELL));

    Flex* s = cast(Flex*, Alloc_Pooled(STUB_POOL));
    if ((GC_Ballast -= sizeof(Stub)) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

    // Out of the 8 platform pointers that comprise a series node, only 3
    // actually need to be initialized to get a functional non-dynamic series
    // or array of length 0!  Two are set here, the third (info) should be
    // set by the caller.
    //
    s->leader.bits = NODE_FLAG_NODE | flags | FLEX_FLAG_8_IS_TRUE;  // #1
    Corrupt_Pointer_If_Debug(LINK(s).corrupt);  // #2
  #if RUNTIME_CHECKS
    memset(cast(char*, &s->content.fixed), 0xBD, sizeof(s->content));  // #3-#6
    memset(&s->info, 0xAE, sizeof(s->info));  // #7, caller sets Flex_Wide()
  #endif
    Corrupt_Pointer_If_Debug(MISC(s).corrupt);  // #8

    // Note: This series will not participate in management tracking!
    // See NODE_FLAG_MANAGED handling in Make_Array_Core() and Make_Flex_Core().

  #if RUNTIME_CHECKS
    Touch_Stub_If_Debug(s); // tag current C stack as series origin in ASAN
    PG_Reb_Stats->Series_Made++;
  #endif

    return s;
}


INLINE REBLEN FIND_POOL(size_t size) {
  #if RUNTIME_CHECKS
    if (PG_Always_Malloc)
        return SYSTEM_POOL;
  #endif

    if (size > 4 * MEM_BIG_SIZE)
        return SYSTEM_POOL;

    return PG_Pool_Map[size]; // ((4 * MEM_BIG_SIZE) + 1) entries
}


// Allocates element array for an already allocated Stub node structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// FLEX_FLAG_FIXED_SIZE are left as they were, and other fields in the
// series structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.
//
INLINE bool Did_Flex_Data_Alloc(Flex* s, REBLEN length) {
    //
    // Currently once a series becomes dynamic, it never goes back.  There is
    // no shrinking process that will pare it back to fit completely inside
    // the Stub node.
    //
    assert(Is_Flex_Dynamic(s)); // caller sets

    Byte wide = Flex_Wide(s);
    assert(wide != 0);

    REBLEN size; // size of allocation (possibly bigger than we need)

    REBLEN pool_num = FIND_POOL(length * wide);
    if (pool_num < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(char*, Alloc_Pooled(pool_num));
        if (not s->content.dynamic.data)
            return false;

        // The pooled allocation might wind up being larger than we asked.
        // Don't waste the space...mark as capacity the series could use.
        size = Mem_Pools[pool_num].wide;
        assert(size >= length * wide);

        // We don't round to power of 2 for allocations in memory pools
        Clear_Flex_Flag(s, POWER_OF_2);
    }
    else {
        // ...the allocation is too big for a pool.  But instead of just
        // doing an unpooled allocation to give you the size you asked
        // for, the system does some second-guessing to align to 2Kb
        // boundaries (or choose a power of 2, if requested).

        size = length * wide;
        if (Get_Flex_Flag(s, POWER_OF_2)) {
            REBLEN len = 2048;
            while (len < size)
                len *= 2;
            size = len;

            // Clear the power of 2 flag if it isn't necessary, due to even
            // divisibility by the item width.
            //
            if (size % wide == 0)
                Clear_Flex_Flag(s, POWER_OF_2);
        }

        s->content.dynamic.data = ALLOC_N(char, size);
        if (not s->content.dynamic.data)
            return false;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

    // Note: Bias field may contain other flags at some point.  Because
    // Set_Flex_Bias() uses bit masking on an existing value, we are sure
    // here to clear out the whole value for starters.
    //
    s->content.dynamic.bias = 0;

    // The allocation may have returned more than we requested, so we note
    // that in 'rest' so that the series can expand in and use the space.
    //
    assert(size % wide == 0);
    s->content.dynamic.rest = size / wide;

    // We set the tail of all series to zero initially, but currently do
    // leave series termination to callers.  (This is under review.)
    //
    s->content.dynamic.len = 0;

    // See if allocation tripped our need to queue a garbage collection

    if ((GC_Ballast -= size) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

    assert(Flex_Total(s) == size);
    return true;
}


// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
//
INLINE Flex* Make_Flex_Core(
    REBLEN capacity,
    Byte wide,
    Flags flags
){
    assert(not (flags & ARRAY_FLAG_HAS_FILE_LINE));

    if (cast(REBU64, capacity) * wide > INT32_MAX)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

    // Non-array series nodes do not need their info bits to conform to the
    // rules of Endlike_Header(), so plain assignment can be used with a
    // non-zero second byte.  However, it obeys the fixed info bits for now.
    // (It technically doesn't need to.)
    //
    Flex* s = Alloc_Flex_Stub(flags);
    s->info.bits =
        FLEX_INFO_0_IS_TRUE
        // not FLEX_INFO_1_IS_FALSE
        // not FLEX_INFO_7_IS_FALSE
        | FLAG_WIDE_BYTE_OR_0(wide);

    if (
        (flags & FLEX_FLAG_ALWAYS_DYNAMIC) // inlining will constant fold
        or (capacity * wide > sizeof(s->content))
    ){
        //
        // Data won't fit in a Stub node, needs a dynamic allocation.  The
        // capacity given back as the ->rest may be larger than the requested
        // size, because the memory pool reports the full rounded allocation.

        LEN_BYTE_OR_255(s) = 255; // alloc caller sets
        if (not Did_Flex_Data_Alloc(s, capacity))
            fail (Error_No_Memory(capacity * wide));

      #if RUNTIME_CHECKS
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use Push_GC_Guard().
    //
    // !!! Code duplicated in Make_Array_Core() ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) {
        if (Is_Flex_Full(GC_Manuals))
            Extend_Flex(GC_Manuals, 8);

        cast(Flex**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s; // start out managed to not need to find/remove from this later
    }

    return s;
}

// !!! When series are made they are not terminated, which means that though
// they are empty they may not be "valid".  Should this be called Alloc_Ser()?
// Is Make_Flex() needed or are there few enough calls it should always take
// the flags and not have a _Core() variant?
//
#define Make_Flex(capacity, wide) \
    Make_Flex_Core((capacity), (wide), FLEX_FLAGS_NONE)
