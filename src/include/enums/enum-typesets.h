//
//  File: %enum-typesets.h
//  Summary: "Optimized built in typesets and order-dependent type macros"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// detail--and the specific integer values of things like REB_BLOCK should
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
// indicators in the quote byte do not affect it.

#define Is_Extra_Mark_Heart(k) \
    ((k) >= REB_VARARGS)

#define Cell_Extra_Needs_Mark(v) \
    Is_Extra_Mark_Heart(HEART_BYTE(v))  // readable checked elsewhere


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the quote byte do not affect it.
//
// 1. There's a range check created automatically for ANY-BINDABLE?, and
//    that's good for fitting into the typeset optimization cases.  But it
//    requires two comparisons, and due to careful organization of %types.r
//    this particular check can be accomplished in the core code with a single
//    comparison.  Is_Bindable() was the historical name of the function
//    and reads a bit beter than Any_Bindable().

INLINE bool Is_Bindable_Heart(Heart h)
  { return h >= REB_WORD; }

#undef Any_Bindable  // use Is_Bindable(), faster than a range check [1]

#define Is_Bindable(v) \
    Is_Bindable_Heart(Cell_Heart_Unchecked(v))  // readable checked elsewhere

INLINE bool Bindable_Heart_Is_Any_Word(Heart heart) {
    assert(heart >= REB_WORD);  // inlined Is_Bindable_Heart()
    return heart < REB_TUPLE;
}

INLINE bool Bindable_Heart_Is_Any_List(Heart heart) {
    assert(heart >= REB_WORD);  // inlined Is_Bindable_Heart()
    return heart >= REB_BLOCK;
}


//=//// SHORTHANDS ////////////////////////////////////////////////////////=//
//
// Easier to define these than to try and write code for the exceptions when
// you want them (sometimes you want `value`, sometimes you don't)
//

#define Any_Get_Kind Any_Get_Value_Kind
#define Any_Set_Kind Any_Set_Value_Kind
#define Any_Type_Kind Any_Type_Value_Kind
#define Any_Meta_Kind Any_Meta_Value_Kind
#define Any_The_Kind Any_The_Value_Kind
#define Any_Plain_Kind Any_Plain_Value_Kind
#define Any_Var_Kind Any_Var_Value_Kind


//=//// SIGIL TRANSFORMATION //////////////////////////////////////////////=//

INLINE Heart Sigilize_Any_Plain_Kind(Sigil sigil, Byte k) {
    assert(Any_Plain_Kind(k));
    assert(sigil != SIGIL_0 and sigil < SIGIL_QUOTE);
    return cast(Heart, k + u_cast(Byte, sigil));
}

INLINE Heart Plainify_Any_Meta_Kind(Byte k) {
    assert(Any_Meta_Kind(k));
    return cast(Heart, k - 1);
}

INLINE Heart Plainify_Any_Type_Kind(Byte k) {
    assert(Any_Meta_Kind(k));
    return cast(Heart, k - 2);
}

INLINE Heart Plainify_Any_The_Kind(Byte k) {
    assert(Any_The_Kind(k));
    return cast(Heart, k - 3);
}

INLINE Heart Plainify_Any_Var_Kind(Byte k) {
    assert(Any_Var_Kind(k));
    return cast(Heart, k - 4);
}


INLINE bool Any_Sequence_Or_List_Kind(Byte k)  // !!! optimize?
  { return Any_Sequence_Kind(k) or Any_List_Kind(k); }

INLINE bool Any_Bytes_Kind(Byte k)
  { return Any_Utf8_Kind(k) or k == REB_BLOB; }
