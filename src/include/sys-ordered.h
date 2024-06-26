//
//  File: %sys-ordered.h
//  Summary: "Order-dependent type macros"
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


#define FLAGIT_KIND(t) \
    (u_cast(uint_fast64_t, 1) << (t))  // makes a 64-bit bitflag


//=//// EXTRA NEEDING GC MARK /////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the quote byte do not affect it.

#define Is_Extra_Mark_Kind(k) \
    ((k) >= REB_VARARGS)

#define Cell_Extra_Needs_Mark(v) \
    Is_Extra_Mark_Kind(HEART_BYTE(v))  // READABLE() checked elsewhere


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the quote byte do not affect it.

INLINE bool Is_Bindable_Heart(Heart h)
  { return h >= REB_WORD; }

#define Is_Bindable(v) \
    Is_Bindable_Heart(Cell_Heart_Unchecked(v))  // READABLE() checked elsewhere

INLINE bool Bindable_Heart_Is_Any_Word(Heart heart) {
    assert(heart >= REB_WORD);  // inlined Is_Bindable_Heart()
    return heart < REB_TUPLE;
}

INLINE bool Bindable_Heart_Is_Any_Array(Heart heart) {
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
#define Any_Meta_Kind Any_Meta_Value_Kind
#define Any_The_Kind Any_The_Value_Kind
#define Any_Plain_Kind Any_Plain_Value_Kind
#define Any_Var_Kind Any_Var_Value_Kind


//=//// SIGIL EXTRACTION //////////////////////////////////////////////////=//

INLINE Option(Sigil) Sigil_Of_Any_Word_Kind(Byte k) {
    assert(Any_Word_Kind(k));
    return cast(Sigil, k - REB_WORD);
}

INLINE Option(Sigil) Sigil_Of_Any_Tuple_Kind(Byte k) {
    assert(Any_Tuple_Kind(k));
    return cast(Sigil, k - REB_TUPLE);
}

INLINE Option(Sigil) Sigil_Of_Any_Path_Kind(Byte k) {
    assert(Any_Path_Kind(k));
    return cast(Sigil, k - REB_PATH);
}

INLINE Option(Sigil) Sigil_Of_Any_Block_Kind(Byte k) {
    assert(Any_Block_Kind(k));
    return cast(Sigil, k - REB_BLOCK);
}

INLINE Option(Sigil) Sigil_Of_Any_Group_Kind(Byte k) {
    assert(Any_Group_Kind(k));
    return cast(Sigil, k - REB_GROUP);
}


//=//// SIGIL TRANSFORMATION //////////////////////////////////////////////=//

INLINE Heart Sigilize_Any_Plain_Kind(Sigil sigil, Byte k) {
    assert(Any_Plain_Kind(k));
    return cast(Heart, k + u_cast(Byte, sigil));
}

INLINE Heart Plainify_Any_Set_Kind(Byte k) {
    assert(Any_Set_Kind(k));
    return cast(Heart, k - 1);
}

INLINE Heart Plainify_Any_Get_Kind(Byte k) {
    assert(Any_Get_Kind(k));
    return cast(Heart, k - 2);
}

INLINE Heart Plainify_Any_Meta_Kind(Byte k) {
    assert(Any_Meta_Kind(k));
    return cast(Heart, k - 3);
}

INLINE Heart Plainify_Any_The_Kind(Byte k) {
    assert(Any_The_Kind(k));
    return cast(Heart, k - 5);
}

INLINE Heart Plainify_Any_Var_Kind(Byte k) {
    assert(Any_Var_Kind(k));
    return cast(Heart, k - 6);
}
