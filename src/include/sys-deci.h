//
//  File: %sys-deci.h
//  Summary: "Deci Datatype"
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

typedef struct deci {
    uint_fast32_t m0;  /* significand, lowest part */
    uint_fast32_t m1;  /* significand, continuation */
    uint_fast32_t m2; /* significand, highest part (only 23 bits used) */
    int_fast8_t s;   /* sign, 0 means nonnegative, 1 means nonpositive */
    int_fast8_t e;        /* exponent */
} deci;
