//
//  file: %cell-integer.h
//  summary: "INTEGER! Datatype Header"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define VAL_INT64(c) \
        (c)->payload.i64

    #define mutable_VAL_INT64(c) \
        (c)->payload.i64
#else
    // allows an assert, but also lvalue: `VAL_INT64(v) = xxx`
    //
    INLINE REBI64 VAL_INT64(const Value* c) {
        assert(Heart_Of(c) == TYPE_INTEGER);
        return c->payload.i64;
    }
    INLINE REBI64 & mutable_VAL_INT64(Value* c) {
        assert(Heart_Of(c) == TYPE_INTEGER);
        Assert_Cell_Writable(c);
        return c->payload.i64;
    }
#endif

INLINE Element* Init_Integer_Untracked(Init(Element) out, REBI64 i64) {
    Reset_Cell_Header_Noquote(out, CELL_MASK_INTEGER);
    Corrupt_Unused_Field(out->extra.corrupt);
    out->payload.i64 = i64;

    return out;
}

#define Init_Integer(out,i64) \
    TRACK(Init_Integer_Untracked((out), (i64)))


#define ROUND_TO_INT(d) \
    cast(int32_t, floor((MAX(INT32_MIN, MIN(INT32_MAX, d))) + 0.5))


INLINE int32_t VAL_INT32(const Value* v) {
    if (VAL_INT64(v) > INT32_MAX or VAL_INT64(v) < INT32_MIN)
        abrupt_panic (Error_Out_Of_Range(v));
    return cast(int32_t, VAL_INT64(v));
}

INLINE uint32_t VAL_UINT32(const Value* v) {
    if (VAL_INT64(v) < 0 or VAL_INT64(v) > UINT32_MAX)
        abrupt_panic (Error_Out_Of_Range(v));
    return cast(uint32_t, VAL_INT64(v));
}

INLINE Byte VAL_UINT8(const Value* v) {
    if (VAL_INT64(v) > 255 or VAL_INT64(v) < 0)
        abrupt_panic (Error_Out_Of_Range(v));
    return cast(Byte, VAL_INT32(v));
}
