//
//  File: %sys-array.h
//  Summary: {Definitions for REBARR}
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
// A "Rebol Array" is a series of REBVAL values which is terminated by an
// END marker.  In R3-Alpha, the END marker was itself a full-sized REBVAL
// cell...so code was allowed to write one cell past the capacity requested
// when Make_Array() was called.  But this always had to be an END.
//
// In Ren-C, there is an implicit END marker just past the last cell in the
// capacity.  Allowing a SET_END() on this position could corrupt the END
// signaling slot, which only uses a bit out of a Reb_Header sized item to
// signal.  Use TERM_ARRAY_LEN() to safely terminate arrays and respect not
// writing if it's past capacity.
//
// While many operations are shared in common with REBSER, there is a
// (deliberate) type incompatibility introduced.  The type compatibility is
// implemented in a way that works in C or C++ (though it should be reviewed
// for strict aliasing compliance).  To get the underlying REBSER of a REBARR
// use the SER() operation.
//
// An ARRAY is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).
//


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  An
// empty array should have an END marker in its head slot, and since it has
// no last value then ARR_LAST should not be called (this is checked in
// debug builds).  A fully constructed array should always have an END
// marker in its tail slot, which is one past the last position that is
// valid for writing a full REBVAL.

inline static RELVAL *ARR_AT(const_if_c REBARR *a, REBLEN n)
  { return SER_AT(RELVAL, SER(a), n); }

inline static RELVAL *ARR_HEAD(const_if_c REBARR *a)
  { return SER_HEAD(RELVAL, SER(a)); }

inline static RELVAL *ARR_TAIL(const_if_c REBARR *a)
  { return SER_TAIL(RELVAL, SER(a)); }

inline static RELVAL *ARR_LAST(const_if_c REBARR *a)
  { return SER_LAST(RELVAL, SER(a)); }

inline static RELVAL *ARR_SINGLE(const_if_c REBARR *a) {
    assert(not IS_SER_DYNAMIC(a));
    return cast(RELVAL*, &SER(a)->content.fixed);
}

#ifdef __cplusplus
    inline static const RELVAL *ARR_AT(const REBARR *a, REBLEN n)
        { return SER_AT(const RELVAL, SER(a), n); }

    inline static const RELVAL *ARR_HEAD(const REBARR *a)
        { return SER_HEAD(const RELVAL, SER(a)); }

    inline static const RELVAL *ARR_TAIL(const REBARR *a)
        { return SER_TAIL(const RELVAL, SER(a)); }

    inline static const RELVAL *ARR_LAST(const REBARR *a)
        { return SER_LAST(const RELVAL, SER(a)); }

    inline static const RELVAL *ARR_SINGLE(const REBARR *a) {
        assert(not IS_SER_DYNAMIC(a));
        return cast(const RELVAL*, &SER(a)->content.fixed);
    }
#endif


// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
inline static REBARR *Singular_From_Cell(const RELVAL *v) {
    REBARR *singular = ARR(  // some checking in debug builds is done by ARR()
        cast(void*,
            cast(REBYTE*, m_cast(RELVAL*, v))
            - offsetof(struct Reb_Series, content)
        )
    );
    assert(not IS_SER_DYNAMIC(singular));
    return singular;
}

// As with an ordinary REBSER, a REBARR has separate management of its length
// and its terminator.  Many routines seek to choose the precise moment to
// sync these independently for performance reasons (for better or worse).
//
#define ARR_LEN(a) \
    SER_USED(SER(a))


// Set length and also terminate.  This routine avoids conditionality in the
// release build, which means it may overwrite a signal byte in a "read-only"
// end (such as an Endlike_Header).  Not branching is presumed to perform
// better, but cells that weren't ends already are writability checked.
//
// !!! Review if SERIES_FLAG_FIXED_SIZE should be calling this routine.  At
// the moment, fixed size series merely can't expand, but it might be more
// efficient if they didn't use any "appending" operators to get built.
//
inline static void TERM_ARRAY_LEN(REBARR *a, REBLEN len) {
    assert(len < SER_REST(SER(a)));
    SET_SERIES_LEN(SER(a), len);

  #if !defined(NDEBUG)
    if (NOT_END(ARR_AT(a, len)))
        ASSERT_CELL_WRITABLE_EVIL_MACRO(ARR_AT(a, len), __FILE__, __LINE__);
  #endif
    mutable_SECOND_BYTE(ARR_AT(a, len)->header.bits) = REB_0_END;
}

inline static void SET_ARRAY_LEN_NOTERM(REBARR *a, REBLEN len) {
    SET_SERIES_LEN(SER(a), len); // call out non-terminating usages
}

inline static void RESET_ARRAY(REBARR *a) {
    TERM_ARRAY_LEN(a, 0);
}

inline static void TERM_SERIES(REBSER *s) {
    if (IS_SER_ARRAY(s))
        TERM_ARRAY_LEN(ARR(s), ARR_LEN(s));
    else
        TERM_SEQUENCE(s);
}


// !!! PLEASE NOTE: !!! These variants do not cast the result to ARR() in
// order to chain it, because `gcc (Ubuntu/Linaro 4.6.3-1ubuntu5) 4.6.3`
// complained about "value computed but not used".  The chaining feature
// wasn't really being used anyway, so it wasn't worth it to workaround.
//
#define Manage_Array(a)             Manage_Series(SER(a))  // SEE NOTE
#define Force_Array_Managed(a)     Force_Series_Managed(SER(a))  // SEE NOTE


//
// REBVAL cells cannot be written to unless they carry CELL_FLAG_CELL, and
// have been "formatted" to convey their lifetime (stack or array).  This
// helps debugging, but is also important information needed by Move_Value()
// for deciding if the lifetime of a target cell requires the "reification"
// of any temporary referenced structures into ones managed by the GC.
//
// Performance-wise, the prep process requires writing one `uintptr_t`-sized
// header field per cell.  For fully optimum efficiency, clients filling
// arrays can initialize the bits as part of filling in cells vs. using
// Prep_Array.  This is done by the evaluator when building the f->varlist for
// a frame (it's walking the parameters anyway).  However, this is usually
// not necessary--and sacrifices generality for code that wants to work just
// as well on stack values and heap values.
//
inline static void Prep_Array(
    REBARR *a,
    REBLEN capacity_plus_one // Expand_Series passes 0 on dynamic reallocation
){
    assert(IS_SER_DYNAMIC(a));

    RELVAL *prep = ARR_HEAD(a);

    if (NOT_SERIES_FLAG(a, FIXED_SIZE)) {
        //
        // Expandable arrays prep all cells, including in the not-yet-used
        // capacity.  Otherwise you'd waste time prepping cells on every
        // expansion and un-prepping them on every shrink.
        //
        REBLEN n;
        for (n = 0; n < SER(a)->content.dynamic.rest - 1; ++n, ++prep)
            Prep_Cell(prep);
    }
    else {
        assert(capacity_plus_one != 0);

        REBLEN n;
        for (n = 1; n < capacity_plus_one; ++n, ++prep)
            Prep_Cell(prep);  // have to prep cells in useful capacity

        // If an array isn't expandable, let the release build not worry
        // about the bits in the excess capacity.  But set them to trash in
        // the debug build.
        //
        prep->header = Endlike_Header(0); // unwritable
        TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
      #if !defined(NDEBUG)
        while (n < SER(a)->content.dynamic.rest) { // no -1 (n is 1-based)
            ++n;
            ++prep;
            prep->header.bits =
                FLAG_KIND_BYTE(REB_T_TRASH)
                | FLAG_MIRROR_BYTE(REB_T_TRASH); // unreadable
            TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
        }
      #endif

        // Currently, release build also puts an unreadable end at capacity.
        // It may not be necessary, but doing it for now to have an easier
        // invariant to work with.  Review.
        //
        prep = ARR_AT(a, SER(a)->content.dynamic.rest - 1);
        // fallthrough
    }

    // Although currently all dynamically allocated arrays use a full REBVAL
    // cell for the end marker, it could use everything except the second byte
    // of the first `uintptr_t` (which must be zero to denote end).  To make
    // sure no code depends on a full cell in the last location,  make it
    // an unwritable end--to leave flexibility to use the rest of the cell.
    //
    prep->header = Endlike_Header(0);
    TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
}


// Make a series that is the right size to store REBVALs (and marked for the
// garbage collector to look into recursively).  ARR_LEN() will be 0.
//
inline static REBARR *Make_Array_Core(REBLEN capacity, REBFLGS flags) {
    const REBLEN wide = sizeof(REBVAL);

    REBSER *s = Alloc_Series_Node(flags);

    if (
        (flags & SERIES_FLAG_ALWAYS_DYNAMIC) // inlining will constant fold
        or capacity > 1
    ){
        capacity += 1; // account for cell needed for terminator (END)

        if (cast(REBU64, capacity) * wide > INT32_MAX) // too big
            fail (Error_No_Memory(cast(REBU64, capacity) * wide));

        s->info = Endlike_Header(FLAG_LEN_BYTE_OR_255(255)); // dynamic
        if (not Did_Series_Data_Alloc(s, capacity)) // expects LEN_BYTE=255
            fail (Error_No_Memory(capacity * wide));

        Prep_Array(ARR(s), capacity);
        SET_END(ARR_HEAD(ARR(s)));

      #if !defined(NDEBUG)
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }
    else {
        SER_CELL(s)->header.bits = CELL_MASK_PREP_END;
        TRACK_CELL_IF_DEBUG(SER_CELL(s), "<<make>>", 0);

        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // implicit termination
                | FLAG_LEN_BYTE_OR_255(0)
        );
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use PUSH_GC_GUARD().
    //
    // !!! Code duplicated in Make_Series_Core ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) { // most callsites const fold this
        if (SER_FULL(GC_Manuals))
            Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.used++
        ] = s; // start out managed to not need to find/remove from this later
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED) { // most callsites fold
        assert(flags & SERIES_FLAG_LINK_NODE_NEEDS_MARK);
        if (
            FS_TOP->feed->array and
            GET_ARRAY_FLAG(FS_TOP->feed->array, HAS_FILE_LINE_UNMASKED)
        ){
            LINK_FILE_NODE(s) = LINK_FILE_NODE(FS_TOP->feed->array);
            MISC(s).line = MISC(FS_TOP->feed->array).line;
        }
        else {
            CLEAR_ARRAY_FLAG(s, HAS_FILE_LINE_UNMASKED);
            CLEAR_SERIES_FLAG(s, LINK_NODE_NEEDS_MARK);
        }
    }

  #if !defined(NDEBUG)
    PG_Reb_Stats->Blocks++;
  #endif

    assert(ARR_LEN(cast(REBARR*, s)) == 0);
    return cast(REBARR*, s);
}

#define Make_Array(capacity) \
    Make_Array_Core((capacity), ARRAY_MASK_HAS_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Array() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
inline static REBARR *Make_Array_For_Copy(
    REBLEN capacity,
    REBFLGS flags,
    const REBARR *original
){
    if (original and GET_ARRAY_FLAG(original, NEWLINE_AT_TAIL)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;
    }

    if (
        (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED)
        and (original and GET_ARRAY_FLAG(original, HAS_FILE_LINE_UNMASKED))
    ){
        REBARR *a = Make_Array_Core(
            capacity,
            flags & ~ARRAY_FLAG_HAS_FILE_LINE_UNMASKED
        );
        LINK_FILE_NODE(a) = LINK_FILE_NODE(original);
        MISC(a).line = MISC(original).line;
        SET_ARRAY_FLAG(a, HAS_FILE_LINE_UNMASKED);
        return a;
    }

    return Make_Array_Core(capacity, flags);
}


// A singular array is specifically optimized to hold *one* value in a REBSER
// node directly, and stay fixed at that size.
//
// Note ARR_SINGLE() must be overwritten by the caller...it contains an END
// marker but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need ARRAY_FLAG_HAS_FILE_LINE.
//
inline static REBARR *Alloc_Singular(REBFLGS flags) {
    assert(not (flags & SERIES_FLAG_ALWAYS_DYNAMIC));
    REBARR *a = Make_Array_Core(1, flags | SERIES_FLAG_FIXED_SIZE);
    mutable_LEN_BYTE_OR_255(SER(a)) = 1; // non-dynamic length (default was 0)
    return a;
}

#define Append_Value(a,v) \
    Move_Value(Alloc_Tail_Array(a), (v))

#define Append_Value_Core(a,v,s) \
    Derelativize(Alloc_Tail_Array(a), (v), (s))

// Modes allowed by Copy_Block function:
enum {
    COPY_SHALLOW = 1 << 0,
    COPY_DEEP = 1 << 1, // recurse into arrays
    COPY_STRINGS = 1 << 2,
    COPY_OBJECT = 1 << 3,
    COPY_SAME = 1 << 4
};

#define COPY_ALL \
    (COPY_DEEP | COPY_STRINGS)


#define Copy_Values_Len_Shallow(v,s,l) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), 0, 0)

#define Copy_Values_Len_Shallow_Core(v,s,l,f) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), 0, (f))

#define Copy_Values_Len_Extra_Shallow(v,s,l,e) \
    Copy_Values_Len_Extra_Shallow_Core((v), (s), (l), (e), 0) 


#define Copy_Array_Shallow(a,s) \
    Copy_Array_At_Shallow((a), 0, (s))

#define Copy_Array_Shallow_Flags(a,s,f) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), 0, (f))

#define Copy_Array_Deep_Managed(a,s) \
    Copy_Array_At_Extra_Deep_Flags_Managed((a), 0, (s), 0, SERIES_FLAGS_NONE)

#define Copy_Array_Deep_Flags_Managed(a,s,f) \
    Copy_Array_At_Extra_Deep_Flags_Managed((a), 0, (s), 0, (f))

#define Copy_Array_At_Deep_Managed(a,i,s) \
    Copy_Array_At_Extra_Deep_Flags_Managed((a), (i), (s), 0, SERIES_FLAGS_NONE)

#define COPY_ANY_ARRAY_AT_DEEP_MANAGED(v) \
    Copy_Array_At_Extra_Deep_Flags_Managed( \
        VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), 0, SERIES_FLAGS_NONE)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0, SERIES_FLAGS_NONE)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e), SERIES_FLAGS_NONE)

// See TS_NOT_COPIED for the default types excluded from being deep copied
//
inline static REBARR* Copy_Array_At_Extra_Deep_Flags_Managed(
    const REBARR *original, // ^-- not macro because original mentioned twice
    REBLEN index,
    REBSPC *specifier,
    REBLEN extra,
    REBFLGS flags
){
    return Copy_Array_Core_Managed(
        original,
        index, // at
        specifier,
        ARR_LEN(original), // tail
        extra, // extra
        flags, // note no ARRAY_HAS_FILE_LINE by default
        TS_SERIES & ~TS_NOT_COPIED // types
    );
}

#define Free_Unmanaged_Array(a) \
    Free_Unmanaged_Series(SER(a))



//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-ARRAY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %sys-bind.h
//

#define EMPTY_BLOCK \
    Root_Empty_Block

#define EMPTY_ARRAY \
    PG_Empty_Array // Note: initialized from VAL_ARRAY(Root_Empty_Block)

#define EMPTY_TEXT \
    Root_Empty_Text

#define EMPTY_BINARY \
    Root_Empty_Binary


// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static const REBARR *VAL_ARRAY(REBCEL(const*) v) {
    assert(ANY_ARRAY_KIND(CELL_KIND(v)));

    const REBARR *a = ARR(PAYLOAD(Any, v).first.node);
    if (GET_SERIES_INFO(a, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return ARR(a);
}

#define VAL_ARRAY_ENSURE_MUTABLE(v) \
    m_cast(REBARR*, VAL_ARRAY(ENSURE_MUTABLE(v)))

#define VAL_ARRAY_KNOWN_MUTABLE(v) \
    m_cast(REBARR*, VAL_ARRAY(KNOWN_MUTABLE(v)))


// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
inline static const RELVAL *VAL_ARRAY_AT(REBCEL(const*) v) {
    if (VAL_INDEX(v) > ARR_LEN(VAL_ARRAY(v)))
        fail (Error_Past_End_Raw());  // don't clip and give deceptive pointer
    return ARR_AT(VAL_ARRAY(v), VAL_INDEX(v));
}

#define VAL_ARRAY_AT_ENSURE_MUTABLE(v) \
    m_cast(RELVAL*, VAL_ARRAY_AT(ENSURE_MUTABLE(v)))

#define VAL_ARRAY_KNOWN_MUTABLE_AT(v) \
    m_cast(RELVAL*, VAL_ARRAY_AT(KNOWN_MUTABLE(v)))

// !!! R3-Alpha introduced concepts of immutable series with PROTECT, but
// did not consider the protected status to apply to binding.  Ren-C added
// more notions of immutability (const, holds, locking/freezing) and enforces
// it at compile-time...which caught many bugs.  But being able to bind
// "immutable" data was mechanically required by R3-Alpha for efficiency...so
// new answers will be needed.  See Virtual_Bind_Deep_To_New_Context() for
// some of the thinking on this topic.  Until it's solved, binding-related
// calls to this function get mutable access on non-mutable series.  :-/
//
#define VAL_ARRAY_AT_MUTABLE_HACK(v) \
    m_cast(RELVAL*, VAL_ARRAY_AT(v))

#define VAL_ARRAY_LEN_AT(v) \
    VAL_LEN_AT(v)

inline static const RELVAL *VAL_ARRAY_TAIL(const RELVAL *v)
  { return ARR_TAIL(VAL_ARRAY(v)); }


// !!! VAL_ARRAY_AT_HEAD() is a leftover from the old definition of
// VAL_ARRAY_AT().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted from the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
inline static const RELVAL *VAL_ARRAY_AT_HEAD(const RELVAL *v, REBLEN n) {
    const REBARR *a = VAL_ARRAY(v);  // debug build checks it's ANY-ARRAY!
    if (n > ARR_LEN(a))
        fail (Error_Past_End_Raw());
    return ARR_AT(a, (n));
}

#define Init_Any_Array_At(v,t,a,i) \
    Init_Any_Series_At((v), (t), SER(a), (i))

#define Init_Any_Array(v,t,a) \
    Init_Any_Array_At((v), (t), (a), 0)

#define Init_Block(v,s) \
    Init_Any_Array((v), REB_BLOCK, (s))

#define Init_Group(v,s) \
    Init_Any_Array((v), REB_GROUP, (s))


inline static RELVAL *Init_Relative_Block_At(
    RELVAL *out,
    REBACT *action,  // action to which array has relative bindings
    REBARR *array,
    REBLEN index
){
    RELVAL *block = RESET_CELL(out, REB_BLOCK, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE(block, array);
    VAL_INDEX(block) = index;
    INIT_BINDING(block, action);
    return block;
}

#define Init_Relative_Block(out,action,array) \
    Init_Relative_Block_At((out), (action), (array), 0)


// The rule for splicing is now fixed as "only plain BLOCK! splices":
// https://forum.rebol.info/t/1332
//
// Despite the simple contract, using a call to this routine helps document
// placs where the decision to splice or not is being made.
//
#define Splices_Without_Only(v) \
    IS_BLOCK(v)


// Checks if ANY-GROUP! is like ((...)) or (...), used by COMPOSE & PARSE
//
inline static bool Is_Any_Doubled_Group(REBCEL(const*) group) {
    assert(ANY_GROUP_KIND(CELL_KIND(group)));
    const RELVAL *inner = VAL_ARRAY_AT(group);
    if (KIND_BYTE(inner) != REB_GROUP or NOT_END(inner + 1))
        return false; // plain (...) GROUP!
    return true; // a ((...)) GROUP!, inject as rule
}


#ifdef NDEBUG
    #define ASSERT_ARRAY(s) \
        NOOP

    #define ASSERT_ARRAY_MANAGED(array) \
        NOOP

    #define ASSERT_SERIES(s) \
        NOOP
#else
    #define ASSERT_ARRAY(s) \
        Assert_Array_Core(s)

    #define ASSERT_ARRAY_MANAGED(array) \
        ASSERT_SERIES_MANAGED(SER(array))

    static inline void ASSERT_SERIES(const REBSER *s) {
        if (IS_SER_ARRAY(s))
            Assert_Array_Core(ARR(s));
        else
            Assert_Series_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (ARR_LEN(a) != 0 and (v) >= ARR_HEAD(a) and (v) < ARR_TAIL(a))
#endif
