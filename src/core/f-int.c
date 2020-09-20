//
//  File: %f-int.c
//  Summary: "integer arithmetic functions"
//  Section: functional
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2014 Atronix Engineering, Inc
// Copyright 2014-2017 Ren-C Open Source Contributors
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
// Based on original code in t-integer.c
//

#include "sys-core.h"

bool reb_i32_add_overflow(int32_t x, int32_t y, int *sum)
{
    int64_t sum64 = cast(int64_t, x) + cast(int64_t, y);
    if (sum64 > INT32_MAX || sum64 < INT32_MIN)
        return true;
    *sum = cast(int32_t, sum64);
    return false;
}

bool reb_u32_add_overflow(uint32_t x, uint32_t y, unsigned int *sum)
{
    uint64_t s = cast(uint64_t, x) + cast(uint64_t, y);
    if (s > INT32_MAX)
        return true;
    *sum = cast(uint32_t, s);
    return false;
}

bool reb_intptr_add_overflow(int64_t x, int64_t y, int64_t *sum)
{
    *sum = cast(uint64_t, x) + cast(uint64_t, y); // unsigned never overflows
    if (((x < 0) == (y < 0)) && ((x < 0) != (*sum < 0)))
        return true;
    return false;
}

bool reb_uintptr_add_overflow(uint64_t x, uint64_t y, uint64_t *sum)
{
    *sum = x + y;
    if (*sum < x || *sum < y)
        return true;
    return false;
}

bool reb_i32_sub_overflow(int32_t x, int32_t y, int32_t *diff)
{
    *diff = cast(int64_t, x) - cast(int64_t, y);
    if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0)))
        return true;
    return false;
}

bool reb_intptr_sub_overflow(int64_t x, int64_t y, int64_t *diff)
{
    *diff = cast(uint64_t, x) - cast(uint64_t, y);
    if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0)))
        return true;
    return false;
}

bool reb_i32_mul_overflow(int32_t x, int32_t y, int32_t *prod)
{
    int64_t p = cast(int64_t, x) * cast(int64_t, y);
    if (p > INT32_MAX || p < INT32_MIN)
        return true;
    *prod = cast(int32_t, p);
    return false;
}

bool reb_u32_mul_overflow(uint32_t x, uint32_t y, uint32_t *prod)
{
    uint64_t p = cast(uint64_t, x) * cast(uint64_t, y);
    if (p > UINT32_MAX)
        return true;
    *prod = cast(uint32_t, p);
    return false;
}

bool reb_intptr_mul_overflow(int64_t x, int64_t y, int64_t *prod)
{
    bool sgn;
    uint64_t p = 0;

    if (!x || !y) {
        *prod = 0;
        return false;
    }

    sgn = x < 0;
    if (sgn) {
        if (x == INT64_MIN) {
            switch (y) {
            case 0:
                *prod = 0;
                return false;
            case 1:
                *prod = x;
                return false;
            default:
                return true;
            }
        }
        x = -x; // undefined when x == INT64_MIN
    }
    if (y < 0) {
        sgn = not sgn;
        if (y == INT64_MIN) {
            switch (x) {
            case 0:
                *prod = 0;
                return false;
            case 1:
                if (!sgn) {
                    return true;
                } else {
                    *prod = y;
                    return false;
                }
            default:
                return true;
            }
        }
        y = -y; // undefined when y == MIN_I64
    }

    if (
        REB_UINTPTR_MUL_OF(x, y, cast(uint64_t*, &p))
        || (!sgn && p > INT64_MAX)
        || (sgn && p - 1 > INT64_MAX)
    ){
        return true; // assumes 2's complement
    }

    if (sgn && p == cast(uint64_t, INT64_MIN)) {
        *prod = INT64_MIN;
        return false;
    }

    if (sgn)
        *prod = -cast(int64_t, p);
    else
        *prod = p;

    return false;
}

bool reb_uintptr_mul_overflow(uint64_t x, uint64_t y, uint64_t *prod)
{
    uint64_t b = UINT64_C(1) << 32;

    uint64_t x1 = x >> 32;
    uint64_t x0 = cast(uint32_t, x);
    uint64_t y1 = y >> 32;
    uint64_t y0 = cast(uint32_t, y);

    // Note: p = (x1 * y1) * b^2 + (x0 * y1 + x1 * y0) * b + x0 * y0

    if (x1 && y1)
        return true; // (x1 * y1) * b^2 overflows

    uint64_t tmp = (x0 * y1 + x1 * y0); // never overflow, b.c. x1 * y1 == 0
    if (tmp >= b)
        return true; // (x0 * y1 + x1 * y0) * b overflows

    return did (REB_UINTPTR_ADD_OF(tmp << 32, x0 * y0, prod));
}
