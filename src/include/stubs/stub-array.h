//
//  file: %stub-array.h
//  summary: "Definitions for the Array Flex subclass"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// A "Rebol Array" is a Flex of Rebol Cells.  Every BLOCK! or GROUP! points
// at an Array Flex, which you see in the source as Array*.
//
// While many Array operations are shared in common with Flex, there are a
// few (deliberate) type incompatibilities introduced.  This incompatibility
// is only noticed when building as C++, and draws attention to operations
// that make sense on things like string but maybe not on array.
//
// An Array is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "binding" FRAME!
// context (which is also required to do a GET_VAR lookup).
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
//  * In R3-Alpha, there was a full-sized cell at the end of every array that
//    would hold an END signal--much like a string terminator.  Ren-C does not
//    terminate Arrays but relies on the known length, in order to save on
//    space.  This also avoids the cost of keeping the terminator up to date
//    as the Array grows or resizes.
//
//   (Note: The checked build may put "poison" at the tail position whenever
//    the Array size is updated, to make it easier to catch out-of-bounds
//    access.  But the release build does not do this)


// HEAD, TAIL, and LAST refer to specific value pointers in the array.  Since
// empty arrays have no "last" value Array_Last() should not be called on it.
//
// NOTE: These return unchecked pointers, because the data it points to may
// be uninitilized.  Even if an array is valid, the Array_Head() pointer may
// be to the tail, and hence not valid.  This means it's not really viable
// to make checked versions of these functions...you just have to know at
// the callsite if you expect the data to be valid or not, and use Element*
// or Sink/Init(Element) as appropriate.

#define Array_At(a,n)           Flex_At(Element, (a), (n))
#define Array_Head(a)           Flex_Head(Element, (a))
#define Array_Tail(a)           Flex_Tail(Element, (a))
#define Array_Last(a)           Flex_Last(Element, (a))

#define Array_Len(a) \
    Flex_Used(ensure(Array*, (a)))


// See Ensure_Readable(), Ensure_Writable() and related functions for an
// explanation of bits that are formatted in cell headers to be legal to use.
//
// 1. Expandable arrays prep all cells, including in the not-yet-used
//    capacity.  Otherwise you'd waste time un-poisoning cells on every
//    expansion and poisoning them again on every shrink.  Trust that the
//    DEBUG_POISON_FLEX_TAILS is good enough.
//
INLINE void Prep_Array(
    Array* a,
    REBLEN capacity  // Expand_Flex passes 0 on dynamic reallocation
){
    assert(Get_Stub_Flag(a, DYNAMIC));

  #if NO_RUNTIME_CHECKS  // see Assert_Cell_Initable() for why 0 headers ok
    UNUSED(capacity);  // branching for FIXED_SIZE test not worth cost
    memset(
        a->content.dynamic.data,
        0x00,
        a->content.dynamic.rest * sizeof(Cell)
    );
  #else
    Cell* prep = Array_Head(a);

    REBLEN n;
    for (n = 0; n < capacity; ++n, ++prep)
        Force_Erase_Cell(prep);  // 0 header, adds TRACK() info

    if (Get_Flex_Flag(a, FIXED_SIZE)) {  // can't expand, poison any excess
        for (; n < a->content.dynamic.rest; ++n, ++prep)
            Force_Poison_Cell(prep);  // unreadable + unwritable
    }
    else {  // array is expandable, so prep all cells [2]
        for (; n < a->content.dynamic.rest; ++n, ++prep)
            Force_Erase_Cell(prep);
    }

    #if DEBUG_POISON_FLEX_TAILS  // allocation deliberately oversized by 1
        Force_Poison_Cell(prep - 1);
    #endif
  #endif
}


INLINE Option(const Strand*) Link_Filename(const Source* source) {
    assert(Stub_Flavor(source) == FLAVOR_SOURCE);
    if (Get_Stub_Flag(source, LINK_NEEDS_MARK)) {
        const Strand* filename = cast(const Strand*,
            LINK_SOURCE_FILENAME_NODE(source)
        );
        assert(Stub_Flavor(filename) == FLAVOR_NONSYMBOL);
        return filename;
    }
    // source->link.base is corrupt/random... make it something known?
    return nullptr;
}

INLINE void Tweak_Link_Filename(Source* source, Option(const Strand*) filename)
{
    assert(Stub_Flavor(source) == FLAVOR_SOURCE);
    if (filename) {
        Set_Stub_Flag(source, LINK_NEEDS_MARK);
        LINK_SOURCE_FILENAME_NODE(source) = m_cast(Strand*, unwrap filename);
    }
    else {
        Clear_Stub_Flag(source, LINK_NEEDS_MARK);
        Corrupt_If_Needful(source->link.base);
    }
}


// Make an Array that is the right size to store Cells (and marked for the
// garbage collector to look into recursively).  Array_Len() will be 0.
//
// 1. Source arrays created at runtime default to inheriting the file and line
//    number from the array executing in the current frame.  (When code is
//    being scanned from UTF-8 source, the scanner will put the file and
//    line information on manually.)
//
INLINE Array* Make_Array_Core_Into(
    Flags flags,  // Make_Flex_Into() ensures not FLAVOR_0
    void* preallocated,
    REBLEN capacity
){
  #if DEBUG_POISON_FLEX_TAILS  // non-dynamic arrays poisoned by bit pattern
    if (capacity > 1 or (flags & STUB_FLAG_DYNAMIC))
        capacity += 1;  // account for space needed for poison cell
  #endif

    Array* a = u_cast(Array*, Make_Flex_Into(flags, preallocated, capacity));
    assert(Stub_Holds_Cells(a));  // flavor should have been an array flavor

    if (Get_Stub_Flag(a, DYNAMIC)) {
        Prep_Array(a, capacity);

      #if DEBUG_POISON_FLEX_TAILS
        Force_Poison_Cell(Array_Head(a));
      #endif
    }
    else {
        Force_Poison_Cell(Stub_Cell(a));  // optimized prep for 0 leng
    }

    if (Flavor_From_Flags(flags) == FLAVOR_SOURCE) {  // add file/line [1]
        Option(const Strand*) filename;
        if (
            not Level_Is_Variadic(TOP_LEVEL) and
            (filename = Link_Filename(Level_Array(TOP_LEVEL)))
        ){
            Tweak_Link_Filename(u_cast(Source*, a), filename);
            MISC_SOURCE_LINE(a) = MISC_SOURCE_LINE(Level_Array(TOP_LEVEL));
        }
    }

  #if DEBUG_COLLECT_STATS
    g_mem.blocks_made += 1;
  #endif

    assert(Array_Len(a) == 0);
    return a;
}

#define Make_Array_Core(flags, capacity) \
    Make_Array_Core_Into((flags), Alloc_Stub(), (capacity))


// A singular array is specifically optimized to hold *one* value in the
// Array Stub directly, and stay fixed at that size.
//
// Note Stub_Cell() must be overwritten by the caller...it contains an erased
// cell but the array length is 1, so that will assert if you don't.
//
INLINE Source* Alloc_Singular(Flags flags) {
    assert(Flavor_From_Flags(flags) == FLAVOR_SOURCE);
    assert(not (flags & STUB_FLAG_DYNAMIC));
    Source* a = u_cast(Source*, Make_Flex_Into(
        flags | FLEX_FLAG_FIXED_SIZE,
        Alloc_Stub(),
        1
    ));
    assert(Stub_Holds_Cells(a));  // flavor should have been an array flavor
    Force_Erase_Cell(Stub_Cell(a));  // poison len 0, erased len 1
    return a;
}

#define Append_Value(a,v) \
    Copy_Cell(Alloc_Tail_Array(a), (v))

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


#define Copy_Values_Len_Shallow(head,len) \
    cast(Source*, Copy_Values_Len_Extra_Shallow_Core( \
        STUB_MASK_UNMANAGED_SOURCE, (head), (len), 0))

#define Copy_Values_Len_Shallow_Core(flags,head,len) \
    Copy_Values_Len_Extra_Shallow_Core((flags), (head), (len), 0)


#define Copy_Array_Shallow(a) \
    Copy_Array_At_Shallow((a), 0)

#define Copy_Array_Shallow_Flags(f,a) \
    Copy_Array_At_Extra_Shallow((f),(a), 0, 0)

#define Copy_Array_At_Shallow(a,i) \
    cast(Source*, Copy_Array_At_Extra_Shallow( \
        STUB_MASK_UNMANAGED_SOURCE, (a), (i), 0))

#define Copy_Array_Extra_Shallow(a,e) \
    cast(Source*, Copy_Array_At_Extra_Shallow( \
        STUB_MASK_UNMANAGED_SOURCE, (a), 0, (e)))


#if NO_RUNTIME_CHECKS
    #define Assert_Array(a)     NOOP
    #define Assert_Flex(f)    NOOP
#else
    #define Assert_Array(a) \
        Assert_Array_Core(a)

    INLINE void Assert_Flex(const Flex* f) {
        if (Stub_Holds_Cells(f))
            Assert_Array_Core(cast(Array*, f));  // calls _Flex_Basics()
        else
            Assert_Flex_Basics_Core(f);
    }

    #define IS_VALUE_IN_ARRAY_DEBUG(a,v) \
        (Array_Len(a) != 0 and (v) >= Array_Head(a) and (v) < Array_Tail(a))
#endif
