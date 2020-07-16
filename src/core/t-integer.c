//
//  File: %t-integer.c
//  Summary: "integer datatype"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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

#include "sys-core.h"
#include "sys-deci-funcs.h"
#include "sys-int-funcs.h"


//
//  CT_Integer: C
//
REBINT CT_Integer(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    if (mode >= 0)  return (VAL_INT64(a) == VAL_INT64(b));
    if (mode == -1) return (VAL_INT64(a) >= VAL_INT64(b));
    return (VAL_INT64(a) > VAL_INT64(b));
}


//
//  MAKE_Integer: C
//
REB_R MAKE_Integer(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_INTEGER);
    UNUSED(kind);

    if (IS_LOGIC(arg)) {
        //
        // !!! Due to Rebol's policies on conditional truth and falsehood,
        // it refuses to say TO FALSE is 0.  MAKE has shades of meaning
        // that are more "dialected", e.g. MAKE BLOCK! 10 creates a block
        // with capacity 10 and not literally `[10]` (or a block with ten
        // BLANK! values in it).  Under that liberal umbrella it decides
        // that it will make an integer 0 out of FALSE due to it having
        // fewer seeming "rules" than TO would.

        if (VAL_LOGIC(arg))
            Init_Integer(out, 1);
        else
            Init_Integer(out, 0);

        // !!! The same principle could suggest MAKE is not bound by
        // the "reversibility" requirement and hence could interpret
        // binaries unsigned by default.  Before getting things any
        // weirder should probably leave it as is.
    }
    else {
        // use signed logic by default (use TO-INTEGER/UNSIGNED to force
        // unsigned interpretation or error if that doesn't make sense)

        Value_To_Int64(out, arg, false);
    }

    return out;
}


//
//  TO_Integer: C
//
REB_R TO_Integer(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_INTEGER);
    UNUSED(kind);

    // use signed logic by default (use TO-INTEGER/UNSIGNED to force
    // unsigned interpretation or error if that doesn't make sense)

    Value_To_Int64(out, arg, false);
    return out;
}


//
//  Value_To_Int64: C
//
// Interpret `value` as a 64-bit integer and return it in `out`.
//
// If `no_sign` is true then use that to inform an ambiguous conversion
// (e.g. TO-INTEGER/UNSIGNED #{FF} is 255 instead of -1).  However, it
// won't contradict the sign of unambiguous source.  So the string "-1"
// will raise an error if you try to convert it unsigned.  (For this,
// use `abs to-integer "-1"` and not `to-integer/unsigned "-1"`.)
//
// Because Rebol's INTEGER! uses a signed REBI64 and not an unsigned
// REBU64, a request for unsigned interpretation is limited to using
// 63 of those bits.  A range error will be thrown otherwise.
//
// If a type is added or removed, update REBNATIVE(to_integer)'s spec
//
void Value_To_Int64(REBVAL *out, const REBVAL *value, bool no_sign)
{
    // !!! Code extracted from REBTYPE(Integer)'s A_MAKE and A_TO cases
    // Use SWITCH instead of IF chain? (was written w/ANY_STR test)

    if (IS_INTEGER(value)) {
        Move_Value(out, value);
        goto check_sign;
    }
    if (IS_DECIMAL(value) || IS_PERCENT(value)) {
        if (VAL_DECIMAL(value) < MIN_D64 || VAL_DECIMAL(value) >= MAX_D64)
            fail (Error_Overflow_Raw());

        Init_Integer(out, cast(REBI64, VAL_DECIMAL(value)));
        goto check_sign;
    }
    else if (IS_MONEY(value)) {
        Init_Integer(out, deci_to_int(VAL_MONEY_AMOUNT(value)));
        goto check_sign;
    }
    else if (IS_BINARY(value)) { // must be before ANY_STRING() test...

        // Rebol3 creates 8-byte big endian for signed 64-bit integers.
        // Rebol2 created 4-byte big endian for signed 32-bit integers.
        //
        // Values originating in file formats from other systems vary widely.
        // Note that in C the default interpretation of single bytes in most
        // implementations of a `char` is signed.
        //
        // We assume big-Endian for decoding (clients can REVERSE if they
        // want little-Endian).  Also by default assume that any missing
        // sign-extended to 64-bits based on the most significant byte
        //
        //     #{01020304} => #{0000000001020304}
        //     #{DECAFBAD} => #{FFFFFFFFDECAFBAD}
        //
        // To override this interpretation and always generate an unsigned
        // result, pass in `no_sign`.  (Used by TO-INTEGER/UNSIGNED)
        //
        // If under these rules a number cannot be represented within the
        // numeric range of the system's INTEGER!, it will error.  This
        // attempts to "future-proof" for other integer sizes and as an
        // interface could support BigNums in the future.

        REBYTE *bp = VAL_BIN_AT(value);
        REBCNT n = VAL_LEN_AT(value);
        bool negative;
        REBINT fill;

    #if !defined(NDEBUG)
        //
        // This is what R3-Alpha did.
        //
        if (LEGACY(OPTIONS_FOREVER_64_BIT_INTS)) {
            REBI64 i = 0;
            if (n > sizeof(REBI64)) n = sizeof(REBI64);
            for (; n; n--, bp++)
                i = cast(REBI64, (cast(REBU64, i) << 8) | *bp);

            Init_Integer(out, i);

            // There was no TO-INTEGER/UNSIGNED in R3-Alpha, so even if
            // running in compatibility mode we can check the sign if used.
            //
            goto check_sign;
        }
    #endif

        if (n == 0) {
            //
            // !!! Should #{} empty binary be 0 or error?  (Historically, 0)
            //
            Init_Integer(out, 0);
            return;
        }

        // default signedness interpretation to high-bit of first byte, but
        // override if the function was called with `no_sign`
        //
        negative = no_sign ? false : (*bp >= 0x80);

        // Consume any leading 0x00 bytes (or 0xFF if negative)
        //
        while (n != 0 && *bp == (negative ? 0xFF : 0x00)) {
            ++bp;
            --n;
        }

        // If we were consuming 0xFFs and passed to a byte that didn't have
        // its high bit set, we overstepped our bounds!  Go back one.
        //
        if (negative && n > 0 && *bp < 0x80) {
            --bp;
            ++n;
        }

        // All 0x00 bytes must mean 0 (or all 0xFF means -1 if negative)
        //
        if (n == 0) {
            if (negative) {
                assert(!no_sign);
                Init_Integer(out, -1);
            } else
                Init_Integer(out, 0);
            return;
        }

        // Not using BigNums (yet) so max representation is 8 bytes after
        // leading 0x00 or 0xFF stripped away
        //
        if (n > 8)
            fail (Error_Out_Of_Range_Raw(value));

        REBI64 i = 0;

        // Pad out to make sure any missing upper bytes match sign
        for (fill = n; fill < 8; fill++)
            i = cast(REBI64,
                (cast(REBU64, i) << 8) | (negative ? 0xFF : 0x00)
            );

        // Use binary data bytes to fill in the up-to-8 lower bytes
        //
        while (n != 0) {
            i = cast(REBI64, (cast(REBU64, i) << 8) | *bp);
            bp++;
            n--;
        }

        if (no_sign && i < 0) {
            //
            // bits may become signed via shift due to 63-bit limit
            //
            fail (Error_Out_Of_Range_Raw(value));
        }

        Init_Integer(out, i);
        return;
    }
    else if (IS_ISSUE(value)) {
        //
        // Like converting a binary, except uses a string of codepoints
        // from the word name conversion.  Does not allow for signed
        // interpretations, e.g. #FFFF => 65535, not -1.  Unsigned makes
        // more sense as these would be hexes likely typed in by users,
        // who rarely do 2s-complement math in their head.

        REBSTR *spelling = VAL_WORD_SPELLING(value);
        const REBYTE *bp = cb_cast(STR_HEAD(spelling));
        size_t size = STR_SIZE(spelling);

        if (size > MAX_HEX_LEN) {
            // Lacks BINARY!'s accommodation of leading 00s or FFs
            fail (Error_Out_Of_Range_Raw(value));
        }

        if (!Scan_Hex(out, bp, size, size))
            fail (Error_Bad_Make(REB_INTEGER, value));

        // !!! Unlike binary, always assumes unsigned (should it?).  Yet still
        // might run afoul of 64-bit range limit.
        //
        if (VAL_INT64(out) < 0)
            fail (Error_Out_Of_Range_Raw(value));

        return;
    }
    else if (ANY_STRING(value)) {
        REBSIZ size;
        const REBCNT max_len = VAL_LEN_AT(value); // e.g. "no maximum"
        REBYTE *bp = Analyze_String_For_Scan(&size, value, max_len);
        if (
            memchr(bp, '.', size)
            || memchr(bp, 'e', size)
            || memchr(bp, 'E', size)
        ){
            DECLARE_LOCAL (d);
            if (Scan_Decimal(d, bp, size, true)) {
                if (
                    VAL_DECIMAL(d) < INT64_MAX
                    && VAL_DECIMAL(d) >= INT64_MIN
                ){
                    Init_Integer(out, cast(REBI64, VAL_DECIMAL(d)));
                    goto check_sign;
                }

                fail (Error_Overflow_Raw());
            }
        }
        if (Scan_Integer(out, bp, size))
            goto check_sign;

        fail (Error_Bad_Make(REB_INTEGER, value));
    }
    else if (IS_LOGIC(value)) {
        //
        // Rebol's choice is that no integer is uniquely representative of
        // "falsehood" condition, e.g. `if 0 [print "this prints"]`.  So to
        // say TO LOGIC! 0 is FALSE would be disingenuous.
        //
        fail (Error_Bad_Make(REB_INTEGER, value));
    }
    else if (IS_CHAR(value)) {
        Init_Integer(out, VAL_CHAR(value)); // always unsigned
        return;
    }
    else if (IS_TIME(value)) {
        Init_Integer(out, SECS_FROM_NANO(VAL_NANO(value))); // always unsigned
        return;
    }
    else
        fail (Error_Bad_Make(REB_INTEGER, value));

check_sign:
    if (no_sign && VAL_INT64(out) < 0)
        fail (Error_Positive_Raw());
}


//
//  to-integer: native [
//
//  {Synonym of TO INTEGER! when used without refinements, adds /UNSIGNED.}
//
//      value [
//      integer! decimal! percent! money! char! time!
//      issue! binary! any-string!
//      ]
//      /unsigned
//      {For BINARY! interpret as unsigned, otherwise error if signed.}
//  ]
//
REBNATIVE(to_integer)
{
    INCLUDE_PARAMS_OF_TO_INTEGER;

    Value_To_Int64(D_OUT, ARG(value), REF(unsigned));

    return D_OUT;
}


//
//  MF_Integer: C
//
void MF_Integer(REB_MOLD *mo, const RELVAL *v, bool form)
{
    UNUSED(form);

    REBYTE buf[60];
    REBINT len = Emit_Integer(buf, VAL_INT64(v));
    Append_Unencoded_Len(mo->series, s_cast(buf), len);
}


//
//  REBTYPE: C
//
REBTYPE(Integer)
{
    REBVAL *val = D_ARG(1);
    REBI64 num = VAL_INT64(val);

    REBI64 arg;

    REBSYM sym = VAL_WORD_SYM(verb);

    // !!! This used to rely on IS_BINARY_ACT, which is no longer available
    // in the symbol based dispatch.  Consider doing another way.
    //
    if (
        sym == SYM_ADD
        or sym == SYM_SUBTRACT
        or sym == SYM_MULTIPLY
        or sym == SYM_DIVIDE
        or sym == SYM_POWER
        or sym == SYM_INTERSECT
        or sym == SYM_UNION
        or sym == SYM_DIFFERENCE
        or sym == SYM_REMAINDER
    ){
        REBVAL *val2 = D_ARG(2);

        if (IS_INTEGER(val2))
            arg = VAL_INT64(val2);
        else if (IS_CHAR(val2))
            arg = VAL_CHAR(val2);
        else {
            // Decimal or other numeric second argument:
            REBCNT n = 0; // use to flag special case
            switch (VAL_WORD_SYM(verb)) {
            // Anything added to an integer is same as adding the integer:
            case SYM_ADD:
            case SYM_MULTIPLY: {
                // Swap parameter order:
                Move_Value(D_OUT, val2);  // Use as temp workspace
                Move_Value(val2, val);
                Move_Value(val, D_OUT);
                GENERIC_HOOK hook = Generic_Hooks[VAL_TYPE(val)];
                return hook(frame_, verb); }

            // Only type valid to subtract from, divide into, is decimal/money:
            case SYM_SUBTRACT:
                n = 1;
                /* fall through */
            case SYM_DIVIDE:
            case SYM_REMAINDER:
            case SYM_POWER:
                if (IS_DECIMAL(val2) || IS_PERCENT(val2)) {
                    Init_Decimal(val, cast(REBDEC, num)); // convert main arg
                    return T_Decimal(frame_, verb);
                }
                if (IS_MONEY(val2)) {
                    Init_Money(val, int_to_deci(VAL_INT64(val)));
                    return T_Money(frame_, verb);
                }
                if (n > 0) {
                    if (IS_TIME(val2)) {
                        VAL_NANO(val) = SEC_TIME(VAL_INT64(val));
                        CHANGE_VAL_TYPE_BITS(val, REB_TIME);
                        return T_Time(frame_, verb);
                    }
                    if (IS_DATE(val2))
                        return T_Date(frame_, verb);
                }

            default:
                break;
            }
            fail (Error_Math_Args(REB_INTEGER, verb));
        }
    }
    else
        arg = 0xDECAFBAD; // wasteful, but avoid maybe unassigned warning

    switch (sym) {

    case SYM_COPY:
        Move_Value(D_OUT, val);
        return D_OUT;

    case SYM_ADD: {
        REBI64 anum;
        if (REB_I64_ADD_OF(num, arg, &anum))
            fail (Error_Overflow_Raw());
        return Init_Integer(D_OUT, anum); }

    case SYM_SUBTRACT: {
        REBI64 anum;
        if (REB_I64_SUB_OF(num, arg, &anum))
            fail (Error_Overflow_Raw());
        return Init_Integer(D_OUT, anum); }

    case SYM_MULTIPLY: {
        REBI64 p;
        if (REB_I64_MUL_OF(num, arg, &p))
            fail (Error_Overflow_Raw());
        return Init_Integer(D_OUT, p); }

    case SYM_DIVIDE:
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        if (num == INT64_MIN && arg == -1)
            fail (Error_Overflow_Raw());
        if (num % arg == 0)
            return Init_Integer(D_OUT, num / arg);
        // Fall thru
    case SYM_POWER:
        Init_Decimal(D_ARG(1), cast(REBDEC, num));
        Init_Decimal(D_ARG(2), cast(REBDEC, arg));
        return T_Decimal(frame_, verb);

    case SYM_REMAINDER:
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        return Init_Integer(D_OUT, (arg != -1) ? (num % arg) : 0);

    case SYM_INTERSECT:
        return Init_Integer(D_OUT, num & arg);

    case SYM_UNION:
        return Init_Integer(D_OUT, num | arg);

    case SYM_DIFFERENCE:
        return Init_Integer(D_OUT, num ^ arg);

    case SYM_NEGATE:
        if (num == INT64_MIN)
            fail (Error_Overflow_Raw());
        return Init_Integer(D_OUT, -num);

    case SYM_COMPLEMENT:
        return Init_Integer(D_OUT, ~num);

    case SYM_ABSOLUTE:
        if (num == INT64_MIN)
            fail (Error_Overflow_Raw());
        return Init_Integer(D_OUT, num < 0 ? -num : num);

    case SYM_EVEN_Q:
        num = ~num;
        // falls through
    case SYM_ODD_Q:
        if (num & 1)
            return Init_True(D_OUT);
        return Init_False(D_OUT);

    case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;

        UNUSED(PAR(value));

        REBFLGS flags = (
            (REF(to) ? RF_TO : 0)
            | (REF(even) ? RF_EVEN : 0)
            | (REF(down) ? RF_DOWN : 0)
            | (REF(half_down) ? RF_HALF_DOWN : 0)
            | (REF(floor) ? RF_FLOOR : 0)
            | (REF(ceiling) ? RF_CEILING : 0)
            | (REF(half_ceiling) ? RF_HALF_CEILING : 0)
        );

        REBVAL *val2 = ARG(scale);
        if (REF(to)) {
            if (IS_MONEY(val2))
                return Init_Money(
                    D_OUT,
                    Round_Deci(
                        int_to_deci(num), flags, VAL_MONEY_AMOUNT(val2)
                    )
                );
            if (IS_DECIMAL(val2) || IS_PERCENT(val2)) {
                REBDEC dec = Round_Dec(
                    cast(REBDEC, num), flags, VAL_DECIMAL(val2)
                );
                RESET_CELL(D_OUT, VAL_TYPE(val2));
                VAL_DECIMAL(D_OUT) = dec;
                return D_OUT;
            }
            if (IS_TIME(val2))
                fail (Error_Invalid(val2));
            arg = VAL_INT64(val2);
        }
        else
            arg = 0L;

        return Init_Integer(D_OUT, Round_Int(num, flags, arg)); }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(num);
            return nullptr;
        }
        if (num == 0)
            break;
        return Init_Integer(D_OUT, Random_Range(num, REF(secure))); }

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_INTEGER, verb));
}


//
//  enbin: native [
//
//  {Encode value as a Little Endian or Big Endian BINARY!, signed/unsigned}
//
//      return: [binary!]
//      settings "[<LE or BE> <+ or +/-> <number of bytes>] (pre-COMPOSE'd)"
//          [block!]
//      value "Value to encode (currently only integers are supported)"
//          [integer!]
//  ]
//
REBNATIVE(enbin)
//
// !!! This routine may wind up being folded into ENCODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
//
// !!! Patched in from %t-binary.c for R3C, with TO-INTEGER left alone.
{
    INCLUDE_PARAMS_OF_ENBIN;

    REBVAL *settings = rebValue("compose", ARG(settings), rebEND);
    if (VAL_LEN_AT(settings) != 3)
        fail ("ENBIN requires array of length 3 for settings for now");
    bool little = rebDid(
        "switch first", settings, "[",
            "'BE [false] 'LE [true]",
            "fail {First element of ENBIN settings must be BE or LE}",
        "]",
        rebEND);
    REBCNT index = VAL_INDEX(settings);
    bool no_sign = rebDid(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of ENBIN settings must be + or +/-}",
        "]",
        rebEND);
    RELVAL *third = VAL_ARRAY_AT_HEAD(settings, index + 2);
    if (not IS_INTEGER(third))
        fail ("Third element of ENBIN settings must be an integer}");
    REBINT num_bytes = VAL_INT32(third);
    if (num_bytes <= 0)
        fail ("Size for ENBIN encoding must be at least 1");
    rebRelease(settings);

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    REBSER* bin = Make_Binary(num_bytes);

    REBINT delta = little ? 1 : -1;
    REBYTE* bp = BIN_HEAD(bin);
    if (not little)
        bp += num_bytes - 1;  // go backwards for big endian

    REBI64 i = VAL_INT64(ARG(value));
    if (no_sign and i < 0)
        fail ("ENBIN request for unsigned but passed-in value is signed");

    // Negative numbers are encoded with two's complement: process we use here
    // is simple: take the absolute value, inverting each byte, add one.
    //
    bool negative = i < 0;
    if (negative)
        i = -(i);

    REBINT carry = negative ? 1 : 0;
    REBINT n = 0;
    while (n != num_bytes) {
        REBINT byte = negative ? ((i % 256) ^ 0xFF) + carry : (i % 256);
        if (byte > 0xFF) {
            assert(byte == 0x100);
            carry = 1;
            byte = 0;
        }
        else
            carry = 0;
        *bp = byte;
        bp += delta;
        i = i / 256;
        ++n;
    }
    if (i != 0)
        rebJumps (
            "fail [", ARG(value), "{exceeds}", rebI(num_bytes), "{bytes}]",
        rebEND);

    // The process of byte production of a positive number shouldn't give us
    // something with the high bit set in a signed representation.
    //
    if (not no_sign and not negative and *(bp - delta) >= 0x80)
        rebJumps (
            "fail [",
                ARG(value), "{aliases a negative value with signed}",
                "{encoding of only}", rebI(num_bytes), "{bytes}",
            "]",
        rebEND);

    TERM_BIN_LEN(bin, num_bytes);
    return Init_Binary(D_OUT, bin);
}


//
//  debin: native [
//
//  {Decode BINARY! as Little Endian or Big Endian, signed/unsigned value}
//
//      return: [integer!]
//      settings "[<LE or BE> <+ or +/-> <number of bytes>] (pre-COMPOSE'd)"
//          [block!]
//      binary "Decoded (defaults length of binary for number of bytes)"
//          [binary!]
//  ]
//
REBNATIVE(debin)
//
// !!! This routine may wind up being folded into DECODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
//
// !!! Patched in from %t-binary.c for R3C, with TO-INTEGER left alone.
{
    INCLUDE_PARAMS_OF_DEBIN;

    REBVAL* settings = rebValue("compose", ARG(settings), rebEND);
    if (VAL_LEN_AT(settings) != 2 and VAL_LEN_AT(settings) != 3)
        fail("DEBIN requires array of length 2 or 3 for settings for now");
    bool little = rebDid(
        "switch first", settings, "[",
            "'BE [false] 'LE [true]",
            "fail {First element of DEBIN settings must be BE or LE}",
        "]",
        rebEND);
    REBCNT index = VAL_INDEX(settings);
    bool no_sign = rebDid(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of DEBIN settings must be + or +/-}",
        "]",
        rebEND);
    REBCNT num_bytes;
    RELVAL *third = VAL_ARRAY_AT_HEAD(settings, index + 2);
    if (IS_END(third))
        num_bytes = VAL_LEN_AT(ARG(binary));
    else {
        if (not IS_INTEGER(third))
            fail ("Third element of DEBIN settings must be an integer}");
        num_bytes = VAL_INT32(third);
        if (VAL_LEN_AT(ARG(binary)) != num_bytes)
            fail ("Input binary is longer than number of bytes to DEBIN");
    }
    if (num_bytes <= 0) {
        //
        // !!! Should #{} empty binary be 0 or error?  (Historically, 0, but
        // if we are going to do this then ENBIN should accept 0 and make #{})
        //
        fail("Size for DEBIN decoding must be at least 1");
    }
    rebRelease(settings);

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    REBINT delta = little ? -1 : 1;
    REBYTE* bp = VAL_BIN_AT(ARG(binary));
    if (little)
        bp += num_bytes - 1;  // go backwards

    REBINT n = num_bytes;

    if (n == 0)
        return Init_Integer(D_OUT, 0);  // !!! Only if we let num_bytes = 0

    // default signedness interpretation to high-bit of first byte, but
    // override if the function was called with `no_sign`
    //
    bool negative = no_sign ? false : (*bp >= 0x80);

    // Consume any leading 0x00 bytes (or 0xFF if negative).  This is just
    // a stopgap measure for reading larger-looking sizes once INTEGER! can
    // support BigNums.
    //
    while (n != 0 and *bp == (negative ? 0xFF : 0x00)) {
        bp += delta;
        --n;
    }

    // If we were consuming 0xFFs and passed to a byte that didn't have
    // its high bit set, we overstepped our bounds!  Go back one.
    //
    if (negative and n > 0 and *bp < 0x80) {
        bp += -(delta);
        ++n;
    }

    // All 0x00 bytes must mean 0 (or all 0xFF means -1 if negative)
    //
    if (n == 0) {
        if (negative) {
            assert(not no_sign);
            return Init_Integer(D_OUT, -1);
        }
        return Init_Integer(D_OUT, 0);
    }

    // Not using BigNums (yet) so max representation is 8 bytes after
    // leading 0x00 or 0xFF stripped away
    //
    if (n > 8)
        fail (Error_Out_Of_Range_Raw(ARG(binary)));

    REBI64 i = 0;

    // Pad out to make sure any missing upper bytes match sign
    //
    REBINT fill;
    for (fill = n; fill < 8; fill++)
        i = cast(REBI64,
            (cast(REBU64, i) << 8) | (negative ? 0xFF : 0x00)
        );

    // Use binary data bytes to fill in the up-to-8 lower bytes
    //
    while (n != 0) {
        i = cast(REBI64, (cast(REBU64, i) << 8) | *bp);
        bp += delta;
        n--;
    }

    if (no_sign and i < 0) {
        //
        // bits may become signed via shift due to 63-bit limit
        //
        fail (Error_Out_Of_Range_Raw(ARG(binary)));
    }

    return Init_Integer(D_OUT, i);
}
