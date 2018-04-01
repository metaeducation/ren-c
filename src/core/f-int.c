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
// Copyright 2014-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Based on original code in t-integer.c
//

#include "reb-c.h"
#include "sys-int-funcs.h"

REBOOL reb_i32_add_overflow(int32_t x, int32_t y, int *sum)
{
    int64_t sum64 = cast(int64_t, x) + cast(int64_t, y);
    if (sum64 > INT32_MAX || sum64 < INT32_MIN)
        return TRUE;
    *sum = cast(int32_t, sum64);
    return FALSE;
}

REBOOL reb_u32_add_overflow(uint32_t x, uint32_t y, unsigned int *sum)
{
    uint64_t s = cast(uint64_t, x) + cast(uint64_t, y);
    if (s > INT32_MAX)
        return TRUE;
    *sum = cast(uint32_t, s);
    return FALSE;
}

REBOOL reb_i64_add_overflow(int64_t x, int64_t y, int64_t *sum)
{
    *sum = cast(uint64_t, x) + cast(uint64_t, y); // unsigned never overflows
    if (((x < 0) == (y < 0)) && ((x < 0) != (*sum < 0)))
        return TRUE;
    return FALSE;
}

REBOOL reb_u64_add_overflow(uint64_t x, uint64_t y, uint64_t *sum)
{
    *sum = x + y;
    if (*sum < x || *sum < y)
        return TRUE;
    return FALSE;
}

REBOOL reb_i32_sub_overflow(int32_t x, int32_t y, int32_t *diff)
{
    *diff = cast(int64_t, x) - cast(int64_t, y);
    if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0)))
        return TRUE;
    return FALSE;
}

REBOOL reb_i64_sub_overflow(int64_t x, int64_t y, int64_t *diff)
{
    *diff = cast(uint64_t, x) - cast(uint64_t, y);
    if (((x < 0) != (y < 0)) && ((x < 0) != (*diff < 0)))
        return TRUE;
    return FALSE;
}

REBOOL reb_i32_mul_overflow(int32_t x, int32_t y, int32_t *prod)
{
    int64_t p = cast(int64_t, x) * cast(int64_t, y);
    if (p > INT32_MAX || p < INT32_MIN)
        return TRUE;
    *prod = cast(int32_t, p);
    return FALSE;
}

REBOOL reb_u32_mul_overflow(uint32_t x, uint32_t y, uint32_t *prod)
{
    uint64_t p = cast(uint64_t, x) * cast(uint64_t, y);
    if (p > UINT32_MAX)
        return TRUE;
    *prod = cast(uint32_t, p);
    return FALSE;
}

REBOOL reb_i64_mul_overflow(int64_t x, int64_t y, int64_t *prod)
{
    REBOOL sgn;
    uint64_t p = 0;

    if (!x || !y) {
        *prod = 0;
        return FALSE;
    }

    sgn = LOGICAL(x < 0);
    if (sgn) {
        if (x == INT64_MIN) {
            switch (y) {
            case 0:
                *prod = 0;
                return FALSE;
            case 1:
                *prod = x;
                return FALSE;
            default:
                return TRUE;
            }
        }
        x = -x; // undefined when x == INT64_MIN
    }
    if (y < 0) {
        sgn = NOT(sgn);
        if (y == INT64_MIN) {
            switch (x) {
            case 0:
                *prod = 0;
                return FALSE;
            case 1:
                if (!sgn) {
                    return TRUE;
                } else {
                    *prod = y;
                    return FALSE;
                }
            default:
                return TRUE;
            }
        }
        y = -y; // undefined when y == MIN_I64
    }

    if (
        REB_U64_MUL_OF(x, y, cast(uint64_t*, &p))
        || (!sgn && p > INT64_MAX)
        || (sgn && p - 1 > INT64_MAX)
    ){
        return TRUE; // assumes 2's complement
    }

    if (sgn && p == cast(uint64_t, INT64_MIN)) {
        *prod = INT64_MIN;
        return FALSE;
    }

    if (sgn)
        *prod = -cast(int64_t, p);
    else
        *prod = p;

    return FALSE;
}

REBOOL reb_u64_mul_overflow(uint64_t x, uint64_t y, uint64_t *prod)
{
    uint64_t b = cast(uint64_t, 1) << 32;

    uint64_t x1 = x >> 32;
    uint64_t x0 = cast(uint32_t, x);
    uint64_t y1 = y >> 32;
    uint64_t y0 = cast(uint32_t, y);

    // Note: p = (x1 * y1) * b^2 + (x0 * y1 + x1 * y0) * b + x0 * y0

    if (x1 && y1)
        return TRUE; // (x1 * y1) * b^2 overflows

    uint64_t tmp = (x0 * y1 + x1 * y0); // never overflow, b.c. x1 * y1 == 0
    if (tmp >= b)
        return TRUE; // (x0 * y1 + x1 * y0) * b overflows

    return LOGICAL(REB_U64_ADD_OF(tmp << 32, x0 * y0, prod));
}
