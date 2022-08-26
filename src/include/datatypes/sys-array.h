//
//  File: %sys-array.h
//  Summary: {Definitions for Raw_Array}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// A "Rebol Array" is a series of value cells.  Every BLOCK! or GROUP! points
// at an array node, which you see in the source as Array(*).
//
// While many array operations are shared in common with REBSER, there is a
// (deliberate) type incompatibility introduced.  The type compatibility is
// only present when building as C++.  To cast as the underlying REBSER, use
// he SER() operation.
//
// An ARRAY is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
//  * In R3-Alpha, there was a full-sized cell at the end of every array that
//    would hold an END signal--much like a strin  terminator.  Ren-C does not
//    terminate arrays but relies on the known length, in order to save on
//    space.  This also avoids the cost of keeping the terminator up to date
//    as the array grows or resizes.
//
//   (Note: The debug build may put "trash" at the tail position whenever
//    the array size is updated, to make it easier to catch out-of-bounds
//    access.  But the release build does not do this)


// !!! We generally want to use LINK(Filename, x) but that uses the STR()
// macro which is not defined in this file.  There's a bit of a circular
// dependency since %sys-string.h uses arrays for bookmarks; so having a
// special operation here is an easy workaround that still lets us make a
// lot of this central code inlinable.
//
#define LINK_FILENAME_HACK(s) \
    cast(String(const*), s->link.any.node)


inline static bool Has_Newline_At_Tail(Array(const*) a) {
    if (SER_FLAVOR(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->leader.bits & ARRAY_FLAG_NEWLINE_AT_TAIL);
}

inline static bool Has_File_Line(Array(const*) a) {
    if (SER_FLAVOR(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->leader.bits & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED);
}


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  Since
// empty arrays have no "last" value then ARR_LAST should not be called on it.

inline static Cell(*) ARR_AT(const_if_c Array(*) a, REBLEN n)
  { return SER_AT(Reb_Cell, a, n); }

inline static Cell(*) ARR_HEAD(const_if_c Array(*) a)
  { return SER_HEAD(Reb_Cell, a); }

inline static Cell(*) ARR_TAIL(const_if_c Array(*) a)
  { return SER_TAIL(Reb_Cell, a); }

inline static Cell(*) ARR_LAST(const_if_c Array(*) a)
  { return SER_LAST(Reb_Cell, a); }

inline static Cell(*) ARR_SINGLE(const_if_c Array(*) a) {
    assert(NOT_SERIES_FLAG(a, DYNAMIC));
    return mutable_SER_CELL(a);
}

#if CPLUSPLUS_11
    inline static Cell(const*) ARR_AT(Array(const*) a, REBLEN n)
        { return SER_AT(const Reb_Cell, a, n); }

    inline static Cell(const*) ARR_HEAD(Array(const*) a)
        { return SER_HEAD(const Reb_Cell, a); }

    inline static Cell(const*) ARR_TAIL(Array(const*) a)
        { return SER_TAIL(const Reb_Cell, a); }

    inline static Cell(const*) ARR_LAST(Array(const*) a)
        { return SER_LAST(const Reb_Cell, a); }

    inline static Cell(const*) ARR_SINGLE(Array(const*) a) {
        assert(NOT_SERIES_FLAG(a, DYNAMIC));
        return SER_CELL(a);
    }
#endif


// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
inline static Array(*) Singular_From_Cell(Cell(const*) v) {
    Array(*) singular = ARR(  // some checking in debug builds is done by ARR()
        cast(void*,
            cast(Byte*, m_cast(Cell(*), v))
            - offsetof(struct Reb_Stub, content)
        )
    );
    assert(NOT_SERIES_FLAG(singular, DYNAMIC));
    return singular;
}

inline static REBLEN ARR_LEN(Array(const*) a)
  { return SER_USED(a); }



//
// REBVAL cells cannot be written to unless they carry CELL_FLAG_CELL, and
// have been "formatted" to convey their lifetime (stack or array).  This
// helps debugging, but is also important information needed by Copy_Cell()
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
    Array(*) a,
    REBLEN capacity  // Expand_Series passes 0 on dynamic reallocation
){
    assert(GET_SERIES_FLAG(a, DYNAMIC));

    Cell(*) prep = ARR_HEAD(a);

    if (NOT_SERIES_FLAG(a, FIXED_SIZE)) {
        //
        // Expandable arrays prep all cells, including in the not-yet-used
        // capacity.  Otherwise you'd waste time prepping cells on every
        // expansion and un-prepping them on every shrink.
        //
        REBLEN n;
        for (n = 0; n < a->content.dynamic.rest; ++n, ++prep)
            Erase_Cell(prep);

      #if DEBUG_POISON_SERIES_TAILS  // allocation deliberately oversized by 1
        Poison_Cell(ARR_AT(a, a->content.dynamic.rest - 1));
      #endif
    }
    else {
        REBLEN n;
        for (n = 0; n < capacity; ++n, ++prep)
            Erase_Cell(prep);  // have to prep cells in useful capacity

        // If an array isn't expandable, let the release build not worry
        // about the bits in the excess capacity.  But poison them in
        // the debug build.
        //
      #if DEBUG_POISON_EXCESS_CAPACITY
        for (; n < a->content.dynamic.rest; ++n, ++prep)
            Poison_Cell(prep);  // unreadable + unwritable
      #endif
    }
}


// Make a series that is the right size to store REBVALs (and marked for the
// garbage collector to look into recursively).  ARR_LEN() will be 0.
//
inline static Array(*) Make_Array_Core_Into(
    void* preallocated,
    REBLEN capacity,
    Flags flags
){
  #if DEBUG_POISON_SERIES_TAILS  // non-dynamic arrays poisoned by bit pattern
    if (capacity > 1 or (flags & SERIES_FLAG_DYNAMIC))
        capacity += 1;  // account for space needed for poison cell
  #endif

    REBSER *s = Make_Series_Into(preallocated, capacity, flags);
    assert(IS_SER_ARRAY(s));  // flavor should have been an array flavor

    if (GET_SERIES_FLAG(s, DYNAMIC)) {
        Prep_Array(ARR(s), capacity);

      #if DEBUG_POISON_SERIES_TAILS
        Poison_Cell(ARR_HEAD(ARR(s)));
      #endif
    }
    else {
        Poison_Cell(mutable_SER_CELL(s));  // optimized prep for 0 length
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (
        FLAVOR_BYTE(flags) == FLAVOR_ARRAY
        and (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED)  // hope callsites fold
    ){
        assert(flags & SERIES_FLAG_LINK_NODE_NEEDS_MARK);
        if (
            not FRM_IS_VARIADIC(TOP_FRAME) and
            Get_Subclass_Flag(ARRAY, FRM_ARRAY(TOP_FRAME), HAS_FILE_LINE_UNMASKED)
        ){
            mutable_LINK(Filename, s) = LINK_FILENAME_HACK(FRM_ARRAY(TOP_FRAME));
            s->misc.line = FRM_ARRAY(TOP_FRAME)->misc.line;
        }
        else {
            Clear_Subclass_Flag(ARRAY, s, HAS_FILE_LINE_UNMASKED);
            CLEAR_SERIES_FLAG(s, LINK_NODE_NEEDS_MARK);
        }
    }

  #if DEBUG_COLLECT_STATS
    PG_Reb_Stats->Blocks++;
  #endif

    assert(ARR_LEN(cast(Array(*), s)) == 0);
    return cast(Array(*), s);
}

#define Make_Array_Core(capacity,flags) \
    Make_Array_Core_Into(Alloc_Series_Node(), (capacity), (flags))

#define Make_Array(capacity) \
    Make_Array_Core((capacity), ARRAY_MASK_HAS_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Array() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
inline static Array(*) Make_Array_For_Copy(
    REBLEN capacity,
    Flags flags,
    Array(const*) original
){
    if (original and Has_Newline_At_Tail(original)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;
    }

    if (
        FLAVOR_BYTE(flags) == FLAVOR_ARRAY
        and (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED)
        and (original and Has_File_Line(original))
    ){
        Array(*) a = Make_Array_Core(
            capacity,
            flags & ~ARRAY_FLAG_HAS_FILE_LINE_UNMASKED
        );
        mutable_LINK(Filename, a) = LINK_FILENAME_HACK(original);
        a->misc.line = original->misc.line;
        Set_Subclass_Flag(ARRAY, a, HAS_FILE_LINE_UNMASKED);
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
inline static Array(*) Alloc_Singular(Flags flags) {
    assert(not (flags & SERIES_FLAG_DYNAMIC));
    Array(*) a = Make_Array_Core(1, flags | SERIES_FLAG_FIXED_SIZE);
    Erase_Cell(mutable_SER_CELL(a));  // poison means length 0, erased length 1
    return a;
}

#define Append_Value(a,v) \
    Copy_Cell(Alloc_Tail_Array(a), (v))

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
inline static Array(*) Copy_Array_At_Extra_Deep_Flags_Managed(
    Array(const*) original, // ^-- not macro because original mentioned twice
    REBLEN index,
    REBSPC *specifier,
    REBLEN extra,
    Flags flags
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


// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static Array(const*) VAL_ARRAY(noquote(Cell(const*)) v) {
    assert(ANY_ARRAYLIKE(v));

    Array(const*) a = ARR(VAL_NODE1(v));
    if (GET_SERIES_FLAG(a, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return a;
}

#define VAL_ARRAY_ENSURE_MUTABLE(v) \
    m_cast(Array(*), VAL_ARRAY(ENSURE_MUTABLE(v)))

#define VAL_ARRAY_KNOWN_MUTABLE(v) \
    m_cast(Array(*), VAL_ARRAY(KNOWN_MUTABLE(v)))


// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
// IMPORTANT: This routine will trigger a failure if the array index is out
// of bounds of the data.  If a function can deal with such out of bounds
// arrays meaningfully, it should work with VAL_INDEX_UNBOUNDED().
//
inline static Cell(const*) VAL_ARRAY_LEN_AT(
    option(REBLEN*) len_at_out,
    noquote(Cell(const*)) v
){
    Array(const*) arr = VAL_ARRAY(v);
    REBIDX i = VAL_INDEX_RAW(v);  // VAL_ARRAY() already checks it's series
    REBLEN len = ARR_LEN(arr);
    if (i < 0 or i > cast(REBIDX, len))
        fail (Error_Index_Out_Of_Range_Raw());
    if (len_at_out)  // inlining should remove this if() for VAL_ARRAY_AT()
        *unwrap(len_at_out) = len - i;
    return ARR_AT(arr, i);
}

inline static Cell(const*) VAL_ARRAY_AT(
    option(Cell(const*)*) tail_out,
    noquote(Cell(const*)) v
){
    Array(const*) arr = VAL_ARRAY(v);
    REBIDX i = VAL_INDEX_RAW(v);  // VAL_ARRAY() already checks it's series
    REBLEN len = ARR_LEN(arr);
    if (i < 0 or i > cast(REBIDX, len))
        fail (Error_Index_Out_Of_Range_Raw());
    Cell(const*) at = ARR_AT(arr, i);
    if (tail_out)  // inlining should remove this if() for no tail
        *unwrap(tail_out) = at + (len - i);
    return at;
}

inline static Cell(const*) VAL_ARRAY_AT_HEAD_T(
    option(Cell(const*)*) tail_out,
    noquote(Cell(const*)) v
){
    Array(const*) arr = VAL_ARRAY(v);
    REBIDX i = VAL_INDEX_RAW(v);  // VAL_ARRAY() already checks it's series
    Cell(const*) at = ARR_AT(arr, i);
    if (tail_out) {  // inlining should remove this if() for no tail
        REBLEN len = ARR_LEN(arr);
        *unwrap(tail_out) = at + len;
    }
    return at;
}

inline static Cell(const*) VAL_ARRAY_ITEM_AT(noquote(Cell(const*)) v) {
    Cell(const*) tail;
    Cell(const*) item = VAL_ARRAY_AT(&tail, v);
    assert(item != tail);  // should be a valid value
    return item;
}


#define VAL_ARRAY_AT_ENSURE_MUTABLE(tail_out,v) \
    m_cast(Cell(*), VAL_ARRAY_AT((tail_out), ENSURE_MUTABLE(v)))

#define VAL_ARRAY_KNOWN_MUTABLE_AT(tail_out,v) \
    m_cast(Cell(*), VAL_ARRAY_AT((tail_out), KNOWN_MUTABLE(v)))


// !!! R3-Alpha introduced concepts of immutable series with PROTECT, but
// did not consider the protected status to apply to binding.  Ren-C added
// more notions of immutability (const, holds, locking/freezing) and enforces
// it at compile-time...which caught many bugs.  But being able to bind
// "immutable" data was mechanically required by R3-Alpha for efficiency...so
// new answers will be needed.  See Virtual_Bind_Deep_To_New_Context() for
// some of the thinking on this topic.  Until it's solved, binding-related
// calls to this function get mutable access on non-mutable series.  :-/
//
#define VAL_ARRAY_AT_MUTABLE_HACK(tail_out,v) \
    m_cast(Cell(*), VAL_ARRAY_AT((tail_out), (v)))

#define VAL_ARRAY_TAIL(v) \
  ARR_TAIL(VAL_ARRAY(v))


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
inline static Cell(const*) VAL_ARRAY_AT_HEAD(
    Cell(const*) v,
    REBLEN n
){
    Array(const*) a = VAL_ARRAY(v);  // debug build checks it's ANY-ARRAY!
    if (n > ARR_LEN(a))
        fail (Error_Index_Out_Of_Range_Raw());
    return ARR_AT(a, (n));
}

//=//// ANY-ARRAY! INITIALIZER HELPERS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a Array(*) to
// initialize, and the C++ build can also validate managed consistent w/const.

inline static REBVAL *Init_Array_Cell_At_Core(
    Cell(*) out,
    enum Reb_Kind kind,
    const_if_c Array(*) array,
    REBLEN index,
    Array(*) binding
){
    return Init_Series_Cell_At_Core(
        out,
        kind,
        Force_Series_Managed_Core(array),
        index,
        binding
    );
}

#if CPLUSPLUS_11
    inline static REBVAL *Init_Array_Cell_At_Core(
        Cell(*) out,
        enum Reb_Kind kind,
        Array(const*) array,  // all const arrays should be already managed
        REBLEN index,
        Array(*) binding
    ){
        return Init_Series_Cell_At_Core(out, kind, array, index, binding);
    }
#endif

#define Init_Array_Cell_At(v,t,a,i) \
    Init_Array_Cell_At_Core((v), (t), (a), (i), UNBOUND)

#define Init_Array_Cell(v,t,a) \
    Init_Array_Cell_At((v), (t), (a), 0)

#define Init_Block(v,s)     Init_Array_Cell((v), REB_BLOCK, (s))
#define Init_Group(v,s)     Init_Array_Cell((v), REB_GROUP, (s))


inline static Cell(*) Init_Relative_Block_At(
    Cell(*) out,
    Action(*) action,  // action to which array has relative bindings
    Array(*) array,
    REBLEN index
){
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_BLOCK);
    INIT_VAL_NODE1(out, array);
    VAL_INDEX_RAW(out) = index;
    INIT_SPECIFIER(out, action);
    return out;
}

#define Init_Relative_Block(out,action,array) \
    Init_Relative_Block_At((out), (action), (array), 0)


#ifdef NDEBUG
    #define ASSERT_ARRAY(s)     NOOP
    #define ASSERT_SERIES(s)    NOOP
#else
    #define ASSERT_ARRAY(s) \
        Assert_Array_Core(s)

    inline static void ASSERT_SERIES(const REBSER *s) {
        if (IS_SER_ARRAY(s))
            Assert_Array_Core(ARR(s));
        else
            Assert_Series_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (ARR_LEN(a) != 0 and (v) >= ARR_HEAD(a) and (v) < ARR_TAIL(a))
#endif


#undef LINK_FILENAME_HACK  // later files shoul use LINK(Filename, x)
