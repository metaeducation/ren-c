//
//  File: %sys-tuple.h
//  Summary: "Tuple Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
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
// TUPLE! is a Rebol2/R3-Alpha concept to fit up to 7 byte-sized integers
// directly into a value payload without needing to make a series allocation.
// At source level they would be numbers separated by dots, like `1.2.3.4.5`.
// This was mainly applied for IP addresses and RGB/RGBA constants, and
// considered to be a "lightweight"...it would allow PICK and POKE like a
// series, but did not behave like one due to not having a position.
//
// !!! Ren-C challenges the value of the TUPLE! type as defined.  Color
// literals are often hexadecimal (where BINARY! would do) and IPv6 addresses
// have a different notation.  It may be that `.` could be used for a more
// generalized partner to PATH!, where `a.b.1` would be like a/b/1
//

#define MAX_TUPLE \
    ((sizeof(uint32_t) * 2)) // for same properties on 64-bit and 32-bit


// Tuple has a compact form that allows it to represent bytes with more
// optimal storage.  It can pack as many bytes in the tuple as space
// available in the cell.  This is the size of the payload (which varies on
// 32 and 64 bit systems).  So it should be willing to expand to an
// arbitrary size if need be.
//
inline static REBVAL *Init_Tuple_Bytes(
    RELVAL *out,
    const REBYTE *data,
    REBLEN len
){
    RESET_CELL(out, REB_TUPLE, CELL_FLAG_FIRST_IS_NODE);

    REBARR *a = Make_Array_Core(len, NODE_FLAG_MANAGED);
    TERM_ARRAY_LEN(a, len);
    for (; len > 0; --len, ++data)
        Init_Integer(Alloc_Tail_Array(a), *data);

    INIT_VAL_NODE(out, Freeze_Array_Shallow(a));  // immutable

    /*  // optimized version

    if (len > MAX_TUPLE)  // need to make some kind of series
        fail ("Extension of optimized binary tuples not yet implemented");

    PAYLOAD(Bytes, out).common[0] = len;  // length goes in the first byte

    REBLEN n = len;
    REBYTE *bp = PAYLOAD(Bytes, out).common + 1;  // content is after length
    for (; n > 0; --n)
        *bp++ = *data++;

    // !!! Historically, 1.0.0 = 1.0.0.0 under non-strict equality.  Make the
    // comparison easier just by setting all the bytes to zero.  This is
    // kind of superfluous; and ZERO? is basically a hack...review.
    //
    memset(bp, 0, MAX_TUPLE - len);
    */

    INIT_BINDING(out, UNBOUND);  // generic tuples execute, need bindings
    return cast(REBVAL*, out);
}

// !!! This is a simple compatibility routine that will just set bytes in
// the buffer to 0 if the element is not an integer.
//
inline static void Get_Tuple_Bytes(
    void *buf,
    const RELVAL *tuple,
    REBSIZ size
){
    REBYTE *dp = cast(REBYTE*, buf);
    REBSIZ i;
    DECLARE_LOCAL (temp);
    for (i = 0; i < size; ++i) {
        REBCEL(const*) cell = VAL_SEQUENCE_AT(temp, tuple, i);
        dp[i] = CELL_KIND(cell) == REB_INTEGER ? VAL_UINT32(cell) : 0;
    }
}

inline static REBVAL *Init_Zeroed_Hack(RELVAL *out, enum Reb_Kind kind) {
    //
    // !!! This captures of a dodgy behavior of R3-Alpha, which was to assume
    // that clearing the payload of a value and then setting the header made
    // it the `zero?` of that type.  Review uses.
    //
    if (kind == REB_PAIR) {
        Init_Pair_Int(out, 0, 0);
    }
    else if (kind == REB_TUPLE) {
        Init_Tuple_Bytes(out, nullptr, 0);
    }
    else {
        RESET_CELL(out, kind, CELL_MASK_NONE);
        CLEAR(&out->extra, sizeof(union Reb_Value_Extra));
        CLEAR(&out->payload, sizeof(union Reb_Value_Payload));
    }
    return SPECIFIC(out);
}
