//
//  File: %sys-array.h
//  Summary: {Definitions for REBARR}
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
// A "Rebol Array" is a series of REBVAL values which is terminated by an
// END marker.  In R3-Alpha, the END marker was itself a full-sized REBVAL
// cell...so code was allowed to write one cell past the capacity requested
// when Make_Arr() was called.  But this always had to be an END.
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

inline static RELVAL *ARR_AT(REBARR *a, REBCNT n)
    { return SER_AT(RELVAL, cast(REBSER*, a), n); }

inline static RELVAL *ARR_HEAD(REBARR *a)
    { return SER_HEAD(RELVAL, cast(REBSER*, a)); }

inline static RELVAL *ARR_TAIL(REBARR *a)
    { return SER_TAIL(RELVAL, cast(REBSER*, a)); }

inline static RELVAL *ARR_LAST(REBARR *a)
    { return SER_LAST(RELVAL, cast(REBSER*, a)); }

inline static RELVAL *ARR_SINGLE(REBARR *a) {
    assert(not IS_SER_DYNAMIC(a)); // singular test avoided in release build
    return cast(RELVAL*, &SER(a)->content.fixed);
}

// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
inline static REBARR *Singular_From_Cell(const RELVAL *v) {
    REBARR *singular = ARR( // some checking in debug builds is done by ARR()
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
    SER_LEN(SER(a))


// Set length and also terminate.  This routine avoids conditionality in the
// release build, which means it may overwrite a signal byte in a "read-only"
// end (such as an Endlike_Header).  Not branching is presumed to perform
// better, but cells that weren't ends already are writability checked.
//
// !!! Review if SERIES_FLAG_FIXED_SIZE should be calling this routine.  At
// the moment, fixed size series merely can't expand, but it might be more
// efficient if they didn't use any "appending" operators to get built.
//
inline static void TERM_ARRAY_LEN(REBARR *a, REBCNT len) {
    assert(len < SER_REST(SER(a)));
    SET_SERIES_LEN(SER(a), len);

  #if !defined(NDEBUG)
    if (NOT_END(ARR_AT(a, len)))
        ASSERT_CELL_WRITABLE_EVIL_MACRO(ARR_AT(a, len), __FILE__, __LINE__);
  #endif
    mutable_SECOND_BYTE(ARR_AT(a, len)->header.bits) = REB_0_END;
}

inline static void SET_ARRAY_LEN_NOTERM(REBARR *a, REBCNT len) {
    SET_SERIES_LEN(SER(a), len); // call out non-terminating usages
}

inline static void RESET_ARRAY(REBARR *a) {
    TERM_ARRAY_LEN(a, 0);
}

inline static void TERM_SERIES(REBSER *s) {
    if (IS_SER_ARRAY(s))
        TERM_ARRAY_LEN(ARR(s), SER_LEN(s));
    else
        memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}


// Setting and getting array flags is common enough to want a macro for it
// vs. having to extract the ARR_SERIES to do it each time.
//
#define IS_ARRAY_MANAGED(a) \
    IS_SERIES_MANAGED(SER(a))

#define MANAGE_ARRAY(a) \
    MANAGE_SERIES(SER(a))

#define ENSURE_ARRAY_MANAGED(a) \
    ENSURE_SERIES_MANAGED(SER(a))


//
// REBVAL cells cannot be written to unless they carry VALUE_FLAG_CELL, and
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
    REBCNT capacity_plus_one // Expand_Series passes 0 on dynamic reallocation
){
    assert(IS_SER_DYNAMIC(a));

    RELVAL *prep = ARR_HEAD(a);

    if (NOT_SER_FLAG(a, SERIES_FLAG_FIXED_SIZE)) {
        //
        // Expandable arrays prep all cells, including in the not-yet-used
        // capacity.  Otherwise you'd waste time prepping cells on every
        // expansion and un-prepping them on every shrink.
        //
        REBCNT n;
        for (n = 0; n < SER(a)->content.dynamic.rest - 1; ++n, ++prep)
            Prep_Non_Stack_Cell(prep);
    }
    else {
        assert(capacity_plus_one != 0);

        REBCNT n;
        for (n = 1; n < capacity_plus_one; ++n, ++prep)
            Prep_Non_Stack_Cell(prep); // have to prep cells in useful capacity

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
            prep->header.bits = FLAG_KIND_BYTE(REB_T_TRASH); // unreadable
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
inline static REBARR *Make_Arr_Core(REBCNT capacity, REBFLGS flags) {
    const REBCNT wide = sizeof(REBVAL);

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
        SER_CELL(s)->header.bits = CELL_MASK_NON_STACK_END;
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
    // !!! Code duplicated in Make_Ser_Core ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) { // most callsites const fold this
        if (SER_FULL(GC_Manuals))
            Extend_Series(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s; // start out managed to not need to find/remove from this later
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (flags & ARRAY_FLAG_FILE_LINE) { // most callsites const fold this
        if (
            FS_TOP->source->array and
            GET_SER_FLAG(FS_TOP->source->array, ARRAY_FLAG_FILE_LINE)
        ){
            LINK(s).file = LINK(FS_TOP->source->array).file;
            MISC(s).line = MISC(FS_TOP->source->array).line;
        }
        else
            CLEAR_SER_FLAG(s, ARRAY_FLAG_FILE_LINE);
    }

  #if !defined(NDEBUG)
    PG_Reb_Stats->Blocks++;
  #endif

    assert(ARR_LEN(cast(REBARR*, s)) == 0);
    return cast(REBARR*, s);
}

#define Make_Arr(capacity) \
    Make_Arr_Core((capacity), ARRAY_FLAG_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Arr() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
inline static REBARR *Make_Arr_For_Copy(
    REBCNT capacity,
    REBFLGS flags,
    REBARR *original
){
    if (original and GET_SER_FLAG(original, ARRAY_FLAG_TAIL_NEWLINE)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_TAIL_NEWLINE;
    }

    if (
        (flags & ARRAY_FLAG_FILE_LINE)
        and (original and GET_SER_FLAG(original, ARRAY_FLAG_FILE_LINE))
    ){
        flags &= ~ARRAY_FLAG_FILE_LINE;

        REBARR *a = Make_Arr_Core(capacity, flags);
        LINK(a).file = LINK(original).file;
        MISC(a).line = MISC(original).line;
        SET_SER_FLAG(a, ARRAY_FLAG_FILE_LINE);
        return a;
    }

    return Make_Arr_Core(capacity, flags);
}


// A singular array is specifically optimized to hold *one* value in a REBSER
// node directly, and stay fixed at that size.
//
// Note ARR_SINGLE() must be overwritten by the caller...it contains an END
// marker but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need SERIES_FLAG_FILE_LINE.
//
inline static REBARR *Alloc_Singular(REBFLGS flags) {
    assert(not (flags & SERIES_FLAG_ALWAYS_DYNAMIC));
    REBARR *a = Make_Arr_Core(1, flags | SERIES_FLAG_FIXED_SIZE);
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
    REBARR *original, // ^-- not a macro because original mentioned twice
    REBCNT index,
    REBSPC *specifier,
    REBCNT extra,
    REBFLGS flags
){
    return Copy_Array_Core_Managed(
        original,
        index, // at
        specifier,
        ARR_LEN(original), // tail
        extra, // extra
        flags, // note no ARRAY_FLAG_FILE_LINE by default
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


inline static void INIT_VAL_ARRAY(RELVAL *v, REBARR *a) {
    INIT_BINDING(v, UNBOUND);
    assert(IS_ARRAY_MANAGED(a));
    v->payload.any_series.series = SER(a);
}

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v) \
    ARR_AT(VAL_ARRAY(v), VAL_INDEX(v))

#define VAL_ARRAY_LEN_AT(v) \
    VAL_LEN_AT(v)

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static REBARR *VAL_ARRAY(const REBCEL *v) {
    if (ANY_PATH_KIND(CELL_KIND(v)))
        assert(v->payload.any_series.index == 0);
    else
        assert(ANY_ARRAY_KIND(CELL_KIND(v)));

    REBSER *s = v->payload.any_series.series;
    if (s->info.bits & SERIES_INFO_INACCESSIBLE)
        fail (Error_Series_Data_Freed_Raw());
    return ARR(s);
}

#define VAL_ARRAY_HEAD(v) \
    ARR_HEAD(VAL_ARRAY(v))

inline static RELVAL *VAL_ARRAY_TAIL(const RELVAL *v) {
    return ARR_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v));
}


// !!! VAL_ARRAY_AT_HEAD() is a leftover from the old definition of
// VAL_ARRAY_AT().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted rom the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
#define VAL_ARRAY_AT_HEAD(v,n) \
    ARR_AT(VAL_ARRAY(v), (n))

#define Init_Any_Array_At(v,t,a,i) \
    Init_Any_Series_At((v), (t), SER(a), (i))

#define Init_Any_Array(v,t,a) \
    Init_Any_Array_At((v), (t), (a), 0)

#define Init_Block(v,s) \
    Init_Any_Array((v), REB_BLOCK, (s))

#define Init_Group(v,s) \
    Init_Any_Array((v), REB_GROUP, (s))

#define Init_Path(v,s) \
    Init_Any_Array((v), REB_PATH, (s))


// PATH! types will splice into each other, but not into a BLOCK! or GROUP!.
// BLOCK! or GROUP! will splice into any other array:
//
//     [a b c d/e/f] -- append copy [a b c] 'd/e/f
//      a/b/c/d/e/f  -- append copy 'a/b/c [d e f]
//     (a b c d/e/f) -- append copy '(a b c) 'd/e/f
//      a/b/c/d/e/f  -- append copy 'a/b/c '(d e f)
//      a/b/c/d/e/f  -- append copy 'a/b/c 'd/e/f
//
// This rule influences the behavior of TO conversions as well:
// https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
//
inline static bool Splices_Into_Type_Without_Only(
    enum Reb_Kind array_kind,
    const REBVAL *arg
){
    // !!! It's desirable for the system to make VOID! insertion "ornery".
    // Requiring the use of /ONLY to put it into arrays may not be perfect,
    // but it's at least something.  Having the check and error in this
    // routine for the moment helps catch it on at least some functions that
    // are similar to APPEND/INSERT/CHANGE in their concerns, and *have*
    // an /ONLY option.
    //
    if (IS_VOID(arg))
        fail ("VOID! cannot be put into arrays without using /ONLY");

    assert(ANY_ARRAY_KIND(array_kind));

    enum Reb_Kind arg_kind = CELL_KIND(VAL_UNESCAPED(arg));
    return arg_kind == REB_GROUP
        or arg_kind == REB_BLOCK
        or (ANY_PATH_KIND(arg_kind) and ANY_PATH_KIND(array_kind));
}


// Checks to see if a GROUP! is like ((...)) or (...), used by COMPOSE & PARSE
//
inline static bool Is_Doubled_Group(const RELVAL *group) {
    assert(IS_GROUP(group));
    RELVAL *inner = VAL_ARRAY_AT(group);
    if (KIND_BYTE(inner) != REB_GROUP or VAL_LEN_AT(group) != 1)
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

    static inline void ASSERT_SERIES(REBSER *s) {
        if (IS_SER_ARRAY(s))
            Assert_Array_Core(ARR(s));
        else
            Assert_Series_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (ARR_LEN(a) != 0 and (v) >= ARR_HEAD(a) and (v) < ARR_TAIL(a))
#endif
