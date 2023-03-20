//
//  File: %sys-ordered.h
//  Summary: "Order-dependent type macros"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// * Some of the tests are bitflag based.  This makes Rebol require a 64-bit
//   integer, so tricks that would not require it for building would be good.
//   (For instance, if all the types being tested fit in a 32-bit range.)
//
// * There was a historical linkage between the order of types and the
//   TOKEN_XXX values.  That might be interesting to exploit for an
//   optimization in the future...see notes on the tokens regarding this.


#define FLAGIT_KIND(t) \
    (cast(uint_fast64_t, 1) << (t)) // makes a 64-bit bitflag


//=//// BINDABILITY ///////////////////////////////////////////////////////=//
//
// Note that the HEART_BYTE() is what is being tested--e.g. the type that the
// cell payload and extra actually are *for*.  QUOTED! and QUASI! indicators
// in the quote byte do not affect it.

#define IS_BINDABLE_KIND(k) \
    ((k) >= REB_OBJECT)

#define Is_Bindable(v) \
    IS_BINDABLE_KIND(HEART_BYTE_UNCHECKED(v))  // checked elsewhere


//=//// INERTNESS ////////////////////////////////////////////////////////=//
//
// All the inert types are grouped together to make this test fast.

inline static bool ANY_INERT_KIND(Byte k) {
    assert(k != REB_VOID);  // don't call on void (0 in enum, breaks pattern)
    return k <= REB_BLOCK;
}

inline static bool ANY_VALUE_KIND(Byte k)
  { return k != REB_NULL and k != REB_LOGIC; }

#define ANY_VALUE(v) \
    ANY_VALUE_KIND(VAL_TYPE(v))

#define ANY_INERT(v) \
    ANY_INERT_KIND(VAL_TYPE(v))

#define ANY_EVALUATIVE(v) \
    (not ANY_INERT_KIND(VAL_TYPE(v)))


//=//// SHORTHANDS ////////////////////////////////////////////////////////=//
//
// Easier to define these than to try and write code for the exceptions when
// you want them (sometimes you want `value`, sometimes you don't)
//

#define ANY_GET_KIND ANY_GET_VALUE_KIND
#define ANY_SET_KIND ANY_SET_VALUE_KIND
#define ANY_META_KIND ANY_META_VALUE_KIND
#define ANY_PLAIN_KIND ANY_PLAIN_VALUE_KIND


//=//// XXX <=> SET-XXX! <=> GET-XXX! TRANSFORMATION //////////////////////=//
//
// See reasoning in %types.r on why ANY-INERT! optimization is favored over
// putting blocks/paths/words/tuples/groups together.  It means ANY_ARRAY() is
// slower but these tests can be faster.

inline static enum Reb_Kind PLAINIFY_ANY_GET_KIND(Byte k) {
    assert(ANY_GET_KIND(k));
    return cast(enum Reb_Kind, k - 10);
}

inline static enum Reb_Kind PLAINIFY_ANY_SET_KIND(Byte k) {
    assert(ANY_SET_KIND(k));
    return cast(enum Reb_Kind, k - 5);
}

inline static enum Reb_Kind PLAINIFY_ANY_META_KIND(Byte k) {
    assert(ANY_META_KIND(k));
    return cast(enum Reb_Kind, k - 15);
}

inline static enum Reb_Kind SETIFY_ANY_PLAIN_KIND(Byte k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k + 5);
}

inline static enum Reb_Kind GETIFY_ANY_PLAIN_KIND(Byte k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k + 10);
}

inline static enum Reb_Kind METAFY_ANY_PLAIN_KIND(Byte k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k + 15);
}

inline static enum Reb_Kind THEIFY_ANY_PLAIN_KIND(Byte k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k - 10);
}

inline static enum Reb_Kind TYPEIFY_ANY_PLAIN_KIND(Byte k) {
    assert(ANY_PLAIN_KIND(k));
    return cast(enum Reb_Kind, k - 5);
}


//=//// SET-WORD! <=> SET-PATH! <=> SET-BLOCK! TRANSFORMATION /////////////=//
//
// This keeps the PLAIN/GET/SET/SYM class the same, changes the type.
//
// Order is: block, group, path, word.

inline static enum Reb_Kind WORDIFY_KIND(Byte k) {
    if (ANY_BLOCK_KIND(k))
        return cast(enum Reb_Kind, k + 3);
    if (ANY_GROUP_KIND(k))
        return cast(enum Reb_Kind, k + 2);
    if (ANY_PATH_KIND(k))
        return cast(enum Reb_Kind, k + 1);
    assert(ANY_WORD_KIND(k));
    return cast(enum Reb_Kind, k);
}

inline static enum Reb_Kind PATHIFY_KIND(Byte k) {
    if (ANY_BLOCK_KIND(k))
        return cast(enum Reb_Kind, k + 2);
    if (ANY_GROUP_KIND(k))
        return cast(enum Reb_Kind, k + 1);
    if (ANY_PATH_KIND(k))
        return cast(enum Reb_Kind, k);
    assert(ANY_WORD_KIND(k));
    return cast(enum Reb_Kind, k - 1);
}

inline static enum Reb_Kind GROUPIFY_KIND(Byte k) {
    if (ANY_BLOCK_KIND(k))
        return cast(enum Reb_Kind, k + 1);
    if (ANY_GROUP_KIND(k))
        return cast(enum Reb_Kind, k);
    if (ANY_PATH_KIND(k))
        return cast(enum Reb_Kind, k - 1);
    assert(ANY_WORD_KIND(k));
    return cast(enum Reb_Kind, k - 2);
}

inline static enum Reb_Kind BLOCKIFY_KIND(Byte k) {
    if (ANY_BLOCK_KIND(k))
        return cast(enum Reb_Kind, k);
    if (ANY_GROUP_KIND(k))
        return cast(enum Reb_Kind, k - 1);
    if (ANY_PATH_KIND(k))
        return cast(enum Reb_Kind, k - 2);
    assert(ANY_WORD_KIND(k));
    return cast(enum Reb_Kind, k - 3);
}
