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
REBINT CT_Char(const Cell* a, const Cell* b, REBINT mode)
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
Bounce MAKE_Char(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_CHAR);
    UNUSED(kind);

    switch(VAL_TYPE(arg)) {
    case REB_CHAR:
        return Init_Char(out, VAL_CHAR(arg));

    case REB_INTEGER:
    case REB_DECIMAL: {
        REBINT n = Int32(arg);
        if (n > MAX_UNI or n < 0)
            goto bad_make;
        return Init_Char(out, n); }

      case REB_BINARY: {
        const Byte *bp = Cell_Blob_Head(arg);
        Size len = Cell_Series_Len_At(arg);
        if (len == 0)
            goto bad_make;

        Ucs2Unit uni;
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
        return Init_Char(out, uni); }

      case REB_TEXT:
        if (VAL_INDEX(arg) >= VAL_LEN_HEAD(arg))
            goto bad_make;
        return Init_Char(out, GET_ANY_CHAR(Cell_Flex(arg), VAL_INDEX(arg)));

      default:
        break;
    }

  bad_make:
    fail (Error_Bad_Make(REB_CHAR, arg));
}


//
//  TO_Char: C
//
Bounce TO_Char(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Char(out, kind, arg);
}


static REBINT Math_Arg_For_Char(Value* arg, Value* verb)
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
void MF_Char(Molder* mo, const Cell* v, bool form)
{
    Binary* out = mo->utf8flex;

    bool parened = GET_MOLD_FLAG(mo, MOLD_FLAG_ALL);
    Ucs2Unit chr = VAL_CHAR(v);

    REBLEN tail = Flex_Len(out);

    if (form) {
        Expand_Flex_Tail(out, 4); // 4 is worst case scenario of bytes
        tail += Encode_UTF8_Char(Binary_At(out, tail), chr);
        Set_Flex_Len(out, tail);
    }
    else {
        Expand_Flex_Tail(out, 10); // worst case: #"^(1234)"

        Byte *bp = Binary_At(out, tail);
        *bp++ = '#';
        *bp++ = '"';
        bp = Emit_Uni_Char(bp, chr, parened);
        *bp++ = '"';

        Set_Flex_Len(out, bp - Binary_Head(out));
    }
    Term_Binary(out);
}


//
//  REBTYPE: C
//
REBTYPE(Char)
{
    // Don't use a Ucs2Unit for chr, because it does signed math and then will
    // detect overflow.
    //
    REBI64 chr = cast(REBI64, VAL_CHAR(D_ARG(1)));
    REBI64 arg;

    switch (Cell_Word_Id(verb)) {

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
        if (Is_Char(D_ARG(2))) {
            Init_Integer(OUT, chr - arg);
            return OUT;
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
        chr &= cast(Ucs2Unit, arg);
        break;

    case SYM_UNION:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr |= cast(Ucs2Unit, arg);
        break;

    case SYM_DIFFERENCE:
        arg = Math_Arg_For_Char(D_ARG(2), verb);
        chr ^= cast(Ucs2Unit, arg);
        break;

    case SYM_COMPLEMENT:
        chr = cast(Ucs2Unit, ~chr);
        break;

    case SYM_EVEN_Q:
        return Init_Logic(OUT, did (cast(Ucs2Unit, ~chr) & 1));

    case SYM_ODD_Q:
        return Init_Logic(OUT, did (chr & 1));

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));
        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed)) {
            Set_Random(chr);
            return nullptr;
        }
        if (chr == 0) break;
        chr = cast(Ucs2Unit, 1 + cast(REBLEN, Random_Int(REF(secure)) % chr));
        break; }

    default:
        fail (Error_Illegal_Action(REB_CHAR, verb));
    }

    if (chr < 0 || chr > 0xffff)  // see main branch build for UTF-8 Everywhere
        fail (Error_Type_Limit_Raw(Datatype_From_Kind(REB_CHAR)));

    return Init_Char(OUT, cast(Ucs2Unit, chr));
}
