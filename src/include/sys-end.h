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
//=//// NOTES /////////////////////////////////////////////////////////////=//
///
// * R3-Alpha terminated all arrays with an END! cell--much the way that
//   C strings are terminated by '\0'.  This provided a convenient way to
//   loop over arrays as `for (; NOT_END(value); ++value)`.  But it was
//   redundant with the length and had cost to keep in sync...plus it also
//   meant memory for the arrays had to be rounded up.  1 cell arrays had
//   to go in the 2 pool, 2-cell arrays had to go in the 4 pool, etc.  Ren-C
//   eliminated this and instead enumerates to the tail pointer.
//
// * Some places (like the feed value) would be more efficient if it were
//   to use nullptr instead of needing to go through an indirection to test
//   for IS_END().  Though this does run greater risk of confusing with the API
//   usage of nullptr, and for now it's clearer to read by emphasizing END.
//

// Note: can't cast rebEND to a REBVAL*, due to alignment (it's char aligned).
// The END_CELL can be used to pass an end signal to things that take REBVAL*
//
#define END_CELL \
    c_cast(const REBVAL*, &PG_End_Cell)


// IMPORTANT: Notice that END markers may be as short as 2 bytes long.
//
#if (! DEBUG_CHECK_ENDS)
    #define IS_END(p) \
        (((const REBYTE*)(p))[0] == END_SIGNAL_BYTE)  // Note: needs (p) parens!
#else
    inline static bool IS_END(const void *p) {
        const REBYTE* bp = cast(const REBYTE*, p);

        if (*bp != END_SIGNAL_BYTE) {
            assert(*bp & NODE_BYTEMASK_0x01_CELL);
            return false;
        }

        assert(not (*bp & NODE_BYTEMASK_0x01_CELL));  // may not be full cell

        assert(bp[1] == REB_0);  // true whether rebEND string, or a full cell

        return true;
    }
#endif

#define NOT_END(v) \
    (not IS_END(v))
