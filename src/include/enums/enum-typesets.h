//
//  file: %enum-typesets.h
//  summary: "Optimized built in typesets and order-dependent type macros"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2025 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The ordering of types in %types.r encodes properties of the types for
// efficiency.  So adding or removing a type generally means shuffling their
// values.  Hence their numbering is subject to change as an implementation
// detail--and the specific integer values of things like TYPE_BLOCK should
// never be exposed through the API.
//
// Many macros are generated automatically to do the range-based checks for
// types, but not everything is covered.  These are extra functions which
// embed specific knowledge of the type ordering.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * There was a historical linkage between the order of types and the
//   TOKEN_XXX values.  That might be interesting to exploit for an
//   optimization in the future...see notes on the tokens regarding this.


#include "tmp-typesets.h"


//=//// EXTRA NEEDING GC MARK /////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the LIFT_BYTE() do not affect it.

INLINE bool Is_Extra_Mark_Heart(Option(Heart) heart)
  { return (maybe heart) >= TYPE_VARARGS; }

#define Cell_Extra_Needs_Mark(cell) \
    Is_Extra_Mark_Heart(HEART_BYTE(cell))  // readable checked elsewhere


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the LIFT_BYTE() do not affect it.
//
// 1. There's a range check created automatically for ANY-BINDABLE?, and
//    that's good for fitting into the typeset optimization cases.  But it
//    requires two comparisons, and due to careful organization of %types.r
//    this particular check can be accomplished in the core code with a single
//    comparison.  Is_Bindable() was the historical name of the function
//    and reads a bit beter than Any_Bindable().

INLINE bool Is_Bindable_Heart(Option(Heart) h)
  { return (maybe h) >= TYPE_WORD; }

#define Is_Bindable_Heart_Byte(heart_byte) \
    (heart_byte >= u_cast(HeartByte, TYPE_WORD))  // fast macro!

#undef Any_Bindable  // use Is_Bindable(), faster than a range check [1]

#define Is_Bindable(v) \
    Is_Bindable_Heart(Unchecked_Heart_Of(v))  // readable checked elsewhere

INLINE bool Bindable_Heart_Is_Any_Word(Heart heart) {
    assert(heart >= TYPE_WORD);  // inlined Is_Bindable_Heart()
    return heart < TYPE_TUPLE;
}

INLINE bool Bindable_Heart_Is_Any_List(Heart heart) {
    assert(heart >= TYPE_WORD);  // inlined Is_Bindable_Heart()
    return heart >= TYPE_BLOCK;
}

#define Any_Fundamental(v) \
    (LIFT_BYTE(Ensure_Readable(v)) == NOQUOTE_1)


INLINE bool Any_Sequence_Or_List_Type(Option(Heart) h)  // !!! optimize?
  { return Any_Sequence_Type(h) or Any_List_Type(h); }

INLINE bool Any_Bytes_Type(Option(Heart) h)
  { return Any_Utf8_Type(h) or h == TYPE_BLOB; }
