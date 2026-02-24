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


// This needs to be defined prior to the %cast-cells.h file, so that it can
// make sure you don't cast an unstable antiform to a Stable*.  This is before
// the inclusion of Option() so it doesn't use Heart_Of(), which is good
// for efficiency even if it looks a bit weird.
//
// !!! Would this be faster as a flag in g_sparse_memberships[]?  Test that.
//
INLINE bool Is_Stable_Antiform_Heart(Heart heart) {
    assert(Any_Isotopic_Heart(heart));
    return (
        heart == HEART_WORD_SIGNIFYING_LOGIC
        or heart == HEART_BLOCK_SIGNIFYING_SPLICE
        or heart == HEART_FENCE_SIGNIFYING_DATATYPE
    );
}

#define Not_Stable_Antiform_Heart(heart) \
    (not Is_Stable_Antiform_Heart(heart))


//=//// EXTRA NEEDING GC MARK /////////////////////////////////////////////=//
//
// Note that the Heart_Of() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the TYPE_BYTE() do not affect it.

#define Heart_Implies_Extra_Needs_Mark(opt_heart) \
    ((opt opt_heart) >= HEART_VARARGS)


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that the HEARTSIGIL_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  Quoted/quasiform/antiform
// indicators in the TYPE_BYTE() do not affect it.
//
// 1. To make this macro fast, we assume the caller passes in Option(Heart)
//    and use compile-time-ensure to avoid an inline function.
//
// 2. A range check for ANY-BINDABLE? would require two comparisons.  But due
//    to careful organization of %types.r, this particular check can be
//    accomplished in the core code with a single comparison.

#define Is_Bindable_Heart(opt_heart) /* assume Option(Heart) [1] */ \
    (known(Heart, (opt opt_heart)) >= HEART_BLANK)  // only one test [2]

#define Is_Cell_Bindable(elem) \
    Is_Bindable_Heart(Unchecked_Heart_Of(Known_Element(elem)))


//=//// MISC /////////////////////////////////////////////////////////////=//

INLINE bool Any_Sequence_Or_List_Heart(Option(Heart) h)  // !!! optimize?
  { return Any_Sequence_Heart(h) or Any_List_Heart(h); }

INLINE bool Any_Bytes_Type(Option(Type) t)
  { return Any_Utf8_Type(t) or t == TYPE_BLOB; }

INLINE bool Any_Bytes_Heart(Option(Heart) h)
  { return Any_Utf8_Heart(h) or h == HEART_BLOB; }
