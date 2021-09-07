//
//  File: %sys-end.h
//  Summary: {Non-value type that signals feed termination and invisibility}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// An END signals the termination of a "Feed" of values (which may come from a
// C variadic, which has no length or intrinsic tail pointer...so we must use
// some sort of signal...and `nullptr` is used in the API for NULL cells).
//
// END also can represent a state which is "more empty than NULL".  Some slots
// (such as the output slot of a frame) will tolerate this marker, but they
// are illegal most places...and will assert on typical tests like IS_BLOCK()
// or IS_WORD().  So tests on values must be guarded with IS_END() to tolerate
// them...or the KIND3Q_BYTE() lower-level accessors must be used.
//
// Another use for the END cell state is in an optimized array representation
// that fits 0 or 1 cells into the series node itself.  Since the cell lives
// where the content tracking information would usually be, there's no length.
// Hence the presence of an END cell in the slot indicates length 0.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * There's some crossover in situations where you might use unreadable trash
//   with when you would use an END.  But there are fewer places where
//   ends are tolerated by the GC...such as frame output cells.  You can't put
//   an END in an array, while an unreadable trash can go there.
//
// * R3-Alpha terminated all arrays with an END! cell--much the way that
//   C strings are terminated by '\0'.  This provided a convenient way to
//   loop over arrays as `for (; NOT_END(value); ++value)`.  But it was
//   redundant with the length and had cost to keep in sync...plus it also
//   meant memory for the arrays had to be rounded up.  1 cell arrays had
//   to go in the 2 pool, 2-cell arrays had to go in the 4 pool, etc.  Ren-C
//   eliminated this and instead enumerates to the tail pointer.
//

#define END_CELL \
    c_cast(const REBVAL*, &PG_End_Cell)

inline static REBVAL *SET_END_Untracked(RELVAL *out) {
    INIT_VAL_HEADER(out, REB_0_END, CELL_MASK_NONE);
    return cast(REBVAL*, out);
}

#define SET_END(out) \
    SET_END_Untracked(TRACK(out))


// Optimized Prep + SET_END (optimize after it's finalized)
//
inline static REBVAL *Prep_End_Untracked(RELVAL *out)
  { return SET_END_Untracked(Prep_Cell_Untracked(out)); }

#define Prep_End(out) \
    TRACK(Prep_End_Untracked(out))  // TRACK() expects REB_0, call *after*


// IMPORTANT: Notice that END markers may not have NODE_FLAG_CELL, and may
// be as short as 2 bytes long.
//
#if !defined(DEBUG_CHECK_ENDS)
    #define IS_END(p) \
        (((const REBYTE*)(p))[1] == REB_0_END)  // Note: needs (p) parens!
#else
    inline static bool IS_END(const void *p) {
        if (((const REBYTE*)(p))[0] & NODE_BYTEMASK_0x40_FREE) {
            printf("IS_END() called on garbage\n");
            panic (p);
        }

        if (((const REBYTE*)(p))[1] == REB_0_END)
            return true;

        if (not (((const REBYTE*)(p))[0] & NODE_BYTEMASK_0x01_CELL)) {
            printf("IS_END() found non-END pointer that's not a cell\n");
            panic (p);
        }

        return false;
    }
#endif

#define NOT_END(v) \
    (not IS_END(v))
