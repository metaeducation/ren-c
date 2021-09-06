//
//  File: %sys-nulled.h
//  Summary: "NULL definitions (transient evaluative cell--not a DATATYPE!)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// NULL is a transient evaluation product.  It is used as a signal for
// "soft failure", e.g. `find [a b] 'c` is NULL, hence they are conditionally
// false.  But null isn't an "ANY-VALUE!", and can't be stored in BLOCK!s that
// are seen by the user.
//
// The libRebol API takes advantage of this by actually using C's concept of
// a null pointer to directly represent the optional state.  By promising this
// is the case, clients of the API can write `if (value)` or `if (!value)`
// and be sure that there's not some nonzero address of a "null-valued cell".
// So there is no `isRebolNull()` API.
//
// But that's the API.  Internally, cells are the currency used, and if they
// are to represent an "optional" value, there must be a special bit pattern
// used to mark them as not containing any value at all.  These are called
// "nulled cells" and marked by means of their KIND3Q_BYTE().
//

#define IS_NULLED(v) \
    (VAL_TYPE(v) == REB_NULL)

inline static REBVAL *Init_Nulled_Untracked(RELVAL *out) {
    INIT_VAL_HEADER(out, REB_NULL, CELL_MASK_NONE);

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
    PAYLOAD(Any, out).first.trash = ZEROTRASH;
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif

    return cast(REBVAL*, out);
}

#define Init_Nulled(out) \
    Init_Nulled_Untracked(TRACK_CELL_IF_DEBUG(out))



// !!! A theory was that the "evaluated" flag would help a function that took
// both <opt> and <end>, which are converted to nulls, distinguish what kind
// of null it is.  This may or may not be a good idea, but unevaluating it
// here just to make a note of the concept, and tag it via the callsites.
//
#define Init_Endish_Nulled(out) \
    INIT_VAL_HEADER((out), REB_NULL, CELL_FLAG_UNEVALUATED)

inline static bool IS_ENDISH_NULLED(const RELVAL *v)
    { return IS_NULLED(v) and GET_CELL_FLAG(v, UNEVALUATED); }

// To help ensure full nulled cells don't leak to the API, the variadic
// interface only accepts nullptr.  Any internal code with a REBVAL* that may
// be a "nulled cell" must translate any such cells to nullptr.
//
inline static const REBVAL *NULLIFY_NULLED(const REBVAL *cell)
  { return VAL_TYPE(cell) == REB_NULL ? nullptr : cell; }

inline static const REBVAL *REIFY_NULL(const REBVAL *cell)
  { return cell == nullptr ? Lib(NULL) : cell; }
