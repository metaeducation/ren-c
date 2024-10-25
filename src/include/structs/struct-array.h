//
//  File: %struct-array.h
//  Summary: "Array structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// Arrays are Flexes whose element type is Cell.  Arrays have many concerns
// specific to them, including that the garbage collector has to treat them
// specially, by visiting the cells and marking the pointers in those cells
// as live.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Several important types (Action* for function, VarList* for context) are
//   actually stylized Arrays.  They are laid out with special values in their
//   content (e.g. at the [0] index), or by links to other Flexes in their
//   `->misc` and `->link` fields of the Flex Stub.
//
// * The default assumption of Array types is that they cannot hold antiforms.
//   So functions like Array_At() will return `Element*`.  However, there are
//   several subclasses of array with different FLAVOR_XXX bytes that can
//   store stable antiforms (none store unstable ones, at time of writing).
//
// * Another difference between the default array type (e.g. the one used by
//   BLOCK! and GROUP! and FENCE!) is that it has meaning for certain flex
//   flags, such as tracking whether a newline is at the end of the array.
//   Hence ARRAY_FLAG_XXX only applies to the FLAVOR_ARRAY default type,
//   not to things like the VarList of an OBJECT!.


// The C++ build derives Array from Flex...so it allows passing an Array to
// a function that expects a Flex, but not vice-versa.  In the C build, an
// Array* and Flex* are the same type, so it doesn't get those checks.
//
#if CPLUSPLUS_11
    struct Array : public Flex {};
#else
    typedef Flex Array;
#endif


//=//// ARRAY_FLAG_HAS_FILE_LINE_UNMASKED /////////////////////////////////=//
//
// The Flex Stub has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the KeyList for an object,
// the C code that runs as the dispatcher for an Action, etc.)  But for
// regular source Arrays, they can be used to store the filename and line
// number, if applicable.
//
// Only Array preserves file and line info, as UTF-8 Strings need to use the
// ->misc and ->link fields for caching purposes in String.
//
#define ARRAY_FLAG_HAS_FILE_LINE_UNMASKED \
    STUB_SUBCLASS_FLAG_24

#define ARRAY_MASK_HAS_FILE_LINE \
    (ARRAY_FLAG_HAS_FILE_LINE_UNMASKED | STUB_FLAG_LINK_NODE_NEEDS_MARK)

#define LINK_Filename_TYPE          const String*
#define HAS_LINK_Filename           FLAVOR_ARRAY


//=//// ARRAY_FLAG_25 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_25 \
    STUB_SUBCLASS_FLAG_25


//=//// ARRAY_FLAG_26 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_26 \
    STUB_SUBCLASS_FLAG_26


//=//// ARRAY_FLAG_27 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_27 \
    STUB_SUBCLASS_FLAG_27


//=//// ARRAY_FLAG_28 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_28 \
    STUB_SUBCLASS_FLAG_28


//=//// ARRAY_FLAG_CONST_SHALLOW //////////////////////////////////////////=//
//
// When a COPY is made of an ANY-LIST? that has CELL_FLAG_CONST, the new
// value shouldn't be const, as the goal of copying it is generally to modify.
// However, if you don't copy it deeply, then mere copying should not be
// giving write access to levels underneath it that would have been seen as
// const if they were PICK'd out before.  This flag tells the copy operation
// to mark any cells that are shallow references as const.  For convenience
// it is the same bit as the const flag one would find in the value.
//
#define ARRAY_FLAG_CONST_SHALLOW \
    STUB_SUBCLASS_FLAG_30
STATIC_ASSERT(ARRAY_FLAG_CONST_SHALLOW == CELL_FLAG_CONST);


//=//// ARRAY_FLAG_NEWLINE_AT_TAIL ////////////////////////////////////////=//
//
// The mechanics of how Rebol tracks newlines is that there is only one bit
// per value to track the property.  Yet since newlines are conceptually
// "between" values, that's one bit too few to represent all possibilities.
//
// Ren-C carries a bit for indicating when there's a newline intended at the
// tail of an array.
//
#define ARRAY_FLAG_NEWLINE_AT_TAIL \
    STUB_SUBCLASS_FLAG_31
