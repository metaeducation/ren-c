//
//  File: %sys-series.h
//  Summary: {Definitions for Series (REBSER) plus Array, Frame, and Map}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// collector.  Free_Series() may only be called on manual series.  See
// MANAGE_SERIES() and PUSH_GUARD_SERIES() for remarks on how to work safely
// with pointers to garbage-collected series, to avoid having them be GC'd
// out from under the code while working with them.
//
// This file defines series subclasses which are type-incompatible with
// REBSER for safety.  (In C++ they would be derived classes, so common
// operations would not require casting...but this is C.)  The subclasses
// are explained where they are defined.
//
// Notes:
//
// * For the struct definition of REBSER, see %sys-rebser.h
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DEBUG PANIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// "Series Panics" will (hopefully) trigger an alert under memory tools
// like address sanitizer and valgrind that indicate the call stack at the
// moment of allocation of a series.  Then you should have TWO stacks: the
// one at the call of the Panic, and one where that series was alloc'd.
//

#if !defined(NDEBUG)
    #define Panic_Series(s) \
        Panic_Series_Debug((s), __FILE__, __LINE__);
#endif


//
// Series flags
//

#define SET_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits |= cast(REBUPT, f)))

#define CLEAR_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits &= ~cast(REBUPT, f)))

#define GET_SER_FLAG(s,f) \
    LOGICAL((s)->info.bits & (f))

#define SET_SER_FLAGS(s,f) \
    SET_SER_FLAG((s), (f))

#define CLEAR_SER_FLAGS(s,f) \
    CLEAR_SER_FLAG((s), (f))


//
// The mechanics of the macros that get or set the length of a series are a
// little bit complicated.  This is due to the optimization that allows data
// which is sizeof(REBVAL) or smaller to fit directly inside the series node.
//
// If a series is not "dynamic" (e.g. has a full pooled allocation) then its
// length is stored in the header...where the "type" bits would be if it
// were a REBVAL.  But if a series is dynamically allocated out of the memory
// pools, then without the data itself taking up the "content", there's room
// for a length in the node.
//

#define SER_WIDE(s) \
    ((REBYTE)((s)->info.bits >> 16) & 0xff) // no use to inline in debug build

inline static REBCNT SER_LEN(REBSER *s) {
    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC))
        return s->content.dynamic.len;

    // Length is stored in the header if it is dynamic, in what would be the
    // "type" bits were it a value.  The same optimization is available in
    // that it can just be shifted out.

    return RIGHT_N_BITS(s->header.bits, NUM_KIND_BITS); // !!! NUM_LEN_BITS
}

inline static void SET_SERIES_LEN(REBSER *s, REBCNT len) {
    assert(!GET_SER_FLAG(s, CONTEXT_FLAG_STACK));

    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        s->content.dynamic.len = len;
    }
    else {
        assert(len < sizeof(s->content));
        CLEAR_N_RIGHT_BITS(s->header.bits, NUM_KIND_BITS); // !!! NUM_LEN_BITS
        s->header.bits |= FLAGVAL_RIGHT(len);
        assert(SER_LEN(s) == len);
    }
}

inline static REBCNT SER_REST(REBSER *s) {
    if (GET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC))
        return s->content.dynamic.rest;

    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
        return 2; // includes info bits acting as trick "terminator"

    assert(sizeof(s->content) % SER_WIDE(s) == 0);
    return sizeof(s->content) / SER_WIDE(s);
}

// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
inline static REBYTE *SER_DATA_RAW(REBSER *s) {
    // if updating, also update manual inlining in SER_AT_RAW
    return GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)
        ? s->content.dynamic.data
        : cast(REBYTE*, &s->content);
}

inline static REBYTE *SER_AT_RAW(size_t w, REBSER *s, REBCNT i) {
#if !defined(NDEBUG)
    if (w != SER_WIDE(s)) {
        //
        // This is usually a sign that the series was GC'd, as opposed to the
        // caller passing in the wrong width (freeing sets width to 0).  But
        // give some debug tracking either way.
        //
        Debug_Fmt("SER_AT_RAW asked %d on width=%d", w, SER_WIDE(s));
        Panic_Series(s);
    }
#endif

    return ((w) * (i)) + ( // v-- inlining of SER_DATA_RAW
        GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)
            ? s->content.dynamic.data
            : cast(REBYTE*, &s->content)
        );
}

inline static void SER_SET_EXTERNAL_DATA(REBSER *s, void *p) {
    SET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);
    s->content.dynamic.data = cast(REBYTE*, p);
}


//
// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `SER_HEAD(t, s)` is the same as `SER_AT(t, s, 0)`
//

#define SER_AT(t,s,i) \
    cast(t*, SER_AT_RAW(sizeof(t), (s), (i)))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)

inline static REBYTE *SER_TAIL_RAW(size_t w, REBSER *s) {
    return SER_AT_RAW(w, s, SER_LEN(s));
}

#define SER_TAIL(t,s) \
    cast(t*, SER_TAIL_RAW(sizeof(t), (s)))

inline static REBYTE *SER_LAST_RAW(size_t w, REBSER *s) {
    assert(SER_LEN(s) != 0);
    return SER_AT_RAW(w, s, SER_LEN(s) - 1);
}

#define SER_LAST(t,s) \
    cast(t*, SER_LAST_RAW(sizeof(t), (s)))


#define SER_FULL(s) \
    (SER_LEN(s) + 1 >= SER_REST(s))

#define SER_AVAIL(s) \
    (SER_REST(s) - (SER_LEN(s) + 1)) // space available (minus terminator)

#define SER_FITS(s,n) \
    ((SER_LEN(s) + (n) + 1) <= SER_REST(s))

#define Is_Array_Series(s) \
    GET_SER_FLAG((s), SERIES_FLAG_ARRAY)

inline static void FAIL_IF_LOCKED_SERIES(REBSER *s) {
    if (GET_SER_FLAG(s, SERIES_FLAG_LOCKED))
        fail (Error(RE_LOCKED));
}

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
    assert(!Is_Array_Series(s));
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
// When a series is allocated by the Make_Series routine, it is not initially
// visible to the garbage collector.  To keep from leaking it, then it must
// be either freed with Free_Series or delegated to the GC to manage with
// MANAGE_SERIES.
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

inline static REBOOL IS_SERIES_MANAGED(REBSER *s) {
    return LOGICAL(s->header.bits & REBSER_REBVAL_FLAG_MANAGED);
}

#define MANAGE_SERIES(s) \
    Manage_Series(s)

inline static void ENSURE_SERIES_MANAGED(REBSER *s) {
    if (NOT(IS_SERIES_MANAGED(s)))
        MANAGE_SERIES(s);
}

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s) \
        NOOP

    #define ASSERT_VALUE_MANAGED(v) \
        NOOP
#else
    inline static void ASSERT_SERIES_MANAGED(REBSER *s) {
        if (NOT(IS_SERIES_MANAGED(s)))
            Panic_Series(s);
    }

    #define ASSERT_VALUE_MANAGED(v) \
        assert(Is_Value_Managed(v))
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

static inline REBOOL Is_Series_Black(REBSER *s) {
    return LOGICAL(s->header.bits & REBSER_FLAG_BLACK);
}

static inline REBOOL Is_Series_White(REBSER *s) {
    return NOT(s->header.bits & REBSER_FLAG_BLACK);
}

static inline void Flip_Series_To_Black(REBSER *s) {
    assert(NOT(s->header.bits & REBSER_FLAG_BLACK));
    s->header.bits |= REBSER_FLAG_BLACK;
#if !defined(NDEBUG)
    ++TG_Num_Black_Series;
#endif
}

static inline void Flip_Series_To_White(REBSER *s) {
    assert(s->header.bits & REBSER_FLAG_BLACK);
    s->header.bits &= ~cast(REBUPT, REBSER_FLAG_BLACK);
#if !defined(NDEBUG)
    --TG_Num_Black_Series;
#endif
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
// Also: Some REBVALs contain one or more series that need to be guarded.
// PUSH_GUARD_VALUE() makes it possible to not worry about what series are in
// a value, as it will take care of it if there are any.  As with series
// guarding, the last value guarded must be the first one you DROP_GUARD on.
//

inline static void PUSH_GUARD_SERIES(REBSER *s) {
    ASSERT_SERIES_MANAGED(s); // see PUSH_GUARD_ARRAY_CONTENTS if you need it
    Guard_Series_Core(s);
}

inline static void DROP_GUARD_SERIES(REBSER *s) {
    assert(GET_SER_FLAG(GC_Series_Guard, SERIES_FLAG_HAS_DYNAMIC));
    assert(s == *SER_LAST(REBSER*, GC_Series_Guard));
    GC_Series_Guard->content.dynamic.len--;
}

#define PUSH_GUARD_VALUE(v) \
    Guard_Value_Core(v)

inline static void DROP_GUARD_VALUE(RELVAL *v) {
    assert(GET_SER_FLAG(GC_Value_Guard, SERIES_FLAG_HAS_DYNAMIC));
    assert(v == *SER_LAST(RELVAL*, GC_Value_Guard));
    GC_Value_Guard->content.dynamic.len--;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

inline static REBSER *VAL_SERIES(const RELVAL *v) {
    assert(ANY_SERIES(v) || IS_MAP(v) || IS_VECTOR(v) || IS_IMAGE(v));
    return v->payload.any_series.series;
}

inline static void INIT_VAL_SERIES(RELVAL *v, REBSER *s) {
    assert(!Is_Array_Series(s));
    v->payload.any_series.series = s;
}

#define VAL_INDEX(v) \
    ((v)->payload.any_series.index)

#define VAL_LEN_HEAD(v) \
    SER_LEN(VAL_SERIES(v))

inline static REBCNT VAL_LEN_AT(const RELVAL *v) {
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v))
        return 0; // avoid negative index
    return VAL_LEN_HEAD(v) - VAL_INDEX(v); // take current index into account
}

inline static REBYTE *VAL_RAW_DATA_AT(const RELVAL *v) {
    return SER_AT_RAW(SER_WIDE(VAL_SERIES(v)), VAL_SERIES(v), VAL_INDEX(v));
}

#define Val_Init_Series_Index(v,t,s,i) \
    Val_Init_Series_Index_Core(SINK(v), (t), (s), (i), SPECIFIED)

#define Val_Init_Series(v,t,s) \
    Val_Init_Series_Index((v), (t), (s), 0)


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

#define Val_Init_Bitset(v,s) \
    Val_Init_Series((v), REB_BITSET, (s))
