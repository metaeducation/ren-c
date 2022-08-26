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
// "nulled cells" and marked by means of their HEART_BYTE being REB_NULL.
//

#define Is_Nulled(v) \
    (VAL_TYPE(v) == REB_NULL)

inline static bool Is_Quasi_Null(Cell(const*) v)
  { return QUOTE_BYTE(v) == QUASI_2 and HEART_BYTE(v) == REB_NULL; }

inline static REBVAL *Init_Nulled_Untracked(Cell(*) out, Byte quote_byte) {
    Init_Cell_Header_Untracked(
        RESET_Untracked(out),
        FLAG_HEART_BYTE(REB_NULL) | FLAG_QUOTE_BYTE(quote_byte)
            | CELL_MASK_NO_NODES
    );

  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
    PAYLOAD(Any, out).first.trash = ZEROTRASH;
    PAYLOAD(Any, out).second.trash = ZEROTRASH;
  #endif

    return cast(REBVAL*, out);
}

// We ensure that non-quoted, non-quasi NULL isn't written into a Cell(*) e.g.
// for a BLOCK!... must be a Value(*), e.g. a context variable or frame output.
//
#define Init_Nulled(out) \
    TRACK(Init_Nulled_Untracked(ensure(Value(*), (out)), UNQUOTED_1))

#define Init_Quasi_Null(out) \
    TRACK(Init_Nulled_Untracked((out), QUASI_2))

#define Init_Void(out) \
    TRACK(Init_Nulled_Untracked(ensure(Value(*), (out)), ISOTOPE_0))


// To help ensure full nulled cells don't leak to the API, the variadic
// interface only accepts nullptr.  Any internal code with a REBVAL* that may
// be a "nulled cell" must translate any such cells to nullptr.
//
inline static const REBVAL *NULLIFY_NULLED(const REBVAL *cell) {
  return VAL_TYPE(cell) == REB_NULL
      ? cast(REBVAL*, nullptr)  // C++98 ambiguous w/o cast
      : cell;
}

inline static const REBVAL *REIFY_NULL(const REBVAL *cell)
  { return cell == nullptr ? Lib(NULL) : cell; }


// We test for null on an arbitrary result that may be an isotope.  Since we
// don't have generic isotope handling we usually just pass them through, so
// the unchecked type test for null is done first.  But plain Is_Nulled() will
// assert on isotopes.  Make the code look friendlier with this simple macro.
//
#define Is_Breaking_Null(out) \
    (VAL_TYPE_UNCHECKED(out) == REB_NULL)
