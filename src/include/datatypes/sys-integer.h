//
//  File: %sys-integer.h
//  Summary: "INTEGER! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// Integers in Rebol were standardized to use a compiler-provided 64-bit
// value.  This was formally added to the spec in C99, but many compilers
// supported it before that.
//
// !!! 64-bit extensions were added by the "rebolsource" fork, with much of
// the code still written to operate on 32-bit values.  Since the standard
// unit of indexing and block length counts remains 32-bit in that 64-bit
// build at the moment, many lingering references were left that operated
// on 32-bit values.  To make this clearer, the macros have been renamed
// to indicate which kind of integer they retrieve.  However, there should
// be a general review for reasoning, and error handling + overflow logic
// for these cases.
//

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_INT64(v) \
        PAYLOAD(Integer, (v)).i64
#else
    // allows an assert, but also lvalue: `VAL_INT64(v) = xxx`
    //
    inline static REBI64 VAL_INT64(noquote(const Cell*) v) {
        assert(CELL_HEART(v) == REB_INTEGER);
        return PAYLOAD(Integer, v).i64;
    }
    inline static REBI64 & VAL_INT64(Cell *v) {
        assert(VAL_TYPE(v) == REB_INTEGER);
        return PAYLOAD(Integer, v).i64;
    }
#endif

inline static REBVAL *Init_Integer_Untracked(Cell *out, REBI64 i64) {
    Reset_Cell_Header_Untracked(out, REB_INTEGER, CELL_MASK_NONE);
    PAYLOAD(Integer, out).i64 = i64;
  #ifdef ZERO_UNUSED_CELL_FIELDS
    EXTRA(Any, out).trash = ZEROTRASH;
  #endif
    return cast(REBVAL*, out);
}

#define Init_Integer(out,i64) \
    Init_Integer_Untracked(TRACK(out), (i64))

inline static int32_t VAL_INT32(noquote(const Cell*) v) {
    if (VAL_INT64(v) > INT32_MAX or VAL_INT64(v) < INT32_MIN)
        fail (Error_Out_Of_Range(CELL_TO_VAL(v)));
    return cast(int32_t, VAL_INT64(v));
}

inline static uint32_t VAL_UINT32(noquote(const Cell*) v) {
    if (VAL_INT64(v) < 0 or VAL_INT64(v) > UINT32_MAX)
        fail (Error_Out_Of_Range(CELL_TO_VAL(v)));
    return cast(uint32_t, VAL_INT64(v));
}

inline static REBYTE VAL_UINT8(noquote(const Cell*) v) {
    if (VAL_INT64(v) > 255 or VAL_INT64(v) < 0)
        fail (Error_Out_Of_Range(CELL_TO_VAL(v)));
    return cast(REBYTE, VAL_INT32(v));
}


#define ROUND_TO_INT(d) \
    cast(int32_t, floor((MAX(INT32_MIN, MIN(INT32_MAX, d))) + 0.5))
