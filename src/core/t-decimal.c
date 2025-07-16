//
//  file: %t-decimal.c
//  summary: "decimal datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
#include <math.h>
#include <float.h>

#define COEF 0.0625 // Coefficient used for float comparision
#define EQ_RANGE 4

#ifdef NO_GCVT
static char *gcvt(double value, int digits, char *buffer)
{
    sprintf(buffer, "%.*g", digits, value);
    return buffer;
}
#endif

/*
    Purpose: {defines the almost_equal comparison function}
    Properties: {
        since floating point numbers are ordered and there is only
        a finite quantity of floating point numbers, it is possible
        to assign an ordinal (integer) number to any floating point number so,
        that the ordinal numbers of neighbors differ by one

        the function compares floating point numbers based on
        the difference of their ordinal numbers in the ordering
        of floating point numbers

        difference of 0 means exact equality, difference of 1 means, that
        the numbers are neighbors.
    }
    Advantages: {
        the function detects approximate equality.

        the function is more strict in the zero neighborhood than
        absolute-error-based approaches

        as opposed to relative-error-based approaches the error can be
        precisely specified, max_diff = 0 meaning exact match, max_diff = 1
        meaning that neighbors are deemed equal, max_diff = 10 meaning, that
        the numbers are deemed equal if at most 9
        distinct floating point numbers can be found between them

        the max_diff value may be one of the system options specified in
        the system/options object allowing users to exactly define the
        strictness of equality checks
    }
    Differences: {
        The approximate comparison currently used in R3 corresponds to the
        almost_equal function using max_diff = 10 (according to my tests).

        The main differences between the currently used comparison and the
        one based on the ordinal number comparison are:
        -   the max_diff parameter can be adjusted, allowing
            the user to precisely specify the strictness of the comparison
        -   the difference rule holds for zero too, which means, that
            zero is deemed equal with totally max_diff distinct (tiny) numbers
    }
    Notes: {
        the max_diff parameter does not need to be a REBI64 number,
        a smaller range like REBLEN may suffice
    }
*/

bool almost_equal(REBDEC a, REBDEC b, REBLEN max_diff) {
    union {REBDEC d; REBI64 i;} ua, ub;
    REBI64 int_diff;

    ua.d = a;
    ub.d = b;

    /* Make ua.i a twos-complement ordinal number */
    if (ua.i < 0) ua.i = INT64_MIN - ua.i;

    /* Make ub.i a twos-complement ordinal number */
    if (ub.i < 0) ub.i = INT64_MIN - ub.i;

    int_diff = ua.i - ub.i;
    if (int_diff < 0) int_diff = -int_diff;

    return cast(REBU64, int_diff) <= max_diff;
}


//
//  Init_Decimal_Bits: C
//
Value* Init_Decimal_Bits(Cell* out, const Byte *bp)
{
    RESET_CELL(out, TYPE_DECIMAL);

    Byte *dp = cast(Byte*, &VAL_DECIMAL(out));

  #ifdef ENDIAN_LITTLE
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = bp[7 - n];
  #elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = bp[n];
  #else
    #error "Unsupported CPU endian"
  #endif

    return KNOWN(out);
}


//
//  MAKE_Decimal: C
//
Bounce MAKE_Decimal(Value* out, enum Reb_Kind kind, const Value* arg)
{
    REBDEC d;

    switch (Type_Of(arg)) {
    case TYPE_DECIMAL:
        d = VAL_DECIMAL(arg);
        goto dont_divide_if_percent;

    case TYPE_PERCENT:
        d = VAL_DECIMAL(arg);
        goto dont_divide_if_percent;

    case TYPE_INTEGER:
        d = cast(REBDEC, VAL_INT64(arg));
        goto dont_divide_if_percent;

    case TYPE_CHAR:
        d = cast(REBDEC, VAL_CHAR(arg));
        goto dont_divide_if_percent;

    case TYPE_TIME:
        d = cast(REBDEC, VAL_NANO(arg)) * NANO;
        break;

    case TYPE_MONEY:
    case TYPE_TEXT: {
        Size size;
        Byte *bp = Analyze_String_For_Scan(&size, arg, MAX_SCAN_DECIMAL);

        Erase_Cell(out);
        if (nullptr == Scan_Decimal(out, bp, size, kind != TYPE_PERCENT))
            goto bad_make;

        d = VAL_DECIMAL(out); // may need to divide if percent, fall through
        break; }

    case TYPE_BINARY:
        if (Series_Len_At(arg) < 8)
            panic (Error_Invalid(arg));

        Init_Decimal_Bits(out, Blob_At(arg)); // makes TYPE_DECIMAL
        RESET_CELL(out, kind); // override type if TYPE_PERCENT
        d = VAL_DECIMAL(out);
        break;

    default:
        if (Any_List(arg) && VAL_ARRAY_LEN_AT(arg) == 2) {
            Cell* item = Cell_List_At(arg);
            if (Is_Integer(item))
                d = cast(REBDEC, VAL_INT64(item));
            else if (Is_Decimal(item) || Is_Percent(item))
                d = VAL_DECIMAL(item);
            else
                panic (Error_Invalid_Core(item, VAL_SPECIFIER(arg)));

            ++item;

            REBDEC exp;
            if (Is_Integer(item))
                exp = cast(REBDEC, VAL_INT64(item));
            else if (Is_Decimal(item) || Is_Percent(item))
                exp = VAL_DECIMAL(item);
            else
                panic (Error_Invalid_Core(item, VAL_SPECIFIER(arg)));

            while (exp >= 1) {
                //
                // !!! Comment here said "funky. There must be a better way"
                //
                --exp;
                d *= 10.0;
                if (!FINITE(d))
                    panic (Error_Overflow_Raw());
            }

            while (exp <= -1) {
                ++exp;
                d /= 10.0;
            }
        }
        else
            panic (Error_Bad_Make(kind, arg));
    }

    if (kind == TYPE_PERCENT)
        d /= 100.0;

dont_divide_if_percent:
    if (!FINITE(d))
        panic (Error_Overflow_Raw());

    RESET_CELL(out, kind);
    VAL_DECIMAL(out) = d;
    return out;

bad_make:
    panic (Error_Bad_Make(kind, arg));
}


//
//  TO_Decimal: C
//
Bounce TO_Decimal(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Decimal(out, kind, arg);
}


//
//  Eq_Decimal: C
//
bool Eq_Decimal(REBDEC a, REBDEC b)
{
    return almost_equal(a, b, 10);
}


//
//  Eq_Decimal2: C
//
bool Eq_Decimal2(REBDEC a, REBDEC b)
{
    return almost_equal(a, b, 0);
}


//
//  CT_Decimal: C
//
REBINT CT_Decimal(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) {
        if (mode == 0)
            return almost_equal(VAL_DECIMAL(a), VAL_DECIMAL(b), 10) ? 1 : 0;

        return almost_equal(VAL_DECIMAL(a), VAL_DECIMAL(b), 0) ? 1 : 0;
    }

    if (mode == -1)
        return (VAL_DECIMAL(a) >= VAL_DECIMAL(b)) ? 1 : 0;

    return (VAL_DECIMAL(a) > VAL_DECIMAL(b)) ? 1 : 0;
}


//
//  MF_Decimal: C
//
// Notice this covers both DECIMAL! and PERCENT!
//
void MF_Decimal(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    switch (Type_Of(v)) {
    case TYPE_DECIMAL:
    case TYPE_PERCENT: {
        Byte buf[60];
        REBINT len = Emit_Decimal(
            buf,
            VAL_DECIMAL(v),
            Is_Percent(v) ? DEC_MOLD_PERCENT : 0,
            GET_MOLD_FLAG(mo, MOLD_FLAG_COMMA_PT) ? ',' : '.',
            mo->digits
        );
        Append_Unencoded_Len(mo->utf8flex, s_cast(buf), len);
        break; }

    default:
        crash (v);
    }
}


//
//  REBTYPE: C
//
REBTYPE(Decimal)
{
    Value* val = D_ARG(1);
    Value* arg;
    REBDEC d2;
    enum Reb_Kind type;

    REBDEC d1 = VAL_DECIMAL(val);

    Option(SymId) sym = Word_Id(verb);

    // !!! This used to use IS_BINARY_ACT() which is no longer available with
    // symbol-based dispatch.  Consider doing this another way.
    //
    if (
        sym == SYM_ADD
        || sym == SYM_SUBTRACT
        || sym == SYM_MULTIPLY
        || sym == SYM_DIVIDE
        || sym == SYM_REMAINDER
        || sym == SYM_POWER
    ){
        arg = D_ARG(2);
        type = Type_Of(arg);
        if ((
            type == TYPE_PAIR
            or type == TYPE_TUPLE
            or type == TYPE_MONEY
            or type == TYPE_TIME
        ) and (
            sym == SYM_ADD ||
            sym == SYM_MULTIPLY
        )){
            Copy_Cell(OUT, D_ARG(2));
            Copy_Cell(D_ARG(2), D_ARG(1));
            Copy_Cell(D_ARG(1), OUT);
            GENERIC_HOOK hook = Generic_Hooks[Type_Of(D_ARG(1))];
            return hook(level_, verb);
        }

        // If the type of the second arg is something we can handle:
        if (type == TYPE_DECIMAL
            || type == TYPE_INTEGER
            || type == TYPE_PERCENT
            || type == TYPE_MONEY
            || type == TYPE_CHAR
        ){
            if (type == TYPE_DECIMAL) {
                d2 = VAL_DECIMAL(arg);
            }
            else if (type == TYPE_PERCENT) {
                d2 = VAL_DECIMAL(arg);
                if (sym == SYM_DIVIDE)
                    type = TYPE_DECIMAL;
                else if (not Is_Percent(val))
                    type = Type_Of(val);
            }
            else if (type == TYPE_CHAR) {
                d2 = cast(REBDEC, VAL_CHAR(arg));
                type = TYPE_DECIMAL;
            }
            else {
                d2 = cast(REBDEC, VAL_INT64(arg));
                type = TYPE_DECIMAL;
            }

            switch (sym) {

            case SYM_ADD:
                d1 += d2;
                goto setDec;

            case SYM_SUBTRACT:
                d1 -= d2;
                goto setDec;

            case SYM_MULTIPLY:
                d1 *= d2;
                goto setDec;

            case SYM_DIVIDE:
            case SYM_REMAINDER:
                if (d2 == 0.0)
                    panic (Error_Zero_Divide_Raw());
                if (sym == SYM_DIVIDE)
                    d1 /= d2;
                else
                    d1 = fmod(d1, d2);
                goto setDec;

            case SYM_POWER:
                if (d2 == 0) {
                    //
                    // This means `power 0 0` is 1.0, despite it not being
                    // defined.  It's a pretty general programming consensus:
                    //
                    // https://rosettacode.org/wiki/Zero_to_the_zero_power
                    //
                    d1 = 1.0;
                    goto setDec;
                }
                if (d1 == 0)
                    goto setDec;
                d1 = pow(d1, d2);
                goto setDec;

            default:
                panic (Error_Math_Args(Type_Of(val), verb));
            }
        }
        panic (Error_Math_Args(Type_Of(val), verb));
    }

    type = Type_Of(val);

    // unary actions
    switch (sym) {

    case SYM_COPY:
        Copy_Cell(OUT, val);
        return OUT;

    case SYM_NEGATE:
        d1 = -d1;
        goto setDec;

    case SYM_ABSOLUTE:
        if (d1 < 0) d1 = -d1;
        goto setDec;

    case SYM_EVEN_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return LOGIC(true);
        return LOGIC(false);

    case SYM_ODD_Q:
        d1 = fabs(fmod(d1, 2.0));
        if (d1 < 0.5 || d1 >= 1.5)
            return LOGIC(false);
        return LOGIC(true);

    case SYM_ROUND: {
        INCLUDE_PARAMS_OF_ROUND;

        UNUSED(PARAM(VALUE));

        Flags flags = (
            (Bool_ARG(TO) ? RF_TO : 0)
            | (Bool_ARG(EVEN) ? RF_EVEN : 0)
            | (Bool_ARG(DOWN) ? RF_DOWN : 0)
            | (Bool_ARG(HALF_DOWN) ? RF_HALF_DOWN : 0)
            | (Bool_ARG(FLOOR) ? RF_FLOOR : 0)
            | (Bool_ARG(CEILING) ? RF_CEILING : 0)
            | (Bool_ARG(HALF_CEILING) ? RF_HALF_CEILING : 0)
        );

        arg = ARG(SCALE);
        if (Bool_ARG(TO)) {
            if (Is_Time(arg))
                panic (Error_Invalid(arg));

            d1 = Round_Dec(d1, flags, Dec64(arg));
            if (Is_Integer(arg))
                return Init_Integer(OUT, cast(REBI64, d1));

            if (Is_Percent(arg))
                type = TYPE_PERCENT;
        }
        else
            d1 = Round_Dec(
                d1, flags | RF_TO, type == TYPE_PERCENT ? 0.01L : 1.0L
            );
        goto setDec; }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(VALUE));
        if (Bool_ARG(ONLY))
            panic (Error_Bad_Refines_Raw());

        if (Bool_ARG(SEED)) {
            REBDEC d = VAL_DECIMAL(val);
            REBI64 i;
            assert(sizeof(d) == sizeof(i));
            memcpy(&i, &d, sizeof(d));
            Set_Random(i); // use IEEE bits
            return nullptr;
        }
        d1 = Random_Dec(d1, Bool_ARG(SECURE));
        goto setDec; }

    case SYM_COMPLEMENT:
        return Init_Integer(OUT, ~cast(REBINT, d1));

    default:
        ; // put panic outside switch() to catch any leaks
    }

    panic (Error_Illegal_Action(Type_Of(val), verb));

setDec:
    if (not FINITE(d1))
        panic (Error_Overflow_Raw());

    RESET_CELL(OUT, type);
    VAL_DECIMAL(OUT) = d1;

    return OUT;
}
