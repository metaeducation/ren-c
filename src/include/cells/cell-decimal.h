//
//  file: %cell-decimal.h
//  summary: "DECIMAL! and PERCENT! Datatype Header"
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
// Implementation-wise, the decimal type is a `double`-precision floating
// point number in C (typically 64-bit).  The percent type uses the same
// payload, and is currently extracted with VAL_DECIMAL() as well.
//
// !!! Calling a floating point type "decimal" appears based on Rebol's
// original desire to use familiar words and avoid jargon.  It has however
// drawn criticism from those who don't think it correctly conveys floating
// point behavior, expecting something else.  Red has renamed the type
// FLOAT! which may be a good idea.
//

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define VAL_DECIMAL(c) \
        (c)->payload.dec
#else
    // allows an assert, but also lvalue: `VAL_DECIMAL(v) = xxx`
    //
    INLINE REBDEC VAL_DECIMAL(const Value* c) {
        assert(Heart_Of(c) == TYPE_DECIMAL or Heart_Of(c) == TYPE_PERCENT);
        return c->payload.dec;
    }
    INLINE REBDEC & VAL_DECIMAL(Value* c) {
        assert(Heart_Of(c) == TYPE_DECIMAL or Heart_Of(c) == TYPE_PERCENT);
        return c->payload.dec;
    }
#endif


INLINE Element* Init_Decimal_Or_Percent_Untracked(
    Init(Element) out,
    Heart heart,
    REBDEC dec
){
    assert(heart == TYPE_DECIMAL or heart == TYPE_PERCENT);

    if (not FINITE(dec))
        panic (Error_Overflow_Raw());

    Reset_Cell_Header_Noquote(
        out,
        FLAG_HEART(heart) | CELL_MASK_NO_MARKING
    );
    out->payload.dec = dec;
    return out;
}

#define Init_Decimal_Or_Percent(out,heart,dec) \
    TRACK(Init_Decimal_Or_Percent_Untracked((out),(heart),(dec)))

#define Init_Decimal(out,dec) \
    TRACK(Init_Decimal_Or_Percent_Untracked((out), TYPE_DECIMAL, (dec)))

#define Init_Percent(out,dec) \
    TRACK(Init_Decimal_Or_Percent_Untracked((out), TYPE_PERCENT, (dec)))
