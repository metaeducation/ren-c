//
//  File: %sys-series.h
//  Summary: {any-series! defs AFTER %tmp-internals.h (see: %sys-rebser.h)}
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
// Note: the word "Series" is overloaded in Rebol to refer to two related but
// distinct concepts:
//
// 1. The internal system datatype, also known as a REBSER.  It's a low-level
//    implementation of something similar to a vector or an array in other
//    languages.  It is an abstraction which represents a contiguous region
//    of memory containing equally-sized elements.
//
//   (For the struct definition of REBSER, see %sys-rebser.h)
//
// 2. The user-level value type ANY-SERIES!.  This might be more accurately
//    called ITERATOR!, because it includes both a pointer to a REBSER of
//    data and an index offset into that data.  Attempts to reconcile all
//    the naming issues from historical Rebol have not yielded a satisfying
//    alternative, so the ambiguity has stuck.
//
// An ANY-SERIES! value contains an `index` as the 0-based position into the
// series represented by this ANY-VALUE! (so if it is 0 then that means a
// Rebol index of 1).
//
// It is possible that the index could be to a point beyond the range of the
// series.  This is intrinsic, because the REBSER can be modified through
// other values and not update the others referring to it.  Hence VAL_INDEX()
// must be checked, or the routine called with it must.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Series subclasses REBARR, REBCTX, REBACT, REBMAP are defined which are
// type-incompatible with REBSER for safety.  (In C++ they would be derived
// classes, so common operations would not require casting...but it is seen
// as worthwhile to offer some protection even compiling as C.)  The
// subclasses are explained where they are defined in separate header files.
//
// Notes:
//
// * It is desirable to have series subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a REBSER* would actually be handled in the same
//   way for a REBARR*.  Plus, just because a REBCTX* is implemented as a
//   REBARR* with a link to another REBARR* doesn't mean most clients should
//   be accessing the array--in a C++ build this would mean it would have some
//   kind of protected inheritance scheme.
//
// * !!! It doesn't seem like index-out-of-range checks on the cells are being
//   done in a systemic way.  VAL_LEN_AT() bounds the length at the index
//   position by the physical length, but VAL_ARRAY_AT() doesn't check.
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
        REBSER *s = SER(p);  // allow REBARR, REBCTX, REBACT...

        // NOTE: When series are allocated, the only thing valid here is the
        // header.  Hence you can't tell (for instance) if it's an array or
        // not, as that's in the info.

      #if defined(DEBUG_SERIES_ORIGINS)
        #ifdef TO_WINDOWS
            //
            // The bug that %d-winstack.c was added for related to API handle
            // leakage.  So we only instrument the root series for now.  (The
            // stack tracking is rather slow if applied to all series, but
            // it is possible...just don't do this test.)
            //
            if (not IS_SER_DYNAMIC(s) and GET_SERIES_FLAG(s, ROOT))
                s->guard = cast(intptr_t*, Make_Winstack_Debug());
            else
                s->guard = nullptr;
        #else
            s->guard = cast(intptr_t*, malloc(sizeof(*s->guard)));
            free(s->guard);
        #endif
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
        SET_SERIES_INFO(p, MONITOR_DEBUG);
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

inline static REBLEN SER_USED(const REBSER *s) {
    REBYTE len_byte = LEN_BYTE_OR_255(s);
    return len_byte == 255 ? s->content.dynamic.used : len_byte;
}

inline static void SET_SERIES_USED(REBSER *s, REBLEN used) {
    if (LEN_BYTE_OR_255(s) == 255)
        s->content.dynamic.used = used;
    else {
        assert(used < sizeof(s->content));
        mutable_LEN_BYTE_OR_255(s) = used;
    }

  #if defined(DEBUG_UTF8_EVERYWHERE)
    //
    // Low-level series mechanics will manipulate the used field, but that's
    // at the byte level.  The higher level string mechanics must be used on
    // strings.
    //
    if (GET_SERIES_FLAG(s, IS_STRING)) {
        MISC(s).length = 0xDECAFBAD;
        TOUCH_SERIES_IF_DEBUG(s);
    }
  #endif
}

inline static void SET_SERIES_LEN(REBSER *s, REBLEN len) {
    assert(NOT_SERIES_FLAG(s, IS_STRING));  // use _LEN_SIZE
    SET_SERIES_USED(s, len);
}


// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
inline static REBYTE *SER_DATA(const_if_c REBSER *s) {
    // if updating, also update manual inlining in SER_AT_RAW

    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(NOT_SERIES_INFO(s, INACCESSIBLE));

    return LEN_BYTE_OR_255(s) == 255
        ? cast(REBYTE*, s->content.dynamic.data)
        : cast(REBYTE*, &s->content);
}

inline static REBYTE *SER_DATA_AT(REBYTE w, const_if_c REBSER *s, REBLEN i) {
  #if !defined(NDEBUG)
    if (w != SER_WIDE(s)) {  // will be "unusual" value if free
        if (IS_FREE_NODE(s))
            printf("SER_DATA_AT asked on freed series\n");
        else
            printf("SER_DATA_AT asked %d on width=%d\n", w, SER_WIDE(s));
        panic (s);
    }
  #endif

    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(not (s->info.bits & SERIES_INFO_INACCESSIBLE));

    return ((w) * (i)) + ( // v-- inlining of SER_DATA
        IS_SER_DYNAMIC(s)
            ? cast(REBYTE*, s->content.dynamic.data)
            : cast(REBYTE*, &s->content)
        );
}

#ifdef __cplusplus
    inline static const REBYTE *SER_DATA(const REBSER *s)  // "SER_DATA_HEAD"
      { return SER_DATA(m_cast(REBSER*, s)); }

    inline static const REBYTE *SER_DATA_AT(
        REBYTE w,
        const REBSER *s,
        REBLEN i
    ){
        return SER_DATA_AT(w, m_cast(REBSER*, s), i);
    }
#endif

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
    cast(t*, SER_DATA_AT(sizeof(t), (s), (i)))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)

inline static REBYTE *SER_DATA_TAIL(size_t w, const_if_c REBSER *s)
  { return SER_DATA_AT(w, s, SER_USED(s)); }

#ifdef __cplusplus
    inline static const REBYTE *SER_DATA_TAIL(size_t w, const REBSER *s)
      { return SER_DATA_AT(w, s, SER_USED(s)); }
#endif

#define SER_TAIL(t,s) \
    cast(t*, SER_DATA_TAIL(sizeof(t), (s)))

inline static REBYTE *SER_DATA_LAST(size_t wide, const_if_c REBSER *s) {
    assert(SER_USED(s) != 0);
    return SER_DATA_AT(wide, s, SER_USED(s) - 1);
}

#ifdef __cplusplus
    inline static const REBYTE *SER_DATA_LAST(size_t wide, const REBSER *s) {
        assert(SER_USED(s) != 0);
        return SER_DATA_AT(wide, s, SER_USED(s) - 1);
    }
#endif

#define SER_LAST(t,s) \
    cast(t*, SER_DATA_LAST(sizeof(t), (s)))


#define SER_FULL(s) \
    (SER_USED(s) + 1 >= SER_REST(s))

#define SER_AVAIL(s) \
    (SER_REST(s) - (SER_USED(s) + 1)) // space available (minus terminator)

#define SER_FITS(s,n) \
    ((SER_USED(s) + (n) + 1) <= SER_REST(s))


//
// Optimized expand when at tail (but, does not reterminate)
//

inline static void EXPAND_SERIES_TAIL(REBSER *s, REBLEN delta) {
    if (SER_FITS(s, delta))
        SET_SERIES_USED(s, SER_USED(s) + delta);  // no termination implied
    else
        Expand_Series(s, SER_USED(s), delta);  // currently terminates

    // !!! R3-Alpha had a premise of not terminating arrays when it did not
    // have to, but the invariants of when termination happened was unclear.
    // Ren-C has tried to ferret out the places where termination was and
    // wasn't happening via asserts and address sanitizer; while not "over
    // terminating" redundantly.  To try and make it clear this does not
    // terminate, we poison even if it calls into Expand_Series, which
    // *does* terminate.
    //
  #if !defined(NDEBUG)
    if (IS_SER_ARRAY(s)) {  // trash to ensure termination (if not implicit)
        RELVAL *tail = SER_TAIL(RELVAL, s);
        if (
            (tail->header.bits & NODE_FLAG_CELL)
            and not (
                IS_END(tail)
                and (tail->header.bits & CELL_FLAG_PROTECTED)
            )
        ){
            mutable_SECOND_BYTE(tail->header.bits) = REB_T_TRASH;
        }
    }
    else if (SER_WIDE(s) == 1)  // presume BINARY! or ANY-STRING! (?)
        *SER_DATA_TAIL(1, s) = 0xFE;  // invalid UTF-8 byte, e.g. poisonous
    else {
        // Assume other series (like GC_Mark_Stack) don't necessarily
        // terminate.
    }
  #endif
}

//
// Termination
//

inline static void TERM_SEQUENCE(REBSER *s) {
    assert(not IS_SER_ARRAY(s));
    memset(SER_DATA_AT(SER_WIDE(s), s, SER_USED(s)), 0, SER_WIDE(s));
}

inline static void TERM_SEQUENCE_LEN(REBSER *s, REBLEN len) {
    SET_SERIES_LEN(s, len);
    TERM_SEQUENCE(s);
}

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM(s) \
        NOOP

    #define ASSERT_SERIES_TERM_IF_NEEDED(s) \
        NOOP
#else
    #define ASSERT_SERIES_TERM(s) \
        Assert_Series_Term_Core(s)

    inline static void ASSERT_SERIES_TERM_IF_NEEDED(REBSER *s) {
        if (
            IS_SER_ARRAY(s)
            or (
                SER_WIDE(s) == 1
                and s != TG_Byte_Buf
                and s != SER(TG_Mold_Buf)
            )
        ){
            Assert_Series_Term_Core(s);
        }
    }
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define NOTE_SERIES_MAYBE_TERM(s) NOOP


//=//// SERIES MANAGED MEMORY /////////////////////////////////////////////=//
//
// If NODE_FLAG_MANAGED is not explicitly passed to Make_Series_Core, a
// series will be manually memory-managed by default.  Hence you don't need
// to worry about the series being freed out from under you while building it.
// Manual series are tracked, and automatically freed in the case of a fail().
//
// All manual series *must* either be freed with Free_Unmanaged_Series() or
// delegated to the GC with Manage_Series() before the frame ends.  Once a
// series is managed, only the GC is allowed to free it.
//
// Manage_Series() is shallow--it only sets a bit on that *one* series, not
// any series referenced by values inside of it.  Hence many routines that
// build hierarchical structures (like the scanner) only return managed
// results, since they can manage it as they build them.

inline static void Untrack_Manual_Series(REBSER *s)
{
    REBSER ** const last_ptr
        = &cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.used - 1
        ];

    assert(GC_Manuals->content.dynamic.used >= 1);
    if (*last_ptr != s) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        REBSER **current_ptr = last_ptr - 1;
        for (; *current_ptr != s; --current_ptr) {
          #if !defined(NDEBUG)
            if (
                current_ptr
                <= cast(REBSER**, GC_Manuals->content.dynamic.data)
            ){
                printf("Series not in list of last manually added series\n");
                panic(s);
            }
          #endif
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should GC_Manuals ever shrink or save memory?
    //
    --GC_Manuals->content.dynamic.used;
}

inline static REBSER *Manage_Series(REBSER *s)  // give manual series to GC
{
  #if !defined(NDEBUG)
    if (GET_SERIES_FLAG(s, MANAGED))
        panic (s);  // shouldn't manage an already managed series
  #endif

    s->header.bits |= NODE_FLAG_MANAGED;
    Untrack_Manual_Series(s);
    return s;
}

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s)
#else
    inline static void ASSERT_SERIES_MANAGED(const void *s) {
        if (NOT_SERIES_FLAG(s, MANAGED))
            panic (s);
    }
#endif

inline static REBSER *Force_Series_Managed(const_if_c void *p) {
    REBSER *s = m_cast(REBSER*, SER(p));
    if (NOT_SERIES_FLAG(s, MANAGED))
        Manage_Series(s);
    return s;
}

#if !defined(__cplusplus)
    #define Force_Series_Managed_Core Force_Series_Managed
#else
    inline static REBSER *Force_Series_Managed_Core(void *p)
      { return Force_Series_Managed(p); }  // mutable series may be unmanaged

    inline static REBSER *Force_Series_Managed_Core(const void *p) {
        const REBSER *s = SER(p);
        ASSERT_SERIES_MANAGED(s);  // const series should already be managed
        return m_cast(REBSER*, s);
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

static inline bool Is_Series_Black(const REBSER *s) {
    return GET_SERIES_INFO(s, BLACK);
}

static inline bool Is_Series_White(const REBSER *s) {
    return NOT_SERIES_INFO(s, BLACK);
}

static inline void Flip_Series_To_Black(const REBSER *s) {
    assert(NOT_SERIES_INFO(s, BLACK));
    SET_SERIES_INFO(s, BLACK);
#if !defined(NDEBUG)
    ++TG_Num_Black_Series;
#endif
}

static inline void Flip_Series_To_White(const REBSER *s) {
    assert(GET_SERIES_INFO(s, BLACK));
    CLEAR_SERIES_INFO(s, BLACK);
#if !defined(NDEBUG)
    --TG_Num_Black_Series;
#endif
}


//
// Freezing and Locking
//

inline static void Freeze_Series(const REBSER *s) {  // there is no unfreeze
    assert(not IS_SER_ARRAY(s)); // use Deep_Freeze_Array
    SET_SERIES_INFO(s, FROZEN_SHALLOW);
    SET_SERIES_INFO(s, FROZEN_DEEP);  // so generic deep frozen checks faster
}

inline static bool Is_Series_Frozen(const REBSER *s) {
    assert(not IS_SER_ARRAY(s));  // use Is_Array_Deeply_Frozen
    if (NOT_SERIES_INFO(s, FROZEN_SHALLOW))
        return false;
    assert(GET_SERIES_INFO(s, FROZEN_DEEP));  // true on frozen non-arrays
    return true;
}

inline static bool Is_Series_Read_Only(const REBSER *s) {  // may be temporary
    return 0 != (s->info.bits &
        (SERIES_INFO_HOLD | SERIES_INFO_PROTECTED
        | SERIES_INFO_FROZEN_SHALLOW | SERIES_INFO_FROZEN_DEEP)
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

    if (GET_SERIES_INFO(s, AUTO_LOCKED))
        fail (Error_Series_Auto_Locked_Raw());

    if (GET_SERIES_INFO(s, HOLD))
        fail (Error_Series_Held_Raw());

    if (GET_SERIES_INFO(s, FROZEN_SHALLOW))
        fail (Error_Series_Frozen_Raw());

    assert(NOT_SERIES_INFO(s, FROZEN_DEEP));  // implies FROZEN_SHALLOW

    assert(GET_SERIES_INFO(s, PROTECTED));
    fail (Error_Series_Protected_Raw());
}

// While ideally error messages would give back data that is bound exactly to
// the context that was applicable, threading the specifier into many cases
// can overcomplicate code.  We'd break too many invariants to just say a
// relativized value is "unbound", so make an expired frame if necessary.
//
inline static REBVAL* Unrelativize(RELVAL* out, const RELVAL* v) {
    if (not Is_Bindable(v) or IS_SPECIFIC(v))
        Move_Value(out, SPECIFIC(v));
    else {  // must be bound to a function
        REBACT *binding = ACT(VAL_BINDING(v));
        REBCTX *expired = Make_Expired_Frame_Ctx_Managed(binding);

        Move_Value_Header(out, v);
        out->payload = v->payload;
        EXTRA(Binding, out).node = NOD(expired);
    }
    return SPECIFIC(out);
}

// This is a super lazy version of unrelativization, which can be used to
// hand a relative value to something like fail(), since fail will clean up
// the stray alloc.
//
#define rebUnrelativize(v) \
    Unrelativize(Alloc_Value(), (v))


#if defined(NDEBUG)
    #define KNOWN_MUTABLE(v) v
#else
    inline static const RELVAL* KNOWN_MUTABLE(const RELVAL* v) {
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        REBSER *s = SER(VAL_NODE(v));  // can be pairlist, varlist, etc.
        assert(not Is_Series_Read_Only(s));
        assert(NOT_CELL_FLAG(v, CONST));
        return v;
    }
#endif

inline static const RELVAL *ENSURE_MUTABLE(const RELVAL *v) {
    assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
    REBSER *s = SER(VAL_NODE(v));  // can be pairlist, varlist, etc.

    FAIL_IF_READ_ONLY_SER(s);

    if (NOT_CELL_FLAG(v, CONST))
        return v;

    DECLARE_LOCAL (specific);
    Unrelativize(specific, v);  // relative values lose binding in error object
    fail (Error_Const_Value_Raw(specific));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING SERIES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the evaluator runs (and also when
// ports are used).  So if a series has had Manage_Series() run on it, the
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

inline static void DROP_GC_GUARD(const void *p) {
  #if defined(NDEBUG)
    UNUSED(p);
  #else
    if (NOD(p) != *SER_LAST(REBNOD*, GC_Guarded)) {
        printf("DROP_GC_GUARD() pointer that wasn't last PUSH_GC_GUARD()\n");
        panic (p);  // should show current call stack AND where node allocated
    }
  #endif

    --GC_Guarded->content.dynamic.used;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

// Uses "evil macro" variations because it is called so frequently, that in
// the debug build (which doesn't inline functions) there's a notable cost.
//
inline static const REBSER *VAL_SERIES(REBCEL(const*) v) {
  #if !defined(NDEBUG)
    enum Reb_Kind k = CELL_KIND(v);
    assert(ANY_SERIES_KIND_EVIL_MACRO);
  #endif
    const REBSER *s = SER(PAYLOAD(Any, v).first.node);
    if (GET_SERIES_INFO(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return s;
}

#define VAL_SERIES_ENSURE_MUTABLE(v) \
    m_cast(REBSER*, VAL_SERIES(ENSURE_MUTABLE(v)))

#define VAL_SERIES_KNOWN_MUTABLE(v) \
    m_cast(REBSER*, VAL_SERIES(KNOWN_MUTABLE(v)))


#define VAL_INDEX_UNCHECKED(v) \
    PAYLOAD(Any, (v)).second.u32

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define VAL_INDEX(v) \
        VAL_INDEX_UNCHECKED(v)
#else
    // allows an assert, but also lvalue: `VAL_INDEX(v) = xxx`
    //
    // uses "evil macro" variants because the cost of this basic operation
    // becomes prohibitive when the functions aren't inlined and checks wind
    // up getting done 
    //
    inline static REBLEN VAL_INDEX(REBCEL(const*) v) { // C++ reference type
        enum Reb_Kind k = CELL_KIND(v);
        assert(ANY_SERIES_KIND_EVIL_MACRO);
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        return VAL_INDEX_UNCHECKED(v);
    }
    inline static REBLEN & VAL_INDEX(RELVAL *v) {
        enum Reb_Kind k = VAL_TYPE(v);
        assert(k == REB_ISSUE or ANY_SERIES_KIND_EVIL_MACRO);
        assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
        return VAL_INDEX_UNCHECKED(v);
    }
#endif


inline static const REBYTE *VAL_DATA_AT(REBCEL(const*) v) {
    return SER_DATA_AT(SER_WIDE(VAL_SERIES(v)), VAL_SERIES(v), VAL_INDEX(v));
}

#define Init_Any_Series_At(v,t,s,i) \
    Init_Any_Series_At_Core((v), (t), \
        Force_Series_Managed_Core(s), (i), UNBOUND)

#define Init_Any_Series(v,t,s) \
    Init_Any_Series_At((v), (t), (s), 0)


// Make a series of a given width (unit size).  The series will be zero
// length to start with, and will not have a dynamic data allocation.  This
// is a particularly efficient default state, so separating the dynamic
// allocation into a separate routine is not a huge cost.
//
// Note: This series will not participate in management tracking!
// See NODE_FLAG_MANAGED handling in Make_Array_Core() and Make_Series_Core().
//
inline static REBSER *Alloc_Series_Node(REBFLGS flags) {
    assert(not (flags & NODE_FLAG_CELL));

    REBSER *s = cast(REBSER*, Alloc_Node(SER_POOL));
    if ((GC_Ballast -= sizeof(REBSER)) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

    // Out of the 8 platform pointers that comprise a series node, only 3
    // actually need to be initialized to get a functional non-dynamic series
    // or array of length 0!  Only one is set here.  The info should be
    // set by the caller, as should a terminator in the internal payload

    s->header.bits = NODE_FLAG_NODE | flags | SERIES_FLAG_8_IS_TRUE;  // #1

  #if !defined(NDEBUG)
    SAFETRASH_POINTER_IF_DEBUG(s->link_private.trash);  // #2
    memset(  // https://stackoverflow.com/q/57721104/
        cast(char*, &s->content.fixed),
        0xBD,
        sizeof(s->content)
    );  // #3 - #6
    memset(&s->info, 0xAE, sizeof(s->info));  // #7, caller sets SER_WIDE()
    SAFETRASH_POINTER_IF_DEBUG(s->link_private.trash);  // #8

    TOUCH_SERIES_IF_DEBUG(s);  // tag current C stack as series origin in ASAN
    PG_Reb_Stats->Series_Made++;
  #endif

    return s;
}


inline static REBLEN FIND_POOL(size_t size) {
  #ifdef DEBUG_ENABLE_ALWAYS_MALLOC
    if (PG_Always_Malloc)
        return SYSTEM_POOL;
  #endif

    // Using a simple > or < check here triggers Spectre Mitigation warnings
    // in MSVC, while the division does not.  :-/  Hopefully the compiler is
    // smart enough to figure out how to do this efficiently in any case.

    if (size / (4 * MEM_BIG_SIZE + 1) == 0)
        return PG_Pool_Map[size]; // ((4 * MEM_BIG_SIZE) + 1) entries

    return SYSTEM_POOL;
}


// Allocates element array for an already allocated REBSER node structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// SERIES_FLAG_FIXED_SIZE are left as they were, and other fields in the
// series structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.
//
inline static bool Did_Series_Data_Alloc(REBSER *s, REBLEN capacity) {
    //
    // Currently once a series becomes dynamic, it never goes back.  There is
    // no shrinking process that will pare it back to fit completely inside
    // the REBSER node.
    //
    assert(IS_SER_DYNAMIC(s)); // caller sets

    REBYTE wide = SER_WIDE(s);
    assert(wide != 0);

    if (cast(REBU64, capacity) * wide > INT32_MAX)  // R3-Alpha said "too big"
        return false;

    REBSIZ size; // size of allocation (possibly bigger than we need)

    REBLEN pool_num = FIND_POOL(capacity * wide);
    if (pool_num < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(char*, Try_Alloc_Node(pool_num));
        if (not s->content.dynamic.data)
            return false;

        // The pooled allocation might wind up being larger than we asked.
        // Don't waste the space...mark as capacity the series could use.
        size = Mem_Pools[pool_num].wide;
        assert(size >= capacity * wide);

        // We don't round to power of 2 for allocations in memory pools
        CLEAR_SERIES_FLAG(s, POWER_OF_2);
    }
    else {
        // ...the allocation is too big for a pool.  But instead of just
        // doing an unpooled allocation to give you the size you asked
        // for, the system does some second-guessing to align to 2Kb
        // boundaries (or choose a power of 2, if requested).

        size = capacity * wide;
        if (GET_SERIES_FLAG(s, POWER_OF_2)) {
            REBSIZ size2 = 2048;
            while (size2 < size)
                size2 *= 2;
            size = size2;

            // Clear the power of 2 flag if it isn't necessary, due to even
            // divisibility by the item width.
            //
            if (size % wide == 0)
                CLEAR_SERIES_FLAG(s, POWER_OF_2);
        }

        s->content.dynamic.data = TRY_ALLOC_N(char, size);
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
    s->content.dynamic.used = 0;

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
inline static REBSER *Make_Series_Core(
    REBLEN capacity,
    REBYTE wide,
    REBFLGS flags
){
    assert(not (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED));

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
        (flags & SERIES_FLAG_ALWAYS_DYNAMIC)  // inlining will constant fold
        or (capacity * wide > sizeof(s->content))
    ){
        // Data won't fit in a REBSER node, needs a dynamic allocation.  The
        // capacity given back as the ->rest may be larger than the requested
        // size, because the memory pool reports the full rounded allocation.

        mutable_LEN_BYTE_OR_255(s) = 255;  // dynamic

        if (not Did_Series_Data_Alloc(s, capacity)) {
            s->header.bits &= ~NODE_FLAG_MANAGED;
            s->info.bits |= SERIES_INFO_INACCESSIBLE;
            GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE

            fail (Error_No_Memory(capacity * wide));
        }

      #if !defined(NDEBUG)
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use PUSH_GC_GUARD().
    //
    // !!! Code duplicated in Make_Array_Core() ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) {
        if (SER_FULL(GC_Manuals))
            Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.used++
        ] = s; // start out managed to not need to find/remove from this later
    }

    return s;
}

// !!! When series are made they are not terminated, which means that though
// they are empty they may not be "valid".  Should this be called Alloc_Ser()?
// Is Make_Series() needed or are there few enough calls it should always take
// the flags and not have a _Core() variant?
//
#define Make_Series(capacity, wide) \
    Make_Series_Core((capacity), (wide), SERIES_FLAGS_NONE)


enum act_modify_mask {
    AM_PART = 1 << 0,
    AM_SPLICE = 1 << 1,
    AM_LINE = 1 << 2
};

enum act_find_mask {
    AM_FIND_ONLY = 1 << 0,
    AM_FIND_CASE = 1 << 1,
    AM_FIND_MATCH = 1 << 2
};
