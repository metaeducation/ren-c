//
//  File: %sys-money.h
//  Summary: "Deci Datatype Functions"
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
// R3-Alpha's MONEY! type is "unitless" currency, such that $10/$10 = $1
// (and not 1).  This is because the feature in Rebol2 of being able to
// store the ISO 4217 code (~15 bits) was not included:
//
// https://en.wikipedia.org/wiki/ISO_4217
//
// According to @Ladislav:
//
// "The money datatype is neither a bignum, nor a fixpoint arithmetic.
//  It actually is unnormalized decimal floating point."
//
// !!! The naming of "deci" used by MONEY! as "decimal" is a confusing overlap
// with DECIMAL!, although that name may be changing also.
//
// !!! It would be better if there were no "deci" structure independent of
// a REBVAL itself, so long as it is designed to fit in a REBVAL anyway.
//
// !!! In R3-alpha, the money type was implemented under a type called "deci".
// The payload for a deci was more than 64 bits in size, which meant it had
// to be split across the separated union components in Ren-C.  (The 64-bit
// aligned "payload" and 32-bit aligned "extra" were broken out independently,
// so that setting one union member would not disengage the other.)
//
// PAYLOAD CONTAINS:
//
//     unsigned m1:32; /* significand, continuation */
//     unsigned m2:23; /* significand, highest part */
//     unsigned s:1;   /* sign, 0 means nonnegative, 1 means nonpositive */
//     int e:8;        /* exponent */
//
// EXTRA CONTAINS:
//
//     unsigned m0:32; /* significand, lowest part */
//

inline static deci VAL_MONEY_AMOUNT(REBCEL(const*) v) {
    deci amount;

    uintptr_t u = EXTRA(Any, v).u;
    assert(u <= UINT32_MAX);
    amount.m0 = u; // "significand, lowest part" (32 bits)

    uintptr_t u1 = PAYLOAD(Any, v).first.u;
    assert(u1 <= UINT32_MAX);
    amount.m1 = u1; // "significand, continuation" (32 bits)

    uintptr_t u2 = PAYLOAD(Any, v).second.u;
    assert(u2 <= UINT32_MAX);

    amount.e = cast(signed char, u2 & 0xFF);  // "exponent" (8 bits)
    u2 >>= 8;  // shift so that highest part of significant is in low 24 bits

    amount.s = 0 != (u2 & (1 << 23));  // sign bit
    u2 &= ~cast(uintptr_t, 1 << 23);  // mask out high 24th bit

    amount.m2 = cast(uint32_t, u2);  // "significand, highest part" (23 bits)

    return amount;
}

inline static REBVAL *Init_Money(RELVAL *out, deci amount) {
    INIT_VAL_HEADER(out, REB_MONEY, CELL_MASK_NONE);

    EXTRA(Any, out).u = amount.m0;  // "significand, lowest part"
    PAYLOAD(Any, out).first.u = amount.m1;  // "significand, continuation"

    uintptr_t u2 = amount.m2;  // "significand, highest part" (23 bits)

    if (amount.s)
        u2 |= cast(unsigned, 1 << 23);  // sign bit (nonnegative/nonpositive)

    u2 <<= 8;  // shift so exponent can go in low byte
    u2 |= cast(unsigned char, amount.e);  // "exponent" (8 bits)"

    PAYLOAD(Any, out).second.u = u2;

    return cast(REBVAL*, out);
}


/* unary operators - logic */
bool deci_is_zero (const deci a);

/* unary operators - deci */
deci deci_abs (deci a);
deci deci_negate (deci a);

/* binary operators - logic */
bool deci_is_equal (deci a, deci b);
bool deci_is_lesser_or_equal (deci a, deci b);
bool deci_is_same (deci a, deci b);

/* binary operators - deci */
deci deci_add (deci a, deci b);
deci deci_subtract (deci a, deci b);
deci deci_multiply (const deci a, const deci b);
deci deci_divide (deci a, deci b);
deci deci_mod (deci a, deci b);

/* conversion to deci */
deci int_to_deci (REBI64 a);
deci decimal_to_deci (REBDEC a);
deci string_to_deci (const REBYTE *s, const REBYTE **endptr);
deci binary_to_deci(const REBYTE *s);

/* conversion to other datatypes */
REBI64 deci_to_int (const deci a);
REBDEC deci_to_decimal (const deci a);
REBINT deci_to_string(REBYTE *string, const deci a, const REBYTE symbol, const REBYTE point);
REBYTE *deci_to_binary(REBYTE binary[12], const deci a);

/* math functions */
deci deci_ldexp (deci a, int32_t e);
deci deci_truncate (deci a, deci b);
deci deci_away (deci a, deci b);
deci deci_floor (deci a, deci b);
deci deci_ceil (deci a, deci b);
deci deci_half_even (deci a, deci b);
deci deci_half_away (deci a, deci b);
deci deci_half_truncate (deci a, deci b);
deci deci_half_ceil (deci a, deci b);
deci deci_half_floor (deci a, deci b);
deci deci_sign (deci a);
