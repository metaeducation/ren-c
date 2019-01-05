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
// * The internal system datatype, also known as a REBSER.  It's a low-level
//   implementation of something similar to a vector or an array in other
//   languages.  It is an abstraction which represents a contiguous region
//   of memory containing equally-sized elements.
//
// * The user-level value type ANY-SERIES!.  This might be more accurately
//   called ITERATOR!, because it includes both a pointer to a REBSER of
//   data and an index offset into that data.  Attempts to reconcile all
//   the naming issues from historical Rebol have not yielded a satisfying
//   alternative, so the ambiguity has stuck.
//
// This file regards the first meaning of the word "series" and covers the
// low-level implementation details of a REBSER and its subclasses.  For info
// about the higher-level ANY-SERIES! value type and its embedded index,
// see %sys-value.h in the definition of `struct Reb_Any_Series`.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A REBSER is a contiguous-memory structure with an optimization of behaving
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
// The element size in a REBSER is known as the "width".  It is designed
// to support widths of elements up to 255 bytes.  (See note on SER_FREED
// about accomodating 256-byte elements.)
//
// REBSERs may be either manually memory managed or delegated to the garbage
// collector.  Free_Unmanaged_Series() may only be called on manual series.
// See MANAGE_SERIES()/PUSH_GC_GUARD() for remarks on how to work safely
// with pointers to garbage-collected series, to avoid having them be GC'd
// out from under the code while working with them.
//
// Series subclasses REBARR, REBCTX, REBACT, REBMAP are defined which are
// type-incompatible with REBSER for safety.  (In C++ they would be derived
// classes, so common operations would not require casting...but it is seen
// as worthwhile to offer some protection even compiling as C.)  The
// subclasses are explained where they are defined in separate header files.
//
// Notes:
//
// * For the struct definition of REBSER, see %sys-rebser.h
//
// * It is desirable to have series subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a REBSER* would actually be handled in the same
//   way for a REBARR*.  Plus, just because a REBCTX* is implemented as a
//   REBARR* with a link to another REBARR* doesn't mean most clients should
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
#if defined(DEBUG_SERIES_ORIGINS) || defined(DEBUG_COUNT_TICKS)
    inline static void Touch_Series_Debug(void *p) {
        REBSER *s = SER(p); // allow REBARR, REBCTX, REBACT...

      #if defined(DEBUG_SERIES_ORIGINS)
        s->guard = cast(intptr_t*, malloc(sizeof(*s->guard)));
        free(s->guard);
      #endif

      #if defined(DEBUG_COUNT_TICKS)
        s->tick = TG_Tick;
      #else
        s->tick = 0;
      #endif
    }

    #define TOUCH_SERIES_IF_DEBUG(s) \
        Touch_Series_Debug(s)
#else
    #define TOUCH_SERIES_IF_DEBUG(s) \
        NOOP
#endif


#if defined(DEBUG_MONITOR_SERIES)
    inline static void MONITOR_SERIES(void *p) {
        printf("Adding monitor to %p on tick #%d\n", p, cast(int, TG_Tick));
        fflush(stdout);
        SET_SER_INFO(p, SERIES_INFO_MONITOR_DEBUG);
    }
#endif


//
// The mechanics of the macros that get or set the length of a series are a
// little bit complicated.  This is due to the optimization that allows data
// which is sizeof(REBVAL) or smaller to fit directly inside the series node.
//
// If a series is not "dynamic" (e.g. has a full pooled allocation) then its
// length is stored in the header.  But if a series is dynamically allocated
// out of the memory pools, then without the data itself taking up the
// "content", there's room for a length in the node.
//

inline static REBCNT SER_LEN(REBSER *s) {
    REBYTE len_byte = LEN_BYTE_OR_255(s);
    return len_byte == 255 ? s->content.dynamic.len : len_byte;
}

inline static void SET_SERIES_LEN(REBSER *s, REBCNT len) {
    assert(NOT_SER_FLAG(s, SERIES_FLAG_STACK));

    if (LEN_BYTE_OR_255(s) == 255)
        s->content.dynamic.len = len;
    else {
        assert(len < sizeof(s->content));
        mutable_LEN_BYTE_OR_255(s) = len;
    }
}


// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
inline static REBYTE *SER_DATA_RAW(REBSER *s) {
    // if updating, also update manual inlining in SER_AT_RAW

    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(not (s->info.bits & SERIES_INFO_INACCESSIBLE));

    return LEN_BYTE_OR_255(s) == 255
        ? cast(REBYTE*, s->content.dynamic.data)
        : cast(REBYTE*, &s->content);
}

inline static REBYTE *SER_AT_RAW(REBYTE w, REBSER *s, REBCNT i) {   
  #if !defined(NDEBUG)
    if (w != SER_WIDE(s)) {
        //
        // This is usually a sign that the series was GC'd, as opposed to the
        // caller passing in the wrong width (freeing sets width to 0).  But
        // give some debug tracking either way.
        //
        REBYTE wide = SER_WIDE(s);
        if (wide == 0)
            printf("SER_AT_RAW asked on freed series\n");
        else
            printf("SER_AT_RAW asked %d on width=%d\n", w, SER_WIDE(s));
        panic (s);
    }
    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(not (s->info.bits & SERIES_INFO_INACCESSIBLE));
  #endif

    return ((w) * (i)) + ( // v-- inlining of SER_DATA_RAW
        (LEN_BYTE_OR_255(s) == 255)
            ? cast(REBYTE*, s->content.dynamic.data)
            : cast(REBYTE*, &s->content)
        );
}


//
// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `SER_HEAD(t, s)` is the same as `SER_AT(t, s, 0)`
//
// Use C-style cast instead of cast() macro, as it will always be safe and
// this is used very frequently.

#define SER_AT(t,s,i) \
    ((t*)SER_AT_RAW(sizeof(t), (s), (i)))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)

inline static REBYTE *SER_TAIL_RAW(size_t w, REBSER *s) {
    return SER_AT_RAW(w, s, SER_LEN(s));
}

#define SER_TAIL(t,s) \
    ((t*)SER_TAIL_RAW(sizeof(t), (s)))

inline static REBYTE *SER_LAST_RAW(size_t w, REBSER *s) {
    assert(SER_LEN(s) != 0);
    return SER_AT_RAW(w, s, SER_LEN(s) - 1);
}

#define SER_LAST(t,s) \
    ((t*)SER_LAST_RAW(sizeof(t), (s)))


#define SER_FULL(s) \
    (SER_LEN(s) + 1 >= SER_REST(s))

#define SER_AVAIL(s) \
    (SER_REST(s) - (SER_LEN(s) + 1)) // space available (minus terminator)

#define SER_FITS(s,n) \
    ((SER_LEN(s) + (n) + 1) <= SER_REST(s))


//
// Optimized expand when at tail (but, does not reterminate)
//

inline static void EXPAND_SERIES_TAIL(REBSER *s, REBCNT delta) {
    if (SER_FITS(s, delta))
        SET_SERIES_LEN(s, SER_LEN(s) + delta);
    else
        Expand_Series(s, SER_LEN(s), delta);
}

//
// Termination
//

inline static void TERM_SEQUENCE(REBSER *s) {
    assert(not IS_SER_ARRAY(s));
    memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}

inline static void TERM_SEQUENCE_LEN(REBSER *s, REBCNT len) {
    SET_SERIES_LEN(s, len);
    TERM_SEQUENCE(s);
}

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM(s) \
        NOOP
#else
    #define ASSERT_SERIES_TERM(s) \
        Assert_Series_Term_Core(s)
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define NOTE_SERIES_MAYBE_TERM(s) NOOP


//=////////////////////////////////////////////////////////////////////////=//
//
//  SERIES MANAGED MEMORY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a series is allocated by the Make_Ser() routine, it is not initially
// visible to the garbage collector.  To keep from leaking it, then it must
// be either freed with Free_Unmanaged_Series or delegated to the GC to manage
// with MANAGE_SERIES.
//
// (In debug builds, there is a test at the end of every Rebol function
// dispatch that checks to make sure one of those two things happened for any
// series allocated during the call.)
//
// The implementation of MANAGE_SERIES is shallow--it only sets a bit on that
// *one* series, not any series referenced by values inside of it.  This
// means that you cannot build a hierarchical structure that isn't visible
// to the GC and then do a single MANAGE_SERIES call on the root to hand it
// over to the garbage collector.  While it would be technically possible to
// deeply walk the structure, the efficiency gained from pre-building the
// structure with the managed bit set is significant...so that's how deep
// copies and the scanner/load do it.
//
// (In debug builds, if any unmanaged series are found inside of values
// reachable by the GC, it will raise an alert.)
//

inline static bool IS_SERIES_MANAGED(REBSER *s) {
    return did (s->header.bits & NODE_FLAG_MANAGED);
}

#define MANAGE_SERIES(s) \
    Manage_Series(s)

inline static void ENSURE_SERIES_MANAGED(REBSER *s) {
    if (not IS_SERIES_MANAGED(s))
        MANAGE_SERIES(s);
}

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s) \
        NOOP
#else
    inline static void ASSERT_SERIES_MANAGED(REBSER *s) {
        if (not IS_SERIES_MANAGED(s))
            panic (s);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES COLORING API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha re-used the same marking flag from the GC in order to do various
// other bit-twiddling tasks when the GC wasn't running.  This is an
// unusually dangerous thing to be doing...because leaving a stray mark on
// during some other traversal could lead the GC to think it had marked
// things reachable from that series when it had not--thus freeing something
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
// so this just goes with a series "color" of black or white, with white as
// the default.  The debug build keeps a count of how many black series there
// are and asserts it's 0 by the time each evaluation ends, to ensure balance.
//

static inline bool Is_Series_Black(REBSER *s) {
    return GET_SER_INFO(s, SERIES_INFO_BLACK);
}

static inline bool Is_Series_White(REBSER *s) {
    return NOT_SER_INFO(s, SERIES_INFO_BLACK);
}

static inline void Flip_Series_To_Black(REBSER *s) {
    assert(NOT_SER_INFO(s, SERIES_INFO_BLACK));
    SET_SER_INFO(s, SERIES_INFO_BLACK);
#if !defined(NDEBUG)
    ++TG_Num_Black_Series;
#endif
}

static inline void Flip_Series_To_White(REBSER *s) {
    assert(GET_SER_INFO(s, SERIES_INFO_BLACK));
    CLEAR_SER_INFO(s, SERIES_INFO_BLACK);
#if !defined(NDEBUG)
    --TG_Num_Black_Series;
#endif
}


//
// Freezing and Locking
//

inline static void Freeze_Sequence(REBSER *s) { // there is no unfreeze!
    assert(not IS_SER_ARRAY(s)); // use Deep_Freeze_Array
    SET_SER_INFO(s, SERIES_INFO_FROZEN);
}

inline static bool Is_Series_Frozen(REBSER *s) {
    assert(not IS_SER_ARRAY(s)); // use Is_Array_Deeply_Frozen
    return GET_SER_INFO(s, SERIES_INFO_FROZEN);
}

inline static bool Is_Series_Read_Only(REBSER *s) { // may be temporary...
    return ANY_SER_INFOS(
        s, SERIES_INFO_FROZEN | SERIES_INFO_HOLD | SERIES_INFO_PROTECTED
    );
}


// Gives the appropriate kind of error message for the reason the series is
// read only (frozen, running, protected, locked to be a map key...)
//
// !!! Should probably report if more than one form of locking is in effect,
// but if only one error is to be reported then this is probably the right
// priority ordering.
//

inline static void FAIL_IF_READ_ONLY_SER(REBSER *s) {
    if (not Is_Series_Read_Only(s))
        return;

    if (GET_SER_INFO(s, SERIES_INFO_AUTO_LOCKED))
        fail (Error_Series_Auto_Locked_Raw());

    if (GET_SER_INFO(s, SERIES_INFO_HOLD))
        fail (Error_Series_Held_Raw());

    if (GET_SER_INFO(s, SERIES_INFO_FROZEN))
        fail (Error_Series_Frozen_Raw());

    assert(GET_SER_INFO(s, SERIES_INFO_PROTECTED));
    fail (Error_Series_Protected_Raw());
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING SERIES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the evaluator runs (and also when
// ports are used).  So if a series has had MANAGE_SERIES run on it, the
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

#define PUSH_GC_GUARD(p) \
    Push_Guard_Node(NOD(p))

#ifdef NDEBUG
    inline static void Drop_Guard_Node(REBNOD *n) {
        UNUSED(n);
        GC_Guarded->content.dynamic.len--;
    }

    #define DROP_GC_GUARD(p) \
        Drop_Guard_Node(NOD(p))
#else
    inline static void Drop_Guard_Node_Debug(
        REBNOD *n,
        const char *file,
        int line
    ){
        if (n != *SER_LAST(REBNOD*, GC_Guarded))
            panic_at (n, file, line);
        GC_Guarded->content.dynamic.len--;
    }

    #define DROP_GC_GUARD(p) \
        Drop_Guard_Node_Debug(NOD(p), __FILE__, __LINE__)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

inline static REBSER *VAL_SERIES(const REBCEL *v) {
    assert(
        ANY_SERIES_KIND(CELL_KIND(v)) or ANY_PATH_KIND(CELL_KIND(v))
        or CELL_KIND(v) == REB_MAP
        or CELL_KIND(v) == REB_IMAGE
    ); // !!! Note: there was a problem here once, with a gcc 5.4 -O2 bug
    REBSER *s = v->payload.any_series.series;
    if (GET_SER_INFO(s, SERIES_INFO_INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return s;
}

inline static void INIT_VAL_SERIES(RELVAL *v, REBSER *s) {
    assert(not IS_SER_ARRAY(s));
    assert(IS_SERIES_MANAGED(s));
    v->payload.any_series.series = s;
}

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define VAL_INDEX(v) \
        ((v)->payload.any_series.index)
#else
    // allows an assert, but also lvalue: `VAL_INDEX(v) = xxx`
    //
    inline static REBCNT & VAL_INDEX(REBCEL *v) { // C++ reference type
        assert(ANY_SERIES_KIND(CELL_KIND(v)) or ANY_PATH_KIND(CELL_KIND(v)));
        return v->payload.any_series.index;
    }
    inline static REBCNT VAL_INDEX(const REBCEL *v) {
        if (ANY_PATH_KIND(CELL_KIND(v))) {
            assert(v->payload.any_series.index == 0);
            return 0;
        }
        assert(ANY_SERIES_KIND(CELL_KIND(v)));
        return v->payload.any_series.index;
    }
#endif

#define VAL_LEN_HEAD(v) \
    SER_LEN(VAL_SERIES(v))

inline static REBCNT VAL_LEN_AT(const REBCEL *v) {
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v))
        return 0; // avoid negative index
    return VAL_LEN_HEAD(v) - VAL_INDEX(v); // take current index into account
}

inline static REBYTE *VAL_RAW_DATA_AT(const REBCEL *v) {
    return SER_AT_RAW(SER_WIDE(VAL_SERIES(v)), VAL_SERIES(v), VAL_INDEX(v));
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

#define VAL_BITSET(v) \
    VAL_SERIES(v)

#define Init_Bitset(v,s) \
    Init_Any_Series((v), REB_BITSET, (s))


// Make a series of a given width (unit size).  The series will be zero
// length to start with, and will not have a dynamic data allocation.  This
// is a particularly efficient default state, so separating the dynamic
// allocation into a separate routine is not a huge cost.
//
inline static REBSER *Alloc_Series_Node(REBFLGS flags) {
    assert(not (flags & NODE_FLAG_CELL));

    REBSER *s = cast(REBSER*, Make_Node(SER_POOL));
    if ((GC_Ballast -= sizeof(REBSER)) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

    // Out of the 8 platform pointers that comprise a series node, only 3
    // actually need to be initialized to get a functional non-dynamic series
    // or array of length 0!  Two are set here, the third (info) should be
    // set by the caller.
    //
    s->header.bits = NODE_FLAG_NODE | flags | SERIES_FLAG_8_IS_TRUE; // #1
    TRASH_POINTER_IF_DEBUG(LINK(s).trash); // #2
  #if !defined(NDEBUG)
    memset(&s->content.fixed, 0xBD, sizeof(s->content)); // #3 - #6
    memset(&s->info, 0xAE, sizeof(s->info)); // #7, caller sets SER_WIDE()
  #endif
    TRASH_POINTER_IF_DEBUG(MISC(s).trash); // #8

    // Note: This series will not participate in management tracking!
    // See NODE_FLAG_MANAGED handling in Make_Arr_Core() and Make_Ser_Core().

  #if !defined(NDEBUG)
    TOUCH_SERIES_IF_DEBUG(s); // tag current C stack as series origin in ASAN
    PG_Reb_Stats->Series_Made++;
  #endif

    return s;
}


inline static REBCNT FIND_POOL(size_t size) {
  #if !defined(NDEBUG)
    if (PG_Always_Malloc)
        return SYSTEM_POOL;
  #endif

    if (size > 4 * MEM_BIG_SIZE)
        return SYSTEM_POOL;

    return PG_Pool_Map[size]; // ((4 * MEM_BIG_SIZE) + 1) entries
}


// Allocates element array for an already allocated REBSER node structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// SERIES_FLAG_FIXED_SIZE are left as they were, and other fields in the
// series structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.
//
inline static bool Did_Series_Data_Alloc(REBSER *s, REBCNT length) {
    //
    // Currently once a series becomes dynamic, it never goes back.  There is
    // no shrinking process that will pare it back to fit completely inside
    // the REBSER node.
    //
    assert(IS_SER_DYNAMIC(s)); // caller sets

    REBYTE wide = SER_WIDE(s);
    assert(wide != 0);

    REBCNT size; // size of allocation (possibly bigger than we need)

    REBCNT pool_num = FIND_POOL(length * wide);
    if (pool_num < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(char*, Make_Node(pool_num));
        if (not s->content.dynamic.data)
            return false;

        // The pooled allocation might wind up being larger than we asked.
        // Don't waste the space...mark as capacity the series could use.
        size = Mem_Pools[pool_num].wide;
        assert(size >= length * wide);

        // We don't round to power of 2 for allocations in memory pools
        CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
    }
    else {
        // ...the allocation is too big for a pool.  But instead of just
        // doing an unpooled allocation to give you the size you asked
        // for, the system does some second-guessing to align to 2Kb
        // boundaries (or choose a power of 2, if requested).

        size = length * wide;
        if (GET_SER_FLAG(s, SERIES_FLAG_POWER_OF_2)) {
            REBCNT len = 2048;
            while (len < size)
                len *= 2;
            size = len;

            // Clear the power of 2 flag if it isn't necessary, due to even
            // divisibility by the item width.
            //
            if (size % wide == 0)
                CLEAR_SER_FLAG(s, SERIES_FLAG_POWER_OF_2);
        }

        s->content.dynamic.data = ALLOC_N(char, size);
        if (not s->content.dynamic.data)
            return false;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

    // Note: Bias field may contain other flags at some point.  Because
    // SER_SET_BIAS() uses bit masking on an existing value, we are sure
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

    assert(SER_TOTAL(s) == size);
    return true;
}


// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
//
inline static REBSER *Make_Ser_Core(
    REBCNT capacity,
    REBYTE wide,
    REBFLGS flags
){
    assert(not (flags & ARRAY_FLAG_FILE_LINE));

    if (cast(REBU64, capacity) * wide > INT32_MAX)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

    // Non-array series nodes do not need their info bits to conform to the
    // rules of Endlike_Header(), so plain assignment can be used with a
    // non-zero second byte.  However, it obeys the fixed info bits for now.
    // (It technically doesn't need to.)
    //
    REBSER *s = Alloc_Series_Node(flags);
    s->info.bits =
        SERIES_INFO_0_IS_TRUE
        // not SERIES_INFO_1_IS_FALSE
        // not SERIES_INFO_7_IS_FALSE
        | FLAG_WIDE_BYTE_OR_0(wide);

    if (
        (flags & SERIES_FLAG_ALWAYS_DYNAMIC) // inlining will constant fold
        or (capacity * wide > sizeof(s->content))
    ){
        //
        // Data won't fit in a REBSER node, needs a dynamic allocation.  The
        // capacity given back as the ->rest may be larger than the requested
        // size, because the memory pool reports the full rounded allocation.

        mutable_LEN_BYTE_OR_255(s) = 255; // alloc caller sets
        if (not Did_Series_Data_Alloc(s, capacity))
            fail (Error_No_Memory(capacity * wide));

      #if !defined(NDEBUG)
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use PUSH_GC_GUARD().
    //
    // !!! Code duplicated in Make_Arr_Core() ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) {
        if (SER_FULL(GC_Manuals))
            Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s; // start out managed to not need to find/remove from this later
    }

    return s;
}

// !!! When series are made they are not terminated, which means that though
// they are empty they may not be "valid".  Should this be called Alloc_Ser()?
// Is Make_Ser() needed or are there few enough calls it should always take
// the flags and not have a _Core() variant?
//
#define Make_Ser(capacity, wide) \
    Make_Ser_Core((capacity), (wide), SERIES_FLAGS_NONE)
