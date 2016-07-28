//
//  File: %t-string.c
//  Summary: "string related datatypes"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
//  CT_String: C
//
REBINT CT_String(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num;

    num = Compare_String_Vals(a, b, NOT(mode == 1));

    if (mode >= 0) return (num == 0) ? 1 : 0;
    if (mode == -1) return (num >= 0) ? 1 : 0;
    return (num > 0) ? 1 : 0;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/

// !!! "STRING value to CHAR value (save some code space)" <-- what?
static void str_to_char(REBVAL *out, REBVAL *val, REBCNT idx)
{
    // Note: out may equal val, do assignment in two steps
    REBUNI codepoint = GET_ANY_CHAR(VAL_SERIES(val), idx);
    SET_CHAR(out, codepoint);
}


static void swap_chars(REBVAL *val1, REBVAL *val2)
{
    REBUNI c1;
    REBUNI c2;
    REBSER *s1 = VAL_SERIES(val1);
    REBSER *s2 = VAL_SERIES(val2);

    c1 = GET_ANY_CHAR(s1, VAL_INDEX(val1));
    c2 = GET_ANY_CHAR(s2, VAL_INDEX(val2));

    if (BYTE_SIZE(s1) && c2 > 0xff) Widen_String(s1, TRUE);
    SET_ANY_CHAR(s1, VAL_INDEX(val1), c2);

    if (BYTE_SIZE(s2) && c1 > 0xff) Widen_String(s2, TRUE);
    SET_ANY_CHAR(s2, VAL_INDEX(val2), c1);
}


static void reverse_string(REBVAL *value, REBCNT len)
{
    REBCNT n;
    REBCNT m;
    REBUNI c;

    if (VAL_BYTE_SIZE(value)) {
        REBYTE *bp = VAL_BIN_AT(value);

        for (n = 0, m = len-1; n < len / 2; n++, m--) {
            c = bp[n];
            bp[n] = bp[m];
            bp[m] = (REBYTE)c;
        }
    }
    else {
        REBUNI *up = VAL_UNI_AT(value);

        for (n = 0, m = len-1; n < len / 2; n++, m--) {
            c = up[n];
            up[n] = up[m];
            up[m] = c;
        }
    }
}


static REBCNT find_string(
    REBSER *series,
    REBCNT index,
    REBCNT end,
    REBVAL *target,
    REBCNT len,
    REBCNT flags,
    REBINT skip
) {
    REBCNT start = index;

    if (flags & (AM_FIND_REVERSE | AM_FIND_LAST)) {
        skip = -1;
        start = 0;
        if (flags & AM_FIND_LAST) index = end - len;
        else index--;
    }

    if (ANY_BINSTR(target)) {
        // Do the optimal search or the general search?
        if (
            BYTE_SIZE(series)
            && VAL_BYTE_SIZE(target)
            && !(flags & ~(AM_FIND_CASE|AM_FIND_MATCH))
        ) {
            return Find_Byte_Str(
                series,
                start,
                VAL_BIN_AT(target),
                len,
                NOT(GET_FLAG(flags, ARG_FIND_CASE - 1)),
                GET_FLAG(flags, ARG_FIND_MATCH - 1)
            );
        }
        else {
            return Find_Str_Str(
                series,
                start,
                index,
                end,
                skip,
                VAL_SERIES(target),
                VAL_INDEX(target),
                len,
                flags & (AM_FIND_MATCH|AM_FIND_CASE)
            );
        }
    }
    else if (IS_BINARY(target)) {
        const REBOOL uncase = FALSE;
        return Find_Byte_Str(
            series,
            start,
            VAL_BIN_AT(target),
            len,
            uncase, // "don't treat case insensitively"
            GET_FLAG(flags, ARG_FIND_MATCH - 1)
        );
    }
    else if (IS_CHAR(target)) {
        return Find_Str_Char(
            VAL_CHAR(target),
            series,
            start,
            index,
            end,
            skip,
            flags
        );
    }
    else if (IS_INTEGER(target)) {
        return Find_Str_Char(
            cast(REBUNI, VAL_INT32(target)),
            series,
            start,
            index,
            end,
            skip,
            flags
        );
    }
    else if (IS_BITSET(target)) {
        return Find_Str_Bitset(
            series,
            start,
            index,
            end,
            skip,
            VAL_SERIES(target),
            flags
        );
    }

    return NOT_FOUND;
}


static REBSER *make_string(const REBVAL *arg, REBOOL make)
{
    REBSER *ser = 0;

    // MAKE <type> 123
    if (make && (IS_INTEGER(arg) || IS_DECIMAL(arg))) {
        ser = Make_Binary(Int32s(arg, 0));
    }
    // MAKE/TO <type> <binary!>
    else if (IS_BINARY(arg)) {
        REBYTE *bp = VAL_BIN_AT(arg);
        REBCNT len = VAL_LEN_AT(arg);
        switch (What_UTF(bp, len)) {
        case 0:
            break;
        case 8: // UTF-8 encoded
            bp  += 3;
            len -= 3;
            break;
        default:
            fail (Error(RE_BAD_UTF8));
        }
        ser = Decode_UTF_String(bp, len, 8); // UTF-8
    }
    // MAKE/TO <type> <any-string>
    else if (ANY_BINSTR(arg)) {
        ser = Copy_String_Slimming(VAL_SERIES(arg), VAL_INDEX(arg), VAL_LEN_AT(arg));
    }
    // MAKE/TO <type> <any-word>
    else if (ANY_WORD(arg)) {
        ser = Copy_Mold_Value(arg, 0 /* opts... MOPT_0? */);
    }
    // MAKE/TO <type> #"A"
    else if (IS_CHAR(arg)) {
        ser = (VAL_CHAR(arg) > 0xff) ? Make_Unicode(2) : Make_Binary(2);
        Append_Codepoint_Raw(ser, VAL_CHAR(arg));
    }
    // MAKE/TO <type> <any-value>
//  else if (IS_BLANK(arg)) {
//      ser = Make_Binary(0);
//  }
    else
        ser = Copy_Form_Value(arg, 1<<MOPT_TIGHT);

    return ser;
}


static REBSER *Make_Binary_BE64(const REBVAL *arg)
{
    REBSER *ser = Make_Binary(9);
    REBI64 n;
    REBINT count;
    REBYTE *bp = BIN_HEAD(ser);

    if (IS_INTEGER(arg)) {
        n = VAL_INT64(arg);
    }
    else {
        assert(IS_DECIMAL(arg));
        n = VAL_DECIMAL_BITS(arg);
    }

    for (count = 7; count >= 0; count--) {
        bp[count] = (REBYTE)(n & 0xff);
        n >>= 8;
    }
    bp[8] = 0;
    SET_SERIES_LEN(ser, 8);

    return ser;
}


static REBSER *make_binary(const REBVAL *arg, REBOOL make)
{
    REBSER *ser;

    // MAKE BINARY! 123
    switch (VAL_TYPE(arg)) {
    case REB_INTEGER:
    case REB_DECIMAL:
        if (make) ser = Make_Binary(Int32s(arg, 0));
        else ser = Make_Binary_BE64(arg);
        break;

    // MAKE/TO BINARY! BINARY!
    case REB_BINARY:
        ser = Copy_Bytes(VAL_BIN_AT(arg), VAL_LEN_AT(arg));
        break;

    // MAKE/TO BINARY! <any-string>
    case REB_STRING:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
//  case REB_ISSUE:
        ser = Make_UTF8_From_Any_String(arg, VAL_LEN_AT(arg), 0);
        break;

    case REB_BLOCK:
        // Join_Binary returns a shared buffer, so produce a copy:
        ser = Copy_Sequence(Join_Binary(arg, -1));
        break;

    // MAKE/TO BINARY! <tuple!>
    case REB_TUPLE:
        ser = Copy_Bytes(VAL_TUPLE(arg), VAL_TUPLE_LEN(arg));
        break;

    // MAKE/TO BINARY! <char!>
    case REB_CHAR:
        ser = Make_Binary(6);
        SET_SERIES_LEN(ser, Encode_UTF8_Char(BIN_HEAD(ser), VAL_CHAR(arg)));
        TERM_SEQUENCE(ser);
        break;

    // MAKE/TO BINARY! <bitset!>
    case REB_BITSET:
        ser = Copy_Bytes(VAL_BIN(arg), VAL_LEN_HEAD(arg));
        break;

    // MAKE/TO BINARY! <image!>
    case REB_IMAGE:
        ser = Make_Image_Binary(arg);
        break;

    case REB_MONEY:
        ser = Make_Binary(12);
        SET_SERIES_LEN(ser, 12);
        deci_to_binary(BIN_HEAD(ser), VAL_MONEY_AMOUNT(arg));
        BIN_HEAD(ser)[12] = 0;
        break;

    default:
        ser = 0;
    }

    return ser;
}


//
//  MAKE_String: C
//
void MAKE_String(REBVAL *out, enum Reb_Kind kind, const REBVAL *def) {
    if (IS_BLOCK(def)) {
        //
        // The construction syntax for making strings or binaries that are
        // preloaded with an offset into the data is #[binary [#{0001} 2]].
        // In R3-Alpha make definitions didn't have to be a single value
        // (they are for compatibility between construction syntax and MAKE
        // in Ren-C).  So the positional syntax was #[binary! #{0001} 2]...
        // while #[binary [#{0001} 2]] would join the pieces together in order
        // to produce #{000102}.  That behavior is not available in Ren-C.

        if (VAL_ARRAY_LEN_AT(def) != 2)
            goto bad_make;

        RELVAL *any_binstr = VAL_ARRAY_AT(def);
        if (!ANY_BINSTR(any_binstr))
            goto bad_make;
        if (IS_BINARY(any_binstr) != LOGICAL(kind == REB_BINARY))
            goto bad_make;

        RELVAL *index = VAL_ARRAY_AT(def) + 1;
        if (!IS_INTEGER(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(any_binstr);
        if (i < 0 || i > cast(REBINT, VAL_LEN_AT(any_binstr)))
            goto bad_make;

        Val_Init_Series_Index(out, kind, VAL_SERIES(any_binstr), i);
        return;
    }

    REBSER *ser; // goto would cross initialization
    ser = (kind != REB_BINARY)
        ? make_string(def, TRUE)
        : make_binary(def, TRUE);

    if (!ser)
        goto bad_make;

    Val_Init_Series_Index(out, kind, ser, 0);
    return;

bad_make:
    fail (Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
void TO_String(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    REBSER *ser = (kind != REB_BINARY)
        ? make_string(arg, FALSE)
        : make_binary(arg, FALSE);

    if (!ser)
        fail (Error_Invalid_Arg(arg));

    Val_Init_Series(out, kind, ser);
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_WIDE = 1 << 0, // String is REBUNI[] and not REBYTE[]
    CC_FLAG_CASE = 1 << 1, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 2 // Reverse sort order
};


//
//  Compare_Chr: C
// 
// This function is called by qsort_r, on behalf of the string sort
// function.  The `thunk` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
static int Compare_Chr(void *thunk, const void *v1, const void *v2)
{
    REBCNT * const flags = cast(REBCNT*, thunk);

    REBUNI c1;
    REBUNI c2;
    if (*flags & CC_FLAG_WIDE) {
        c1 = *cast(const REBUNI*, v1);
        c2 = *cast(const REBUNI*, v2);
    }
    else {
        c1 = cast(REBUNI, *cast(const REBYTE*, v1));
        c2 = cast(REBUNI, *cast(const REBYTE*, v2));
    }

    if (*flags & CC_FLAG_CASE) {
        if (*flags & CC_FLAG_REVERSE)
            return *cast(const REBYTE*, v2) - *cast(const REBYTE*, v1);
        else
            return *cast(const REBYTE*, v1) - *cast(const REBYTE*, v2);
    }
    else {
        if (*flags & CC_FLAG_REVERSE) {
            if (c1 < UNICODE_CASES)
                c1 = UP_CASE(c1);
            if (c2 < UNICODE_CASES)
                c2 = UP_CASE(c2);
            return c2 - c1;
        }
        else {
            if (c1 < UNICODE_CASES)
                c1 = UP_CASE(c1);
            if (c2 < UNICODE_CASES)
                c2 = UP_CASE(c2);
            return c1 - c2;
        }
    }
}


//
//  Sort_String: C
//
static void Sort_String(
    REBVAL *string,
    REBOOL ccase,
    REBVAL *skipv,
    REBVAL *compv,
    REBVAL *part,
    REBOOL all,
    REBOOL rev
) {
    REBCNT len;
    REBCNT skip = 1;
    REBCNT size = 1;
    REBCNT thunk = 0;

    // Determine length of sort:
    len = Partial(string, 0, part);
    if (len <= 1) return;

    // Skip factor:
    if (!IS_VOID(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (Error_Invalid_Arg(skipv));
    }

    // Use fast quicksort library function:
    if (skip > 1) len /= skip, size *= skip;

    if (!VAL_BYTE_SIZE(string)) thunk |= CC_FLAG_WIDE;
    if (ccase) thunk |= CC_FLAG_CASE;
    if (rev) thunk |= CC_FLAG_REVERSE;

    reb_qsort_r(
        VAL_RAW_DATA_AT(string),
        len,
        size * SER_WIDE(VAL_SERIES(string)),
        &thunk,
        Compare_Chr
    );
}


//
//  PD_String: C
//
REBINT PD_String(REBPVS *pvs)
{
    const REBVAL *setval;
    REBINT n;
    REBCNT i;
    REBINT c;
    REBSER *ser = VAL_SERIES(pvs->value);

    if (IS_INTEGER(pvs->selector)) {
        n = Int32(pvs->selector) + VAL_INDEX(pvs->value) - 1;
    }
    else fail (Error_Bad_Path_Select(pvs));

    if (!pvs->opt_setval) {
        if (n < 0 || (REBCNT)n >= SER_LEN(ser)) return PE_NONE;
        if (IS_BINARY(pvs->value)) {
            SET_INTEGER(pvs->store, *BIN_AT(ser, n));
        } else {
            SET_CHAR(pvs->store, GET_ANY_CHAR(ser, n));
        }
        return PE_USE_STORE;
    }

    FAIL_IF_LOCKED_SERIES(ser);
    setval = pvs->opt_setval;

    if (n < 0 || cast(REBCNT, n) >= SER_LEN(ser))
        fail (Error_Bad_Path_Range(pvs));

    if (IS_CHAR(setval)) {
        c = VAL_CHAR(setval);
        if (c > MAX_CHAR)
            fail (Error_Bad_Path_Set(pvs));
    }
    else if (IS_INTEGER(setval)) {
        c = Int32(setval);
        if (c > MAX_CHAR || c < 0)
            fail (Error_Bad_Path_Set(pvs));
    }
    else if (ANY_BINSTR(setval)) {
        i = VAL_INDEX(setval);
        if (i >= VAL_LEN_HEAD(setval))
            fail (Error_Bad_Path_Set(pvs));

        c = GET_ANY_CHAR(VAL_SERIES(setval), i);
    }
    else fail (Error_Bad_Path_Select(pvs));

    if (IS_BINARY(pvs->value)) {
        if (c > 0xff)
            fail (Error_Out_Of_Range(setval));

        BIN_HEAD(ser)[n] = cast(REBYTE, c);
        return PE_OK;
    }

    if (BYTE_SIZE(ser) && c > 0xff)
        Widen_String(ser, TRUE);

    SET_ANY_CHAR(ser, n, c);

    return PE_OK;
}


//
//  PD_File: C
//
// Path dispatch when the left hand side has evaluated to a FILE!.  This
// must be done through evaluations, because a literal file consumes
// slashes as its literal form:
//
//     >> type-of quote %foo/bar
//     == file!
//
//     >> x: %foo
//     >> type-of quote x/bar
//     == path!
//
//     >> x/bar
//     == %foo/bar ;-- a FILE!
//
REBINT PD_File(REBPVS *pvs)
{
    REBSER *ser;
    REBCNT skip;
    REBCNT len;
    REBUNI c;
    REB_MOLD mo;
    CLEARS(&mo);

    if (pvs->opt_setval)
        fail (Error_Bad_Path_Set(pvs));

    ser = Copy_Sequence_At_Position(KNOWN(pvs->value));

    // This makes sure there's always a "/" at the end of the file before
    // appending new material via a selector:
    //
    //     >> x: %foo
    //     >> (x)/("bar")
    //     == %foo/bar
    //
    len = SER_LEN(ser);
    if (len > 0) c = GET_ANY_CHAR(ser, len - 1);
    if (len == 0 || c != '/') Append_Codepoint_Raw(ser, '/');

    Push_Mold(&mo);
    Mold_Value(&mo, pvs->selector, FALSE);

    // The `skip` logic here regarding slashes and backslashes is apparently
    // for an exception to the rule of appending the molded content.  It
    // doesn't want two slashes in a row:
    //
    //     >> x/("/bar")
    //     == %foo/bar
    //
    // !!! Review if this makes sense under a larger philosophy of string
    // path composition.
    //
    c = GET_ANY_CHAR(mo.series, mo.start);
    skip = (c == '/' || c == '\\') ? 1 : 0;

    // !!! Would be nice if there was a better way of doing this that didn't
    // involve reaching into mo.start and mo.series.
    //
    Append_String(
        ser, // dst
        mo.series, // src
        mo.start + skip, // i
        SER_LEN(mo.series) - mo.start - skip // len
    );

    Drop_Mold(&mo);

    Val_Init_Series(pvs->store, VAL_TYPE(pvs->value), ser);

    return PE_USE_STORE;
}


//
//  REBTYPE: C
//
REBTYPE(String)
{
    REBVAL  *value = D_ARG(1);
    REBVAL  *arg = D_ARGC > 1 ? D_ARG(2) : NULL;
    REBINT  index;
    REBINT  tail;
    REBINT  len;
    REBSER  *ser;
    enum Reb_Kind type;
    REBCNT  args;
    REBCNT  ret;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r;
        if (Series_Common_Action_Returns(&r, frame_, action))
            return r;
    }

    // Common setup code for all actions:
    index = cast(REBINT, VAL_INDEX(value));
    tail = cast(REBINT, VAL_LEN_HEAD(value));

    switch (action) {

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        //Modify_String(action, value, arg);
        // Length of target (may modify index): (arg can be anything)
        len = Partial1((action == SYM_CHANGE) ? value : arg, D_ARG(AN_LIMIT));
        index = VAL_INDEX(value);
        args = 0;
        if (IS_BINARY(value)) SET_FLAG(args, AN_SERIES); // special purpose
        if (D_REF(AN_PART)) SET_FLAG(args, AN_PART);
        index = Modify_String(action, VAL_SERIES(value), index, arg, args, len, D_REF(AN_DUP) ? Int32(D_ARG(AN_COUNT)) : 1);
        ENSURE_SERIES_MANAGED(VAL_SERIES(value));
        VAL_INDEX(value) = index;
        break;

    //-- Search:
    case SYM_SELECT:
        ret = ALL_SELECT_REFS;
        goto find;
    case SYM_FIND:
        ret = ALL_FIND_REFS;
find:
        args = Find_Refines(frame_, ret);

        if (IS_BINARY(value)) {
            args |= AM_FIND_CASE;

            if (!IS_BINARY(arg) && !IS_INTEGER(arg) && !IS_BITSET(arg))
                fail (Error(RE_NOT_SAME_TYPE));

            if (IS_INTEGER(arg)) {
                if (VAL_INT64(arg) < 0 || VAL_INT64(arg) > 255)
                    fail (Error_Out_Of_Range(arg));
                len = 1;
            }
        }
        else {
            if (IS_CHAR(arg) || IS_BITSET(arg)) len = 1;
            else if (!ANY_STRING(arg)) {
                Val_Init_String(arg, Copy_Form_Value(arg, 0));
            }
        }

        if (ANY_BINSTR(arg)) len = VAL_LEN_AT(arg);

        if (args & AM_FIND_PART)
            tail = Partial(value, 0, D_ARG(ARG_FIND_LIMIT));
        ret = 1; // skip size
        if (args & AM_FIND_SKIP)
            ret = Partial(value, 0, D_ARG(ARG_FIND_SIZE));

        ret = find_string(VAL_SERIES(value), index, tail, arg, len, args, ret);

        if (ret >= (REBCNT)tail) return R_BLANK;
        if (args & AM_FIND_ONLY) len = 1;

        if (action == SYM_FIND) {
            if (args & (AM_FIND_TAIL | AM_FIND_MATCH)) ret += len;
            VAL_INDEX(value) = ret;
        }
        else {
            ret++;
            if (ret >= (REBCNT)tail) return R_BLANK;
            if (IS_BINARY(value)) {
                SET_INTEGER(value, *BIN_AT(VAL_SERIES(value), ret));
            }
            else
                str_to_char(value, value, ret);
        }
        break;

    //-- Picking:
    case SYM_POKE:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));
    case SYM_PICK:
        len = Get_Num_From_Arg(arg); // Position
        //if (len > 0) index--;
        if (REB_I32_SUB_OF(len, 1, &len)
            || REB_I32_ADD_OF(index, len, &index)
            || index < 0 || index >= tail) {
            if (action == SYM_PICK) return R_BLANK;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == SYM_PICK) {
pick_it:
            if (IS_BINARY(value)) {
                SET_INTEGER(D_OUT, *VAL_BIN_AT_HEAD(value, index));
            }
            else
                str_to_char(D_OUT, value, index);
            return R_OUT;
        }
        else {
            REBUNI c;
            arg = D_ARG(3);
            if (IS_CHAR(arg))
                c = VAL_CHAR(arg);
            else if (
                IS_INTEGER(arg)
                && VAL_INT32(arg) >= 0
                && VAL_INT32(arg) <= MAX_CHAR
            ) {
                c = VAL_INT32(arg);
            }
            else
                fail (Error_Invalid_Arg(arg));

            ser = VAL_SERIES(value);
            if (IS_BINARY(value)) {
                if (c > 0xff) fail (Error_Out_Of_Range(arg));
                BIN_HEAD(ser)[index] = (REBYTE)c;
            }
            else {
                if (BYTE_SIZE(ser) && c > 0xff) Widen_String(ser, TRUE);
                SET_ANY_CHAR(ser, index, c);
            }
            value = arg;
        }
        break;

    case SYM_TAKE:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        if (D_REF(2)) {
            len = Partial(value, 0, D_ARG(3));
            if (len == 0) {
zero_str:
                Val_Init_Series(D_OUT, VAL_TYPE(value), Make_Binary(0));
                return R_OUT;
            }
        } else
            len = 1;

        index = VAL_INDEX(value); // /part can change index

        // take/last:
        if (D_REF(5)) index = tail - len;
        if (index < 0 || index >= tail) {
            if (!D_REF(2)) return R_BLANK;
            goto zero_str;
        }

        ser = VAL_SERIES(value);
        // if no /part, just return value, else return string:
        if (!D_REF(2)) {
            if (IS_BINARY(value)) {
                SET_INTEGER(value, *VAL_BIN_AT_HEAD(value, index));
            } else
                str_to_char(value, value, index);
        }
        else {
            enum Reb_Kind kind = VAL_TYPE(value);
            Val_Init_Series(value, kind, Copy_String_Slimming(ser, index, len));
        }
        Remove_Series(ser, index, len);
        break;

    case SYM_CLEAR:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        if (index < tail) {
            if (index == 0) Reset_Series(VAL_SERIES(value));
            else {
                SET_SERIES_LEN(VAL_SERIES(value), cast(REBCNT, index));
                TERM_SEQUENCE(VAL_SERIES(value));
            }
        }
        break;

    //-- Creation:

    case SYM_COPY:
        len = Partial(value, 0, D_ARG(3)); // Can modify value index.
        ser = Copy_String_Slimming(VAL_SERIES(value), VAL_INDEX(value), len);
        goto ser_exit;

    //-- Bitwise:

    case SYM_AND_T:
    case SYM_OR_T:
    case SYM_XOR_T:
        if (!IS_BINARY(arg)) fail (Error_Invalid_Arg(arg));

        if (VAL_INDEX(value) > VAL_LEN_HEAD(value))
            VAL_INDEX(value) = VAL_LEN_HEAD(value);

        if (VAL_INDEX(arg) > VAL_LEN_HEAD(arg))
            VAL_INDEX(arg) = VAL_LEN_HEAD(arg);

        ser = Xandor_Binary(action, value, arg);
        goto ser_exit;

    case SYM_COMPLEMENT:
        if (!IS_BINARY(value)) fail (Error_Invalid_Arg(value));
        ser = Complement_Binary(value);
        goto ser_exit;

    //-- Special actions:

    case SYM_TRIM:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        // Check for valid arg combinations:
        args = Find_Refines(frame_, ALL_TRIM_REFS);
        if (
            ((args & (AM_TRIM_ALL | AM_TRIM_WITH)) &&
            (args & (AM_TRIM_HEAD | AM_TRIM_TAIL | AM_TRIM_LINES | AM_TRIM_AUTO))) ||
            ((args & AM_TRIM_AUTO) &&
            (args & (AM_TRIM_HEAD | AM_TRIM_TAIL | AM_TRIM_LINES | AM_TRIM_ALL | AM_TRIM_WITH)))
        ) {
            fail (Error(RE_BAD_REFINES));
        }

        Trim_String(VAL_SERIES(value), VAL_INDEX(value), VAL_LEN_AT(value), args, D_ARG(ARG_TRIM_STR));
        break;

    case SYM_SWAP:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        if (VAL_TYPE(value) != VAL_TYPE(arg))
            fail (Error(RE_NOT_SAME_TYPE));

        FAIL_IF_LOCKED_SERIES(VAL_SERIES(arg));

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg))
            swap_chars(value, arg);
        break;

    case SYM_REVERSE:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        len = Partial(value, 0, D_ARG(3));
        if (len > 0) reverse_string(value, len);
        break;

    case SYM_SORT:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        Sort_String(
            value,
            D_REF(2),   // case sensitive
            D_ARG(4),   // skip size
            D_ARG(6),   // comparator
            D_ARG(8),   // part-length
            D_REF(9),   // all fields
            D_REF(10)   // reverse
        );
        break;

    case SYM_RANDOM:
        FAIL_IF_LOCKED_SERIES(VAL_SERIES(value));

        if (D_REF(2)) { // /seed
            //
            // Use the string contents as a seed.  R3-Alpha would try and
            // treat it as byte-sized hence only take half the data into
            // account if it were REBUNI-wide.  This multiplies the number
            // of bytes by the width and offsets by the size.
            //
            Set_Random(
                Compute_CRC(
                    SER_AT_RAW(
                        SER_WIDE(VAL_SERIES(value)),
                        VAL_SERIES(value),
                        VAL_INDEX(value)
                    ),
                    VAL_LEN_AT(value) * SER_WIDE(VAL_SERIES(value))
                )
            );
            return R_VOID;
        }

        if (D_REF(4)) { // /only
            if (index >= tail) return R_BLANK;
            index += (REBCNT)Random_Int(D_REF(3)) % (tail - index);  // /secure
            goto pick_it;
        }
        Shuffle_String(value, D_REF(3));  // /secure
        break;

    default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((IS_FILE(value) || IS_URL(value)))
            return T_Port(frame_, action);

        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    *D_OUT = *value;
    return R_OUT;

ser_exit:
    type = VAL_TYPE(value);
    Val_Init_Series(D_OUT, type, ser);
    return R_OUT;
}
