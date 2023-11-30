//
//  File: %struct-array.h
//  Summary: "Array structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// Arrays are series whose element type is Cell.  The garbage collector has to
// treat them specially, by visiting the cells and marking the pointers inside
// those cells as live.
//
// There are several subclasses (FLAVOR_XXX) whose elements are value cells,
// and hence are arrays.  However the "plain" array, e.g. the kind used in
// BLOCK!s and GROUP!s, is its own subclass...which interprets the subclass
// bits in particular ways not relevant to other arrays (e.g. object variable
// lists do not need a flag tracking if there's a newline that needs to be
// output at the end of the varlist).
//

// The C++ build derives Array from Series...so it allows passing an array to
// a function that expects a series, but not vice-versa.  In the C build, an
// Array* and Series* are the same type, so it doesn't get those checks.
//
#if CPLUSPLUS_11
    struct Array : public Series {};
#else
    typedef Series Array;
#endif


//=//// ARRAY_FLAG_HAS_FILE_LINE_UNMASKED /////////////////////////////////=//
//
// The Series node has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the keylist for an object,
// the C code that runs as the dispatcher for a function, etc.)  But for
// regular source series, they can be used to store the filename and line
// number, if applicable.
//
// Only arrays preserve file and line info, as UTF-8 strings need to use the
// ->misc and ->link fields for caching purposes in strings.
//
#define ARRAY_FLAG_HAS_FILE_LINE_UNMASKED \
    SERIES_FLAG_24

#define ARRAY_MASK_HAS_FILE_LINE \
    (ARRAY_FLAG_HAS_FILE_LINE_UNMASKED | SERIES_FLAG_LINK_NODE_NEEDS_MARK)

#define LINK_Filename_TYPE          const String*
#define HAS_LINK_Filename           FLAVOR_ARRAY


//=//// ARRAY_FLAG_25 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_25 \
    SERIES_FLAG_25


//=//// ARRAY_FLAG_26 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_26 \
    SERIES_FLAG_26


//=//// ARRAY_FLAG_27 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_27 \
    SERIES_FLAG_27


//=//// ARRAY_FLAG_28 /////////////////////////////////////////////////////=//
//
#define ARRAY_FLAG_28 \
    SERIES_FLAG_28


//=//// ARRAY_FLAG_CONST_SHALLOW //////////////////////////////////////////=//
//
// When a COPY is made of an ANY-ARRAY! that has CELL_FLAG_CONST, the new
// value shouldn't be const, as the goal of copying it is generally to modify.
// However, if you don't copy it deeply, then mere copying should not be
// giving write access to levels underneath it that would have been seen as
// const if they were PICK'd out before.  This flag tells the copy operation
// to mark any cells that are shallow references as const.  For convenience
// it is the same bit as the const flag one would find in the value.
//
#define ARRAY_FLAG_CONST_SHALLOW \
    SERIES_FLAG_30
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
    SERIES_FLAG_31
