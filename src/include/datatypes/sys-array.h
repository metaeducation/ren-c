//
//  File: %sys-array.h
//  Summary: {Definitions for Array}
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
// at an array node, which you see in the source as Array*.
//
// While many Array operations are shared in common with Series, there are a
// few (deliberate) type incompatibilities introduced.  This incompatibility
// is only noticed when building as C++, and draws attention to operations
// that make sense on things like string but maybe not on array.
//
// An Array is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
//  * In R3-Alpha, there was a full-sized cell at the end of every array that
//    would hold an END signal--much like a string terminator.  Ren-C does not
//    terminate arrays but relies on the known length, in order to save on
//    space.  This also avoids the cost of keeping the terminator up to date
//    as the array grows or resizes.
//
//   (Note: The debug build may put "trash" at the tail position whenever
//    the array size is updated, to make it easier to catch out-of-bounds
//    access.  But the release build does not do this)


// These flags are only for checking "plain" array flags...so not varlists
// or paramlists or anything that isn't just an ordinary source-level array
// (like you'd find in a BLOCK!)
//
// 1. See mutability notes on Set_Series_Flag() / Clear_Series_Flag()

#define Get_Array_Flag(a,flag) \
    Get_Subclass_Flag(ARRAY, ensure(const Array*, (a)), flag)

#define Not_Array_Flag(a,flag) \
    Not_Subclass_Flag(ARRAY, ensure(const Array*, (a)), flag)

#define Set_Array_Flag(a,flag) \
    Set_Subclass_Flag(ARRAY, m_cast(Array*, (a)), flag)  // [1]

#define Clear_Array_Flag(a,flag) \
    Clear_Subclass_Flag(ARRAY, m_cast(Array*, (a)), flag)  // [1]


INLINE bool Has_Newline_At_Tail(const Array* a) {
    if (Series_Flavor(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->header.bits & ARRAY_FLAG_NEWLINE_AT_TAIL);
}

INLINE bool Has_File_Line(const Array* a) {
    if (Series_Flavor(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->header.bits & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED);
}


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  Since
// empty arrays have no "last" value Array_Last() should not be called on it.

#define Array_At(a,n)           Series_At(Cell, (a), (n))
#define Array_Head(a)           Series_Head(Cell, (a))
#define Array_Tail(a)           Series_Tail(Cell, (a))
#define Array_Last(a)           Series_Last(Cell, (a))

INLINE Cell* Array_Single(const_if_c Array* a) {
    assert(Not_Series_Flag(a, DYNAMIC));
    return Stub_Cell(a);
}

#if CPLUSPLUS_11
    INLINE const Cell* Array_Single(const Array* a) {
        assert(Not_Series_Flag(a, DYNAMIC));
        return Stub_Cell(a);
    }
#endif


// It's possible to calculate the array from just a cell if you know it's a
// cell inside a singular array.
//
INLINE Array* Singular_From_Cell(const Cell* v) {
    Array* singular = cast(Array*,  // DEBUG_CHECK_CASTS checks Array
        cast(void*,
            cast(Byte*, m_cast(Cell*, v))
            - offsetof(Stub, content)
        )
    );
    assert(Not_Series_Flag(singular, DYNAMIC));
    return singular;
}

#define Array_Len(a) \
    Series_Used(ensure(const Array*, (a)))


// See READABLE(), WRITABLE() and related functions for an explanation of the
// bits that have to be formatted in cell headers to be legal to use.
//
INLINE void Prep_Array(
    Array* a,
    REBLEN capacity  // Expand_Series passes 0 on dynamic reallocation
){
    assert(Get_Series_Flag(a, DYNAMIC));

    Cell* prep = Array_Head(a);

    if (Not_Series_Flag(a, FIXED_SIZE)) {
        //
        // Expandable arrays prep all cells, including in the not-yet-used
        // capacity.  Otherwise you'd waste time prepping cells on every
        // expansion and un-prepping them on every shrink.
        //
        REBLEN n;
        for (n = 0; n < a->content.dynamic.rest; ++n, ++prep)
            Erase_Cell(prep);

      #if DEBUG_POISON_SERIES_TAILS  // allocation deliberately oversized by 1
        Poison_Cell(prep - 1);
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
// garbage collector to look into recursively).  Array_Len() will be 0.
//
INLINE Array* Make_Array_Core_Into(
    void* preallocated,
    REBLEN capacity,
    Flags flags
){
  #if DEBUG_POISON_SERIES_TAILS  // non-dynamic arrays poisoned by bit pattern
    if (capacity > 1 or (flags & SERIES_FLAG_DYNAMIC))
        capacity += 1;  // account for space needed for poison cell
  #endif

    Array* a = x_cast(Array*, Make_Series_Into(preallocated, capacity, flags));
    assert(Is_Series_Array(a));  // flavor should have been an array flavor

    if (Get_Series_Flag(a, DYNAMIC)) {
        Prep_Array(a, capacity);

      #if DEBUG_POISON_SERIES_TAILS
        Poison_Cell(Array_Head(a));
      #endif
    }
    else {
        Poison_Cell(Stub_Cell(a));  // optimized prep for 0 length
    }

    // Arrays created at runtime default to inheriting the file and line
    // number from the array executing in the current frame.
    //
    if (
        Flavor_From_Flags(flags) == FLAVOR_ARRAY
        and (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED)  // hope callsites fold
    ){
        assert(flags & SERIES_FLAG_LINK_NODE_NEEDS_MARK);
        if (
            not Level_Is_Variadic(TOP_LEVEL) and
            Get_Array_Flag(Level_Array(TOP_LEVEL), HAS_FILE_LINE_UNMASKED)
        ){
            LINK(Filename, a) = LINK(Filename, Level_Array(TOP_LEVEL));
            a->misc.line = Level_Array(TOP_LEVEL)->misc.line;
        }
        else {
            Clear_Array_Flag(a, HAS_FILE_LINE_UNMASKED);
            Clear_Series_Flag(a, LINK_NODE_NEEDS_MARK);
        }
    }

  #if DEBUG_COLLECT_STATS
    g_mem.blocks_made += 1;
  #endif

    assert(Array_Len(a) == 0);
    return a;
}

#define Make_Array_Core(capacity,flags) \
    Make_Array_Core_Into(Alloc_Stub(), (capacity), (flags))

#define Make_Array(capacity) \
    Make_Array_Core((capacity), ARRAY_MASK_HAS_FILE_LINE)

// !!! Currently, many bits of code that make copies don't specify if they are
// copying an array to turn it into a paramlist or varlist, or to use as the
// kind of array the use might see.  If we used plain Make_Array() then it
// would add a flag saying there were line numbers available, which may
// compete with the usage of the ->misc and ->link fields of the series node
// for internal arrays.
//
INLINE Array* Make_Array_For_Copy(
    REBLEN capacity,
    Flags flags,
    const Array* original
){
    if (original and Has_Newline_At_Tail(original)) {
        //
        // All of the newline bits for cells get copied, so it only makes
        // sense that the bit for newline on the tail would be copied too.
        //
        flags |= ARRAY_FLAG_NEWLINE_AT_TAIL;
    }

    if (
        Flavor_From_Flags(flags) == FLAVOR_ARRAY
        and (flags & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED)
        and (original and Has_File_Line(original))
    ){
        Array* a = Make_Array_Core(
            capacity,
            flags & ~ARRAY_FLAG_HAS_FILE_LINE_UNMASKED
        );
        LINK(Filename, a) = LINK(Filename, original);
        a->misc.line = original->misc.line;
        Set_Array_Flag(a, HAS_FILE_LINE_UNMASKED);
        return a;
    }

    return Make_Array_Core(capacity, flags);
}


// A singular array is specifically optimized to hold *one* value in the
// series Stub directly, and stay fixed at that size.
//
// Note Array_Single() must be overwritten by the caller...it contains an end
// marker but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need ARRAY_FLAG_HAS_FILE_LINE.
//
INLINE Array* Alloc_Singular(Flags flags) {
    assert(not (flags & SERIES_FLAG_DYNAMIC));
    Array* a = Make_Array_Core(1, flags | SERIES_FLAG_FIXED_SIZE);
    Erase_Cell(Stub_Cell(a));  // poison means length 0, erased length 1
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
        Cell_Array(v), VAL_INDEX(v), Cell_Specifier(v), 0, SERIES_FLAGS_NONE)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0, SERIES_FLAGS_NONE)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e), SERIES_FLAGS_NONE)

// See TS_NOT_COPIED for the default types excluded from being deep copied
//
INLINE Array* Copy_Array_At_Extra_Deep_Flags_Managed(
    const Array* original, // ^-- not macro because original mentioned twice
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
        flags, // note no ARRAY_HAS_FILE_LINE by default
        TS_SERIES & ~TS_NOT_COPIED // types
    );
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-ARRAY! (uses `struct AnyUnion_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See %sys-bind.h
//

#define EMPTY_BLOCK \
    Root_Empty_Block

#define EMPTY_ARRAY \
    PG_Empty_Array // Note: initialized from Cell_Array(Root_Empty_Block)


// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
INLINE const Array* Cell_Array(NoQuote(const Cell*) v) {
    assert(Any_Arraylike(v));
    assert(Is_Node_A_Stub(Cell_Node1(v)));  // not a pairing arraylike!

    const Array* a = cast(Array*, Cell_Node1(v));
    if (Get_Series_Flag(a, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return a;
}

#define Cell_Array_Ensure_Mutable(v) \
    m_cast(Array*, Cell_Array(Ensure_Mutable(v)))

#define Cell_Array_Known_Mutable(v) \
    m_cast(Array*, Cell_Array(Known_Mutable(v)))


#define PAIRING_LEN 2

#define Pairing_Second(paired) \
    (ensure(const Cell*, (paired)) + 1)

#define Pairing_Tail(paired) \
    (ensure(const Cell*, (paired)) + 2)


// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
// IMPORTANT: This routine will trigger a failure if the array index is out
// of bounds of the data.  If a function can deal with such out of bounds
// arrays meaningfully, it should work with VAL_INDEX_UNBOUNDED().
//
INLINE const Cell* Cell_Array_Len_At(
    Option(REBLEN*) len_at_out,
    NoQuote(const Cell*) v
){
    const Node* node = Cell_Node1(v);
    if (Is_Node_A_Cell(node)) {
        assert(Any_Sequence_Kind(Cell_Heart(v)));
        assert(VAL_INDEX_RAW(v) == 0);
        if (len_at_out)
            *unwrap(len_at_out) = PAIRING_LEN;
        return c_cast(Cell*, node);
    }
    const Array* arr = c_cast(Array*, node);
    REBIDX i = VAL_INDEX_RAW(v);  // Cell_Array() already checks it's series
    REBLEN len = Array_Len(arr);
    if (i < 0 or i > cast(REBIDX, len))
        fail (Error_Index_Out_Of_Range_Raw());
    if (len_at_out)  // inlining should remove this if() for Cell_Array_At()
        *unwrap(len_at_out) = len - i;
    return Array_At(arr, i);
}

INLINE const Cell* Cell_Array_At(
    Option(const Cell**) tail_out,
    NoQuote(const Cell*) v
){
    const Node* node = Cell_Node1(v);
    if (Is_Node_A_Cell(node)) {
        assert(Any_Sequence_Kind(Cell_Heart(v)));
        assert(VAL_INDEX_RAW(v) == 0);
        const Cell* cell = c_cast(Cell*, node);
        if (tail_out)
            *unwrap(tail_out) = Pairing_Tail(cell);
        return cell;
    }
    const Array* arr = c_cast(Array*, node);
    REBIDX i = VAL_INDEX_RAW(v);  // Cell_Array() already checks it's arraylike
    REBLEN len = Array_Len(arr);
    if (i < 0 or i > cast(REBIDX, len))
        fail (Error_Index_Out_Of_Range_Raw());
    const Cell* at = Array_At(arr, i);
    if (tail_out)  // inlining should remove this if() for no tail
        *unwrap(tail_out) = at + (len - i);
    return at;
}

INLINE const Cell* Cell_Array_Item_At(NoQuote(const Cell*) v) {
    const Cell* tail;
    const Cell* item = Cell_Array_At(&tail, v);
    assert(item != tail);  // should be a valid value
    return item;
}


#define Cell_Array_At_Ensure_Mutable(tail_out,v) \
    m_cast(Cell*, Cell_Array_At((tail_out), Ensure_Mutable(v)))

#define Cell_Array_At_Known_Mutable(tail_out,v) \
    m_cast(Cell*, Cell_Array_At((tail_out), Known_Mutable(v)))


// !!! R3-Alpha introduced concepts of immutable series with PROTECT, but
// did not consider the protected status to apply to binding.  Ren-C added
// more notions of immutability (const, holds, locking/freezing) and enforces
// it at compile-time...which caught many bugs.  But being able to bind
// "immutable" data was mechanically required by R3-Alpha for efficiency...so
// new answers will be needed.  See Virtual_Bind_Deep_To_New_Context() for
// some of the thinking on this topic.  Until it's solved, binding-related
// calls to this function get mutable access on non-mutable series.  :-/
//
#define Cell_Array_At_Mutable_Hack(tail_out,v) \
    m_cast(Cell*, Cell_Array_At((tail_out), (v)))


//=//// ANY-ARRAY! INITIALIZER HELPERS ////////////////////////////////////=//
//
// Declaring as inline with type signature ensures you use a Array* to
// initialize, and the C++ build can also validate managed consistent w/const.

INLINE REBVAL *Init_Array_Cell_At_Core(
    Cell* out,
    enum Reb_Kind kind,
    const_if_c Array* array,
    REBLEN index,
    Array* binding
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
    INLINE REBVAL *Init_Array_Cell_At_Core(
        Cell* out,
        enum Reb_Kind kind,
        const Array* array,  // all const arrays should be already managed
        REBLEN index,
        Array* binding
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


INLINE Cell* Init_Relative_Block_At(
    Cell* out,
    Action* action,  // action to which array has relative bindings
    Array* array,
    REBLEN index
){
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_BLOCK);
    Init_Cell_Node1(out, array);
    VAL_INDEX_RAW(out) = index;
    INIT_SPECIFIER(out, action);
    return out;
}

#define Init_Relative_Block(out,action,array) \
    Init_Relative_Block_At((out), (action), (array), 0)


#ifdef NDEBUG
    #define Assert_Array(s)     NOOP
    #define Assert_Series(s)    NOOP
#else
    #define Assert_Array(s) \
        Assert_Array_Core(s)

    INLINE void Assert_Series(const Series* s) {
        if (Is_Series_Array(s))
            Assert_Array_Core(c_cast(Array*, s));  // calls _Series_Basics()
        else
            Assert_Series_Basics_Core(s);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (Array_Len(a) != 0 and (v) >= Array_Head(a) and (v) < Array_Tail(a))
#endif


// Checks if ANY-GROUP! is like ((...)), useful for dialects--though the
// uses of this have all been replaced at time of writing.
//
// https://forum.rebol.info/t/doubled-groups-as-a-dialecting-tool/1893
//
INLINE bool Is_Any_Doubled_Group(NoQuote(const Cell*) group) {
    assert(Any_Group_Kind(Cell_Heart(group)));
    const Cell* tail;
    const Cell* inner = Cell_Array_At(&tail, group);
    if (inner + 1 != tail)  // should be exactly one item
        return false;
    return Is_Group(inner);  // if true, it's a ((...)) GROUP!
}


//=//// "PACKS" (BLOCK! Isotopes) /////////////////////////////////////////=//
//
// BLOCK! isotopes are exploited as a mechanism for bundling values in a way
// that they can be passed around as a single value.  They are leveraged in
// particular for multi-return, because a SET-WORD! will unpack only the
// first item, while a SET-BLOCK! will unpack others.
//
//      >> pack [<a> <b>]
//      == ~['<a> '<b>]~  ; isotope
//
//      >> x: pack [<a> <b>]
//      == <a>
//
//      >> [x y]: pack [<a> <b>]
//      == <a>
//
//      >> x
//      == <a>
//
//      >> y
//      == <b>
//

INLINE Value(*) Init_Pack_Untracked(Atom(*) out, Array* a) {
    Init_Block(out, a);
    QUOTE_BYTE(out) = ISOTOPE_0;
    return cast(Value(*), out);  // Note: Is_Isotope_Unstable(out)
}

#define Init_Pack(out,a) \
    TRACK(Init_Pack_Untracked((out), (a)))


//=//// "NIHIL" (empty BLOCK! Isotope Pack, ~[]~) /////////////////////////=//
//
// This unstable isotope is used in situations that want to convey a full
// absence of values (e.g. ELIDE).  It can't be used in assignments, and if
// the evaluator encounters one in an interstitial context it will be
// vaporized.  It is sensibly represented as a parameter pack of length 0.
//

#define Init_Nihil_Untracked(out) \
    Init_Pack_Untracked((out), EMPTY_ARRAY)

#define Init_Nihil(out) \
    TRACK(Init_Nihil_Untracked(out))

INLINE bool Is_Nihil(Atom(const*) v) {
    if (not Is_Pack(v))
        return false;
    const Cell* tail;
    const Cell* at = Cell_Array_At(&tail, v);
    return tail == at;
}

INLINE bool Is_Meta_Of_Nihil(const Cell* v) {
    if (not Is_Meta_Of_Pack(v))
        return false;
    const Cell* tail;
    const Cell* at = Cell_Array_At(&tail, v);
    return tail == at;
}


//=//// "SPLICES" (GROUP! Isotopes) ///////////////////////////////////////=//
//
// Group isotopes are understood by routines like APPEND/INSERT/CHANGE to
// mean that you intend to splice their content (the default is to append
// as-is, which is changed from Rebol2/Red).  The typical way of making these
// isotopes is the SPREAD function.
//
//    >> append [a b c] [d e]
//    == [a b c] [d e]
//
//    >> spread [d e]
//    == ~(d e)~  ; isotope
//
//    >> append [a b c] ~(d e)~
//    == [a b c d e]
//

INLINE Value(*) Splicify(Value(*) v) {
    assert(Any_Array(v) and QUOTE_BYTE(v) == UNQUOTED_1);
    QUOTE_BYTE(v) = ISOTOPE_0;
    HEART_BYTE(v) = REB_GROUP;
    return v;
}

INLINE Value(*) Init_Splice_Untracked(Value(*) out, Array* a) {
    Init_Group(out, a);
    QUOTE_BYTE(out) = ISOTOPE_0;
    return out;
}

#define Init_Splice(out,a) \
    TRACK(Init_Splice_Untracked((out), (a)))
