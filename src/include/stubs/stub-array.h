//
//  File: %stub-array.h
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
//   (Note: The debug build may put "poison" at the tail position whenever
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
    Set_Subclass_Flag(ARRAY, ensure(const Array*, (a)), flag)

#define Clear_Array_Flag(a,flag) \
    Clear_Subclass_Flag(ARRAY, ensure(const Array*, (a)), flag)


INLINE bool Has_Newline_At_Tail(const Array* a) {
    if (Series_Flavor(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->leader.bits & ARRAY_FLAG_NEWLINE_AT_TAIL);
}

INLINE bool Has_File_Line(const Array* a) {
    if (Series_Flavor(a) != FLAVOR_ARRAY)
        return false;  // only plain arrays can have newlines

    // Using Get_Subclass_Flag() would redundantly check it's a plain array.
    //
    return did (a->leader.bits & ARRAY_FLAG_HAS_FILE_LINE_UNMASKED);
}


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  Since
// empty arrays have no "last" value Array_Last() should not be called on it.

#define Array_At(a,n)           Series_At(Element, (a), (n))
#define Array_Head(a)           Series_Head(Element, (a))
#define Array_Tail(a)           Series_Tail(Element, (a))
#define Array_Last(a)           Series_Last(Element, (a))

INLINE Value* Stub_Cell(const_if_c Series* s) {
    assert(Not_Series_Flag(s, DYNAMIC));
    assert(Is_Series_Array(s));
    return x_cast(Value*, &s->content.fixed.cell);
}

#if CPLUSPLUS_11
    INLINE const Value* Stub_Cell(const Stub* s) {
        assert(Not_Series_Flag(s, DYNAMIC));
        assert(Is_Series_Array(s));
        return u_cast(const Value*, &s->content.fixed.cell);
    }
#endif


// It's possible to calculate the array from just a cell if you know it's a
// cell living in a singular array.
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
// Note Stub_Cell() must be overwritten by the caller...it contains an erased
// cell but the array length is 1, so that will assert if you don't.
//
// For `flags`, be sure to consider if you need ARRAY_FLAG_HAS_FILE_LINE.
//
INLINE Array* Alloc_Singular(Flags flags) {
    assert(not (flags & SERIES_FLAG_DYNAMIC));
    Array* a = x_cast(Array*, Make_Series_Into(
        Alloc_Stub(),
        1,
        flags | SERIES_FLAG_FIXED_SIZE
    ));
    assert(Is_Series_Array(a));  // flavor should have been an array flavor
    Erase_Cell(Stub_Cell(a));  // poison means length 0, erased length 1
    return a;
}

#define Append_Value(a,v) \
    cast(Element*, Copy_Relative_internal(Alloc_Tail_Array(a), (v)))

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


#define Copy_Values_Len_Shallow(v,l) \
    Copy_Values_Len_Extra_Shallow_Core((v), (l), 0, 0)

#define Copy_Values_Len_Shallow_Core(v,l,f) \
    Copy_Values_Len_Extra_Shallow_Core((v), (l), 0, (f))


#define Copy_Array_Shallow(a) \
    Copy_Array_At_Shallow((a), 0)

#define Copy_Array_Shallow_Flags(a,f) \
    Copy_Array_At_Extra_Shallow((a), 0, 0, (f))

#define Copy_Array_At_Shallow(a,i) \
    Copy_Array_At_Extra_Shallow((a), (i), 0, SERIES_FLAGS_NONE)

#define Copy_Array_Extra_Shallow(a,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (e), SERIES_FLAGS_NONE)


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
