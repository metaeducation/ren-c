//
//  File: %sys-decimal.h
//  Summary: "DECIMAL! and PERCENT! Datatype Header"
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

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_DECIMAL(v) \
        PAYLOAD(Decimal, (v)).dec
#else
    // allows an assert, but also lvalue: `VAL_DECIMAL(v) = xxx`
    //
    inline static REBDEC VAL_DECIMAL(REBCEL(const*) v) {
        assert(CELL_KIND(v) == REB_DECIMAL or CELL_KIND(v) == REB_PERCENT);
        return PAYLOAD(Decimal, v).dec;
    }
    inline static REBDEC & VAL_DECIMAL(RELVAL *v) {
        assert(VAL_TYPE(v) == REB_DECIMAL or VAL_TYPE(v) == REB_PERCENT);
        return PAYLOAD(Decimal, v).dec;
    }
#endif

inline static REBVAL *Init_Decimal(RELVAL *out, REBDEC dec) {
    INIT_VAL_HEADER(out, REB_DECIMAL, CELL_MASK_NONE);
    PAYLOAD(Decimal, out).dec = dec;
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Percent(RELVAL *out, REBDEC dec) {
    INIT_VAL_HEADER(out, REB_PERCENT, CELL_MASK_NONE);
    PAYLOAD(Decimal, out).dec = dec;
    return cast(REBVAL*, out);
}
