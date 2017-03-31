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
#ifdef NDEBUG
    UNUSED(kind);
#else
    assert(kind == REB_CHAR);
#endif

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

    case REB_BINARY:
        {
        const REBYTE *bp = VAL_BIN(arg);
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
        } // case REB_BINARY
        break;

#ifdef removed
//      case REB_ISSUE:
        // Scan 8 or 16 bit hex str, will throw on error...
        REBINT n = Scan_Hex_Value(
            VAL_RAW_DATA_AT(arg), VAL_LEN_AT(arg), !VAL_BYTE_SIZE(arg)
        );
        if (n > MAX_UNI || n < 0) goto bad_make;
        chr = n;
        break;
#endif

    case REB_STRING:
        if (VAL_INDEX(arg) >= VAL_LEN_HEAD(arg))
            goto bad_make;
        uni = GET_ANY_CHAR(VAL_SERIES(arg), VAL_INDEX(arg));
        break;

    default:
    bad_make:
        fail (Error_Bad_Make(REB_CHAR, arg));
    }

    SET_CHAR(out, uni);
}


//
//  TO_Char: C
//
void TO_Char(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    MAKE_Char(out, kind, arg);
}


static REBINT Math_Arg_For_Char(REBVAL *arg, REBSYM action)
{
    switch (VAL_TYPE(arg)) {
    case REB_CHAR:
        return VAL_CHAR(arg);

    case REB_INTEGER:
        return VAL_INT32(arg);

    case REB_DECIMAL:
        return cast(REBINT, VAL_DECIMAL(arg));

    default:
        fail (Error_Math_Args(REB_CHAR, action));
    }
}


//
//  REBTYPE: C
//
REBTYPE(Char)
{
    REBCNT chr = VAL_CHAR(D_ARG(1)); // !!! Larger than REBCHR for math ops?
    REBINT arg;

    switch (action) {

    case SYM_ADD:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        chr += cast(REBUNI, arg);
        break;

    case SYM_SUBTRACT:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        chr -= cast(REBUNI, arg);
        if (IS_CHAR(D_ARG(2))) {
            SET_INTEGER(D_OUT, chr);
            return R_OUT;
        }
        break;

    case SYM_MULTIPLY:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        chr *= arg;
        break;

    case SYM_DIVIDE:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        if (arg == 0) fail (Error_Zero_Divide_Raw());
        chr /= arg;
        break;

    case SYM_REMAINDER:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        if (arg == 0) fail (Error_Zero_Divide_Raw());
        chr %= arg;
        break;

    case SYM_AND_T:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        chr &= cast(REBUNI, arg);
        break;

    case SYM_OR_T:
        arg = Math_Arg_For_Char(D_ARG(2), action);
        chr |= cast(REBUNI, arg);
        break;

    case SYM_XOR_T:
        arg = Math_Arg_For_Char(D_ARG(2), action);
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
        fail (Error_Illegal_Action(REB_CHAR, action));
    }

    if ((chr >> 16) != 0 && (chr >> 16) != 0xffff)
        fail (Error_Type_Limit_Raw(Get_Type(REB_CHAR)));
    SET_CHAR(D_OUT, chr);
    return R_OUT;
}

