//
//  File: %sys-array.h
//  Summary: {Definitions for Array}
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
// A "Rebol Array" is a series of cell structs which is terminated by an
// END marker.  In R3-Alpha, the END marker was itself a full-sized cell
// which meant code was allowed to write one cell past the capacity requested
// when Make_Array() was called.  But this always had to be an END.
//
// In Ren-C, there is an implicit END marker just past the last cell in the
// capacity.  Allowing a SET_END() on this position could corrupt the END
// signaling slot, which only uses a bit out of a HeaderUnion sized item to
// signal.  Use Term_Array_Len() to safely terminate arrays and respect not
// writing if it's past capacity.
//
// While many operations are shared in common with Series, there is a
// (deliberate) type incompatibility introduced in the C++ build, where
// Array is derived from Series.  So you can pass an Array to functions that
// expect Series, but not vice-versa.
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
// valid for writing a full cell.

INLINE Cell* Array_At(Array* a, REBLEN n)
    { return Flex_At(Cell, cast(Flex*, a), n); }

INLINE Cell* Array_Head(Array* a)
    { return Flex_Head(Cell, cast(Flex*, a)); }

INLINE Cell* Array_Tail(Array* a)
    { return Flex_Tail(Cell, cast(Flex*, a)); }

INLINE Cell* Array_Last(Array* a)
    { return Series_Last(Cell, cast(Flex*, a)); }

INLINE Cell* ARR_SINGLE(Array* a) {
    assert(not Is_Flex_Dynamic(a)); // singular test avoided in release build
    return &a->content.fixed.cell;
}

// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
INLINE Array* Singular_From_Cell(const Cell* v) {
    Array* singular = cast_Array( // some checking in debug builds is done by cast_Array()
        cast(void*,
            cast(Byte*, m_cast(Cell*, v))
            - offsetof(Stub, content)
        )
    );
    assert(not Is_Flex_Dynamic(singular));
    return singular;
}

// As with an ordinary Series, an Array has separate management of its length
// and its terminator.  Many routines seek to choose the precise moment to
// sync these independently for performance reasons (for better or worse).
//
#define Array_Len(a) \
    Flex_Len(a)


// Set length and also terminate.  This routine avoids conditionality in the
// release build, which means it may overwrite a signal byte in a "read-only"
// end (such as an Endlike_Header).  Not branching is presumed to perform
// better, but cells that weren't ends already are writability checked.
//
// !!! Review if FLEX_FLAG_FIXED_SIZE should be calling this routine.  At
// the moment, fixed size series merely can't expand, but it might be more
// efficient if they didn't use any "appending" operators to get built.
//
INLINE void Term_Array_Len(Array* a, REBLEN len) {
    assert(len < Flex_Rest(a));
    Set_Flex_Len(a, len);

    Cell* at = Array_At(a, len);

  #if !defined(NDEBUG)
    if (NOT_END(at))
        Assert_Cell_Writable(at, __FILE__, __LINE__);
  #endif

    KIND_BYTE(at) = REB_0_END;
}

INLINE void SET_ARRAY_LEN_NOTERM(Array* a, REBLEN len) {
    Set_Flex_Len(a, len);  // call out non-terminating usages
}

INLINE void RESET_ARRAY(Array* a) {
    Term_Array_Len(a, 0);
}

INLINE void Term_Flex(Flex* s) {
    if (Is_Flex_Array(s))
        Term_Array_Len(cast_Array(s), Flex_Len(s));
    else
        memset(Flex_Data_At(Flex_Wide(s), s, Flex_Len(s)), 0, Flex_Wide(s));
}


//
// Locking
//

INLINE bool Is_Array_Deeply_Frozen(Array* a) {
    return Get_Flex_Info(a, FROZEN_DEEP);

    // should be frozen all the way down (can only freeze arrays deeply)
}

INLINE void Deep_Freeze_Array(Array* a) {
    Protect_Flex(
        a,
        0, // start protection at index 0
        PROT_DEEP | PROT_SET | PROT_FREEZE
    );
    Uncolor_Array(a);
}

#define Is_Array_Shallow_Read_Only(a) \
    Is_Flex_Read_Only(a)



//
// The cells cannot be written to unless they carry VALUE_FLAG_CELL, and
// have been "formatted" to convey their lifetime (stack or array).  This
// helps debugging, but is also important information needed by Copy_Cell()
// for deciding if the lifetime of a target cell requires the "reification"
// of any temporary referenced structures into ones managed by the GC.
//
// Performance-wise, the prep process requires writing one `uintptr_t`-sized
// header field per cell.  For fully optimum efficiency, clients filling
// arrays can initialize the bits as part of filling in cells vs. using
// Prep_Array.  This is done by the evaluator when building the L->varlist for
// a frame (it's walking the parameters anyway).  However, this is usually
// not necessary--and sacrifices generality for code that wants to work just
// as well on stack values and heap values.
//
INLINE void Prep_Array(
    Array* a,
    REBLEN capacity_plus_one // Expand_Flex passes 0 on dynamic reallocation
){
    assert(Is_Flex_Dynamic(a));

    Cell* prep = Array_Head(a);

    if (Not_Flex_Flag(a, FIXED_SIZE)) {
        //
        // Expandable arrays prep all cells, including in the not-yet-used
        // capacity.  Otherwise you'd waste time prepping cells on every
        // expansion and un-prepping them on every shrink.
        //
        REBLEN n;
        for (n = 0; n < a->content.dynamic.rest - 1; ++n, ++prep)
            Erase_Cell(prep);
    }
    else {
        assert(capacity_plus_one != 0);

        REBLEN n;
        for (n = 1; n < capacity_plus_one; ++n, ++prep)
            Erase_Cell(prep); // have to prep cells in useful capacity

        // If an array isn't expandable, let the release build not worry
        // about the bits in the excess capacity.  But set them to trash in
        // the debug build.
        //
        prep->header = Endlike_Header(0); // unwritable
        TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
      #if !defined(NDEBUG)
        while (n < a->content.dynamic.rest) { // no -1 (n is 1-based)
            ++n;
            ++prep;
            Poison_Cell(prep);
        }
      #endif

        // Currently, release build also puts an unreadable end at capacity.
        // It may not be necessary, but doing it for now to have an easier
        // invariant to work with.  Review.
        //
        prep = Array_At(a, a->content.dynamic.rest - 1);
        // fallthrough
    }

    // Although currently all dynamically allocated arrays use a full sized
    // cell for the end marker, it could use everything except the second byte
    // of the first `uintptr_t` (which must be zero to denote end).  To make
    // sure no code depends on a full cell in the last location,  make it
    // an unwritable end--to leave flexibility to use the rest of the cell.
    //
    prep->header = Endlike_Header(0);
    TRACK_CELL_IF_DEBUG(prep, __FILE__, __LINE__);
}


// Make a series that is the right size to store REBVALs (and marked for the
// garbage collector to look into recursively).  Array_Len() will be 0.
//
INLINE Array* Make_Array_Core(REBLEN capacity, Flags flags) {
    const REBLEN wide = sizeof(Cell);

    Flex* s = Alloc_Flex_Stub(flags);

    if (
        (flags & FLEX_FLAG_ALWAYS_DYNAMIC) // inlining will constant fold
        or capacity > 1
    ){
        capacity += 1; // account for cell needed for terminator (END)

        if (cast(REBU64, capacity) * wide > INT32_MAX) // too big
            fail (Error_No_Memory(cast(REBU64, capacity) * wide));

        s->info = Endlike_Header(FLAG_LEN_BYTE_OR_255(255)); // dynamic
        if (not Did_Flex_Data_Alloc(s, capacity)) // expects LEN_BYTE=255
            fail (Error_No_Memory(capacity * wide));

        Prep_Array(cast_Array(s), capacity);
        SET_END(Array_Head(cast_Array(s)));

      #if !defined(NDEBUG)
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }
    else {
        Stub_Cell(s)->header.bits = CELL_MASK_ERASE_END;
        TRACK_CELL_IF_DEBUG(Stub_Cell(s), "<<make>>", 0);

        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // implicit termination
                | FLAG_LEN_BYTE_OR_255(0)
        );
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use Push_GC_Guard().
    //
    // !!! Code duplicated in Make_Ser_Core ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) { // most callsites const fold this
        if (Is_Flex_Full(GC_Manuals))
            Extend_Flex(GC_Manuals, 8);

        cast(Flex**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.len++
        ] = s; // start out managed to not need to find/remove from this later
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (flags & ARRAY_FLAG_HAS_FILE_LINE) { // most callsites const fold this
        if (
            TOP_LEVEL->source->array and
            Get_Array_Flag(TOP_LEVEL->source->array, HAS_FILE_LINE)
        ){
            LINK(s).file = LINK(TOP_LEVEL->source->array).file;
            MISC(s).line = MISC(TOP_LEVEL->source->array).line;
        }
        else
            Clear_Array_Flag(s, HAS_FILE_LINE);
    }

  #if !defined(NDEBUG)
    PG_Reb_Stats->Blocks++;
  #endif

    assert(Array_Len(cast(Array*, s)) == 0);
    return cast(Array*, s);
}

#define Make_Array(capacity) \
    Make_Array_Core((capacity), ARRAY_FLAG_HAS_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Array() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
INLINE Array* Make_Arr_For_Copy(
    REBLEN capacity,
    Flags flags,
    Array* original
){
    if (original and Get_Array_Flag(original, NEWLINE_AT_TAIL)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;
    }

    if (
        (flags & ARRAY_FLAG_HAS_FILE_LINE)
        and (original and Get_Array_Flag(original, HAS_FILE_LINE))
    ){
        flags &= ~ARRAY_FLAG_HAS_FILE_LINE;

        Array* a = Make_Array_Core(capacity, flags);
        LINK(a).file = LINK(original).file;
        MISC(a).line = MISC(original).line;
        Set_Array_Flag(a, HAS_FILE_LINE);
        return a;
    }

    return Make_Array_Core(capacity, flags);
}


// A singular array is specifically optimized to hold *one* value in a Stub
// node directly, and stay fixed at that size.
//
// Note ARR_SINGLE() must be overwritten by the caller...it contains an END
// marker but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need FLEX_FLAG_FILE_LINE.
//
INLINE Array* Alloc_Singular(Flags flags) {
    assert(not (flags & FLEX_FLAG_ALWAYS_DYNAMIC));
    Array* a = Make_Array_Core(1, flags | FLEX_FLAG_FIXED_SIZE);
    LEN_BYTE_OR_255(a) = 1; // non-dynamic length (defaulted to 0)
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
    Copy_Array_At_Extra_Deep_Flags_Managed((a), 0, (s), 0, FLEX_FLAGS_NONE)

#define Copy_Array_Deep_Flags_Managed(a,s,f) \
    Copy_Array_At_Extra_Deep_Flags_Managed((a), 0, (s), 0, (f))

#define Copy_Array_At_Deep_Managed(a,i,s) \
    Copy_Array_At_Extra_Deep_Flags_Managed((a), (i), (s), 0, FLEX_FLAGS_NONE)

#define COPY_ANY_LIST_AT_DEEP_MANAGED(v) \
    Copy_Array_At_Extra_Deep_Flags_Managed( \
        Cell_Array(v), VAL_INDEX(v), VAL_SPECIFIER(v), 0, FLEX_FLAGS_NONE)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0, FLEX_FLAGS_NONE)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e), FLEX_FLAGS_NONE)

// See TS_NOT_COPIED for the default types excluded from being deep copied
//
INLINE Array* Copy_Array_At_Extra_Deep_Flags_Managed(
    Array* original, // ^-- not a macro because original mentioned twice
    REBLEN index,
    Specifier* specifier,
    REBLEN extra,
    Flags flags
){
    return Copy_Array_Core_Managed(
        original,
        index, // at
        specifier,
        Array_Len(original), // tail
        extra, // extra
        flags, // note no ARRAY_FLAG_HAS_FILE_LINE by default
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
    PG_Empty_Array // Note: initialized from Cell_Array(Root_Empty_Block)

#define EMPTY_TEXT \
    Root_Empty_Text

#define EMPTY_BINARY \
    Root_Empty_Binary


INLINE void INIT_VAL_ARRAY(Cell* v, Array* a) {
    INIT_BINDING(v, UNBOUND);
    assert(Is_Flex_Managed(a));
    v->payload.any_series.series = a;
}

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define Cell_List_At(v) \
    Array_At(Cell_Array(v), VAL_INDEX(v))

#define VAL_ARRAY_LEN_AT(v) \
    Cell_Series_Len_At(v)

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
INLINE Array* Cell_Array(const Cell* v) {
    assert(Any_List(v));
    Flex* s = v->payload.any_series.series;
    if (s->info.bits & FLEX_INFO_INACCESSIBLE)
        fail (Error_Series_Data_Freed_Raw());
    return cast_Array(s);
}

#define VAL_ARRAY_HEAD(v) \
    Array_Head(Cell_Array(v))

INLINE Cell* VAL_ARRAY_TAIL(const Cell* v) {
    return Array_At(Cell_Array(v), VAL_ARRAY_LEN_AT(v));
}


// !!! Cell_List_At_Head() is a leftover from the old definition of
// Cell_List_At().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted rom the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
#define Cell_List_At_Head(v,n) \
    Array_At(Cell_Array(v), (n))

#define Init_Any_List_At(v,t,a,i) \
    Init_Any_Series_At((v), (t), (a), (i))

#define Init_Any_List(v,t,a) \
    Init_Any_List_At((v), (t), (a), 0)

#define Init_Block(v,s) \
    Init_Any_List((v), REB_BLOCK, (s))

#define Init_Group(v,s) \
    Init_Any_List((v), REB_GROUP, (s))

#define Init_Path(v,s) \
    Init_Any_List((v), REB_PATH, (s))


// PATH! types will splice into each other, but not into a BLOCK! or GROUP!.
// BLOCK! or GROUP! will splice into any other array:
//
//     [a b c d/e/f] -- append copy [a b c] 'd/e/f
//      a/b/c/d/e/f  -- append copy 'a/b/c [d e f]
//     (a b c d/e/f) -- append copy the (a b c) 'd/e/f
//      a/b/c/d/e/f  -- append copy 'a/b/c the (d e f)
//      a/b/c/d/e/f  -- append copy 'a/b/c 'd/e/f
//
// This rule influences the behavior of TO conversions as well:
// https://forum.rebol.info/t/justifiable-asymmetry-to-on-block/751
//
INLINE bool Splices_Into_Type_Without_Only(
    enum Reb_Kind array_kind,
    const Value* arg
){
    // !!! It's desirable for the system to make trash insertion "ornery".
    // Requiring the use of /ONLY to put it into arrays may not be perfect,
    // but it's at least something.  Having the check and error in this
    // routine for the moment helps catch it on at least some functions that
    // are similar to APPEND/INSERT/CHANGE in their concerns, and *have*
    // an /ONLY option.
    //
    if (Is_Nothing(arg))
        fail ("Cannot put trash (~) into arrays");

    assert(Any_List_Kind(array_kind));
    return Is_Group(arg)
        or Is_Block(arg)
        or (Any_Path(arg) and Any_Path_Kind(array_kind));
}


// Checks to see if a GROUP! is like ((...)) or (...), used by COMPOSE & PARSE
//
INLINE bool Is_Doubled_Group(const Cell* group) {
    assert(Is_Group(group));
    Cell* inner = Cell_List_At(group);
    if (VAL_TYPE_RAW(inner) != REB_GROUP or Cell_Series_Len_At(group) != 1)
        return false; // plain (...) GROUP!
    return true; // a ((...)) GROUP!, inject as rule
}


#ifdef NDEBUG
    #define Assert_Array(s) \
        NOOP

    #define Assert_Flex(s) \
        NOOP
#else
    #define Assert_Array(s) \
        Assert_Array_Core(s)

    INLINE void Assert_Flex(Flex* s) {
        if (Is_Flex_Array(s))
            Assert_Array_Core(cast_Array(s));
        else
            Assert_Flex_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (Array_Len(a) != 0 and (v) >= Array_Head(a) and (v) < Array_Tail(a))
#endif
