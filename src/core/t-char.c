//
//  File: %t-char.c
//  Summary: "character datatype"
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


//
//  CT_Char: C
//
REBINT CT_Char(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num;

    if (mode >= 0) {
        if (mode == 0)
            num = LO_CASE(VAL_CHAR(a)) - LO_CASE(VAL_CHAR(b));
        else
            num = VAL_CHAR(a) - VAL_CHAR(b);
        return (num == 0);
    }

    num = VAL_CHAR(a) - VAL_CHAR(b);
    if (mode == -1) return (num >= 0);
    return (num > 0);
}


//
//  MAKE_Char: C
//
void MAKE_Char(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_CHAR);
    UNUSED(kind);

    REBUNI uni;

    switch(VAL_TYPE(arg)) {
    case REB_CHAR:
        uni = VAL_CHAR(arg);
        break;

    case REB_INTEGER:
    case REB_DECIMAL:
        {
        REBINT n = Int32(arg);
        if (n > MAX_UNI || n < 0) goto bad_make;
        uni = n;
        }
        break;

    case REB_BINARY: {
        const REBYTE *bp = VAL_BIN_HEAD(arg);
        REBCNT len = VAL_LEN_AT(arg);
        if (len == 0) goto bad_make;
        if (*bp <= 0x80) {
            if (len != 1)
                goto bad_make;

            uni = *bp;
        }
        else {
            --len;
            bp = Back_Scan_UTF8_Char(&uni, bp, &len);
            if (!bp || len != 0) // must be valid UTF8 and consume all data
                goto bad_make;
        }
        break; }

    case REB_STRING:
        if (VAL_INDEX(arg) >= VAL_LEN_HEAD(arg))
            goto bad_make;
        uni = GET_ANY_CHAR(VAL_SERIES(arg), VAL_INDEX(arg));
        break;

    default:
    bad_make:
        fail (Error_Bad_Make(REB_CHAR, arg));
    }

    Init_Char(out, uni);
}


//
//  TO_Char: C
//
void TO_Char(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Char(out, kind, arg);
}


static REBINT Math_Arg_For_Char(REBVAL *arg, REBSYM verb)
{
    switch (VAL_TYPE(arg)) {
    case REB_CHAR:
        return VAL_CHAR(arg);

    case REB_INTEGER:
        return VAL_INT32(arg);

    case REB_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

    default:
        fail (Error_Math_Args(REB_CHAR, verb));
    }
}


//
//  MF_Char: C
//
void MF_Char(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBSER *out = mo->series;

    REBOOL parened = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
    REBUNI chr = VAL_CHAR(v);

    REBCNT tail = SER_LEN(out);

    if (form) {
        EXPAND_SERIES_TAIL(out, 4); // 4 is worst case scenario of bytes
        tail += Encode_UTF8_Char(BIN_AT(out, tail), chr);
        SET_SERIES_LEN(out, tail);
    }
    else {
        EXPAND_SERIES_TAIL(out, 10); // worst case: #"^(1234)"

        REBYTE *bp = BIN_AT(out, tail);
        *bp++ = '#';
        *bp++ = '"';
        bp = Emit_Uni_Char(bp, chr, parened);
        *bp++ = '"';

        SET_SERIES_LEN(out, bp - BIN_HEAD(out));
    }
    TERM_BIN(out);
}


//
//  REBTYPE: C
//
REBTYPE(Char)
{
    // Don't use a REBUNI for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, VAL_CHAR(D_ARG(1)));
    REBI64 arg;

    switch (verb) {

    case SYM_ADD: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr += arg;
        break; }

    case SYM_SUBTRACT: {
        arg = Math_Arg_For_Char(D_ARG(2), verb);

        // Rebol2 and Red return CHAR! values for subtraction from another
        // CHAR! (though Red checks for overflow and errors on something like
        // `subtract #"^(00)" #"^(01)"`, vs returning #"^(FF)").
        //
        // R3-Alpha chose to return INTEGER! and gave a signed difference, so
        // the above would give -1.
        //
        if (IS_CHAR(D_ARG(2))) {
            Init_Integer(D_OUT, chr - arg);
            return R_OUT;
        }

        chr -= arg;
        break; }

    case SYM_MULTIPLY:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr *= arg;
        break;

    case SYM_DIVIDE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr /= arg;
        break;

    case SYM_REMAINDER:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        if (arg == 0)
            fail (Error_Zero_Divide_Raw());
        chr %= arg;
        break;

    case SYM_INTERSECT:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr &= cast(REBUNI, arg);
        break;

    case SYM_UNION:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr |= cast(REBUNI, arg);
        break;

    case SYM_DIFFERENCE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr ^= cast(REBUNI, arg);
        break;

    case SYM_COMPLEMENT:
        chr = cast(REBUNI, ~chr);
        break;

    case SYM_EVEN_Q:
        return (cast(REBUNI, ~chr) & 1) ? R_TRUE : R_FALSE;

    case SYM_ODD_Q:
        return (chr & 1) ? R_TRUE : R_FALSE;

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return R_VOID;
        }
        if (chr == 0) break;
        chr = cast(REBUNI, 1 + cast(REBCNT, Random_Int(REF(secure)) % chr));
        break; }

    default:
        fail (Error_Illegal_Action(REB_CHAR, verb));
    }

    if (chr < 0 || chr > 0xffff) // DEBUG_UTF8_EVERYWHERE
        fail (Error_Type_Limit_Raw(Get_Type(REB_CHAR)));

    Init_Char(D_OUT, cast(REBUNI, chr));
    return R_OUT;
}

