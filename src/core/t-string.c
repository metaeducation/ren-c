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
    REBCNT target_len,
    REBCNT flags,
    REBINT skip
) {
    assert(end >= index);

    if (target_len > end - index) // series not long enough to have target
        return NOT_FOUND;

    REBCNT start = index;

    if (flags & (AM_FIND_REVERSE | AM_FIND_LAST)) {
        skip = -1;
        start = 0;
        if (flags & AM_FIND_LAST) index = end - target_len;
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
                target_len,
                NOT(flags & AM_FIND_CASE),
                LOGICAL(flags & AM_FIND_MATCH)
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
                target_len,
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
            target_len,
            uncase, // "don't treat case insensitively"
            LOGICAL(flags & AM_FIND_MATCH)
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


static REBSER *MAKE_TO_String_Common(const REBVAL *arg)
{
    REBSER *ser = 0;

    // MAKE/TO <type> <binary!>
    if (IS_BINARY(arg)) {
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
            fail (Error_Bad_Utf8_Raw());
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
    else
        ser = Copy_Form_Value(arg, 1 << MOPT_TIGHT);

    return ser;
}


static REBSER *Make_Binary_BE64(const REBVAL *arg)
{
    REBSER *ser = Make_Binary(8);

    REBYTE *bp = BIN_HEAD(ser);

    REBI64 i;
    REBDEC d;
    const REBYTE *cp;
    if (IS_INTEGER(arg)) {
        assert(sizeof(REBI64) == 8);
        i = VAL_INT64(arg);
        cp = cast(const REBYTE*, &i);
    }
    else {
        assert(sizeof(REBDEC) == 8);
        d = VAL_DECIMAL(arg);
        cp = cast(const REBYTE*, &d);
    }

#ifdef ENDIAN_LITTLE
    REBCNT n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[7 - n];
#elif defined(ENDIAN_BIG)
    REBCNT n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[n];
#else
    #error "Unsupported CPU endian"
#endif

    TERM_BIN_LEN(ser, 8);
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
        TERM_SEQUENCE_LEN(ser, Encode_UTF8_Char(BIN_HEAD(ser), VAL_CHAR(arg)));
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
        deci_to_binary(BIN_HEAD(ser), VAL_MONEY_AMOUNT(arg));
        TERM_SEQUENCE_LEN(ser, 12);
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
    REBSER *ser; // goto would cross initialization

    if (IS_INTEGER(def)) {
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make string! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        //
        ser = Make_Binary(Int32s(def, 0));
        Init_Any_Series(out, kind, ser);
        return;
    }
    else if (IS_BLOCK(def)) {
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

        Init_Any_Series_At(out, kind, VAL_SERIES(any_binstr), i);
        return;
    }

    if (kind == REB_BINARY)
        ser = make_binary(def, TRUE);
    else
        ser = MAKE_TO_String_Common(def);

    if (!ser)
        goto bad_make;

    Init_Any_Series_At(out, kind, ser, 0);
    return;

bad_make:
    fail (Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
void TO_String(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    REBSER *ser;
    if (kind == REB_BINARY)
        ser = make_binary(arg, FALSE);
    else
        ser = MAKE_TO_String_Common(arg);

    if (!ser)
        fail (Error_Invalid_Arg(arg));

    Init_Any_Series(out, kind, ser);
}


//
//  to-string: native [
//
//  {Like TO STRING! but with additional options.}
//
//      value [any-value!]
//          {Value to convert to a string.}
//      /astral
//          {Provide special handling for codepoints bigger than 0xFFFF}
//      handler [function! string! char! blank!]
//          {If function, receives integer argument of large codepoint value}
//  ]
//
REBNATIVE(to_string)
{
    INCLUDE_PARAMS_OF_TO_STRING;

    REBVAL *value = ARG(value);

    if (NOT(REF(astral)) || NOT(IS_BINARY(value))) {
        TO_String(D_OUT, REB_STRING, value); // just act like TO STRING!
        return R_OUT;
    }

    // Ordinarily, UTF8 decoding is done into the unicode buffer.  The number
    // of unicode codepoints is guaranteed to be <= the number of UTF8 bytes,
    // so the length is used as a conservative bound.  Since we don't know
    // how many astral codepoints there are, it's not easy to know the size
    // in advance.  So the series may be expanded multiple times.
    //
    REBSER *ser = Make_Unicode(VAL_LEN_AT(value));
    if (Decode_UTF8_Maybe_Astral_Throws(
        D_OUT,
        ser,
        VAL_BIN_AT(value),
        VAL_LEN_AT(value),
        TRUE, // cr/lf => lf conversion is done by TO_String (review)
        ARG(handler)
    )){
        return R_OUT_IS_THROWN;
    }

    // !!! Note also that since this conversion does not go through the
    // unicode buffer, so it's not copied out with "slimming" if it turns out
    // to not contain wide chars.

    Init_String(D_OUT, ser);
    return R_OUT;
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
    REBOOL rev
) {
    if (!IS_VOID(compv))
        fail (Error_Bad_Refine_Raw(compv)); // !!! didn't seem to be supported (?)

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

    FAIL_IF_READ_ONLY_SERIES(ser);
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
//  File_Or_Url_Path_Dispatch: C
//
// Path dispatch when the left hand side has evaluated to a FILE! or URL!.
// This must be done through evaluations, because a literal file consumes
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
REBSER *File_Or_Url_Path_Dispatch(REBPVS *pvs)
{
    if (pvs->opt_setval)
        fail (Error_Bad_Path_Set(pvs));

    REBSER *ser = Copy_Sequence_At_Position(KNOWN(pvs->value));

    // This makes sure there's always a "/" at the end of the file before
    // appending new material via a selector:
    //
    //     >> x: %foo
    //     >> (x)/("bar")
    //     == %foo/bar
    //
    REBUNI ch_last;
    REBCNT len = SER_LEN(ser);
    if (len > 0)
        ch_last = GET_ANY_CHAR(ser, len - 1);
    if (len == 0 || ch_last != '/')
        Append_Codepoint_Raw(ser, '/');

    REB_MOLD mo;
    CLEARS(&mo);
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
    REBUNI ch_start = GET_ANY_CHAR(mo.series, mo.start);
    REBCNT skip = (ch_start == '/' || ch_start == '\\') ? 1 : 0;

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

    return ser;
}


//
//  PD_File: C
//
REBINT PD_File(REBPVS *pvs) {
    assert(VAL_TYPE(pvs->value) == REB_FILE);
    REBSER *ser = File_Or_Url_Path_Dispatch(pvs);
    Init_File(pvs->store, ser);
    return PE_USE_STORE;
}


//
//  PD_Url: C
//
REBINT PD_Url(REBPVS *pvs) {
    assert(VAL_TYPE(pvs->value) == REB_URL);
    REBSER *ser = File_Or_Url_Path_Dispatch(pvs);
    Init_Url(pvs->store, ser);
    return PE_USE_STORE;
}


//
//  REBTYPE: C
//
REBTYPE(String)
{
    REBSER *ser;
    TRASH_POINTER_IF_DEBUG(ser); // `goto return_ser;` will return this

    REBVAL  *value = D_ARG(1);
    REBVAL  *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, action);
        if (r != R_UNHANDLED)
            return r;
    }

    // Common setup code for all actions:
    //
    REBINT index = cast(REBINT, VAL_INDEX(value));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(value));

    switch (action) {

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        if (REF(only)) {
            // !!! Doesn't pay attention...all string appends are /ONLY
        }

        REBINT len;
        Partial1(
            (action == SYM_CHANGE) ? value : arg,
            ARG(limit),
            cast(REBCNT*, &len)
        );
        index = VAL_INDEX(value);

        REBFLGS flags = 0;
        if (IS_BINARY(value))
            flags |= AM_BINARY_SERIES;
        if (REF(part))
            flags |= AM_PART;
        index = Modify_String(
            action,
            VAL_SERIES(value),
            index,
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(count)) : 1
        );
        ENSURE_SERIES_MANAGED(VAL_SERIES(value));
        VAL_INDEX(value) = index;
        break; }

    //-- Search:
    case SYM_SELECT:
    case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(reverse) ? AM_FIND_REVERSE : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
            | (REF(last) ? AM_FIND_LAST : 0)
            | (REF(tail) ? AM_FIND_TAIL : 0)
        );

        REBINT len;
        if (IS_BINARY(value)) {
            flags |= AM_FIND_CASE;

            if (!IS_BINARY(arg) && !IS_INTEGER(arg) && !IS_BITSET(arg))
                fail (Error_Not_Same_Type_Raw());

            if (IS_INTEGER(arg)) {
                if (VAL_INT64(arg) < 0 || VAL_INT64(arg) > 255)
                    fail (Error_Out_Of_Range(arg));
                len = 1;
            }
        }
        else {
            if (IS_CHAR(arg) || IS_BITSET(arg))
                len = 1;
            else if (!IS_STRING(arg)) {
                //
                // !! This FORM creates a temporary value that is then handed
                // over to the GC.  Not only could the temporary value be
                // unmanaged (and freed), a more efficient matching could
                // be done e.g. of `FIND "<abc...z>" <abc...z>` without having
                // to create an entire series just to include the delimiters.
                //
                REBSER *copy = Copy_Form_Value(arg, 0);
                Init_String(arg, copy);
            }
        }

        if (ANY_BINSTR(arg))
            len = VAL_LEN_AT(arg);

        if (REF(part))
            tail = Partial(value, 0, ARG(limit));

        REBCNT skip;
        if (REF(skip))
            skip = Partial(value, 0, ARG(size));
        else
            skip = 1;

        REBCNT ret = find_string(
            VAL_SERIES(value), index, tail, arg, len, flags, skip
        );

        if (ret >= (REBCNT)tail)
            return R_BLANK;

        if (REF(only))
            len = 1;

        if (action == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
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
        break; }

    //-- Picking:
    case SYM_POKE:
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));
    case SYM_PICK: {
        REBINT len = Get_Num_From_Arg(arg); // Position
        //if (len > 0) index--;
        if (REB_I32_SUB_OF(len, 1, &len)
            || REB_I32_ADD_OF(index, len, &index)
            || index < 0 || index >= tail) {
            if (action == SYM_PICK) return R_BLANK;
            fail (Error_Out_Of_Range(arg));
        }
        if (action == SYM_PICK) {
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
        break; }

    case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Partial(value, 0, ARG(limit));
            if (len == 0) {
                Init_Any_Series(D_OUT, VAL_TYPE(value), Make_Binary(0));
                return R_OUT;
            }
        } else
            len = 1;

        index = VAL_INDEX(value); // /PART can change index

        if (REF(last))
            index = tail - len;
        if (index < 0 || index >= tail) {
            if (NOT(REF(part)))
                return R_BLANK;
            Init_Any_Series(D_OUT, VAL_TYPE(value), Make_Binary(0));
            return R_OUT;
        }

        ser = VAL_SERIES(value);

        // if no /PART, just return value, else return string
        //
        if (NOT(REF(part))) {
            if (IS_BINARY(value)) {
                SET_INTEGER(value, *VAL_BIN_AT_HEAD(value, index));
            } else
                str_to_char(value, value, index);
        }
        else {
            enum Reb_Kind kind = VAL_TYPE(value);
            Init_Any_Series(
                value, kind, Copy_String_Slimming(ser, index, len)
            );
        }
        Remove_Series(ser, index, len);
        break; }

    case SYM_CLEAR: {
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        if (index < tail) {
            if (index == 0)
                Reset_Sequence(VAL_SERIES(value));
            else
                TERM_SEQUENCE_LEN(VAL_SERIES(value), cast(REBCNT, index));
        }
        break; }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        UNUSED(REF(part));
        REBINT len = Partial(value, 0, ARG(limit)); // Can modify value index.
        ser = Copy_String_Slimming(VAL_SERIES(value), VAL_INDEX(value), len);
        goto return_ser; }

    //-- Bitwise:

    case SYM_AND_T:
    case SYM_OR_T:
    case SYM_XOR_T: {
        if (!IS_BINARY(arg)) fail (Error_Invalid_Arg(arg));

        if (VAL_INDEX(value) > VAL_LEN_HEAD(value))
            VAL_INDEX(value) = VAL_LEN_HEAD(value);

        if (VAL_INDEX(arg) > VAL_LEN_HEAD(arg))
            VAL_INDEX(arg) = VAL_LEN_HEAD(arg);

        ser = Xandor_Binary(action, value, arg);
        goto return_ser; }

    case SYM_COMPLEMENT: {
        if (!IS_BINARY(value)) fail (Error_Invalid_Arg(value));
        ser = Complement_Binary(value);
        goto return_ser; }

    // Arithmetic operations are allowed on BINARY!, because it's too limiting
    // to not allow `#{4B} + 1` => `#{4C}`.  Allowing the operations requires
    // a default semantic of binaries as unsigned arithmetic, since one
    // does not want `#{FF} + 1` to be #{FE}.  It uses a big endian
    // interpretation, so `#{00FF} + 1` is #{0100}
    //
    // Since Rebol is a language with mutable semantics by default, `add x y`
    // will mutate x by default (if X is not an immediate type).  `+` is an
    // enfixing of `add-of` which copies the first argument before adding.
    //
    // To try and maximize usefulness, the semantic chosen is that any
    // arithmetic that would go beyond the bounds of the length is considered
    // an overflow.  Hence the size of the result binary will equal the size
    // of the original binary.  This means that `#{0100} - 1` is #{00FF},
    // not #{FF}.
    //
    // !!! The code below is extremely slow and crude--using an odometer-style
    // loop to do the math.  What's being done here is effectively "bigint"
    // math, and it might be that it would share code with whatever big
    // integer implementation was used; e.g. integers which exceeded the size
    // of the platform REBI64 would use BINARY! under the hood.

    case SYM_SUBTRACT:
    case SYM_ADD: {
        if (!IS_BINARY(value))
            fail (Error_Invalid_Arg(value));

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        REBINT amount;
        if (IS_INTEGER(arg))
            amount = VAL_INT32(arg);
        else if (IS_BINARY(arg))
            fail (Error_Invalid_Arg(arg)); // should work
        else
            fail (Error_Invalid_Arg(arg)); // what about other types?

        if (action == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) { // adding or subtracting 0 works, even #{} + 0
            Move_Value(D_OUT, value);
            return R_OUT;
        }
        else if (VAL_LEN_AT(value) == 0) // add/subtract to #{} otherwise
            fail (Error_Overflow_Raw());

        while (amount != 0) {
            REBCNT wheel = VAL_LEN_HEAD(value) - 1;
            while (TRUE) {
                REBYTE *b = VAL_BIN_AT_HEAD(value, wheel);
                if (amount > 0) {
                    if (*b == 255) {
                        if (wheel == VAL_INDEX(value))
                            fail (Error_Overflow_Raw());

                        *b = 0;
                        --wheel;
                        continue;
                    }
                    ++(*b);
                    --amount;
                    break;
                }
                else {
                    if (*b == 0) {
                        if (wheel == VAL_INDEX(value))
                            fail (Error_Overflow_Raw());

                        *b = 255;
                        --wheel;
                        continue;
                    }
                    --(*b);
                    ++amount;
                    break;
                }
            }
        }
        Move_Value(D_OUT, value);
        return R_OUT; }

    //-- Special actions:

    case SYM_TRIM: {
        INCLUDE_PARAMS_OF_TRIM;
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        UNUSED(PAR(series));

        ser = VAL_SERIES(value);

        if (REF(all) || REF(with)) {
            if (REF(head) || REF(tail) || REF(lines) || REF(auto))
                fail (Error_Bad_Refines_Raw());

            Whitespace_Replace_With(ser, index, tail, ARG(str));
        }
        else if (REF(auto)) {
            if (REF(head) || REF(tail) || REF(lines) || REF(all) || REF(with))
                fail (Error_Bad_Refines_Raw());

            Trim_String_Auto(ser, index, tail);
        }
        else if (REF(lines)) {
            Trim_String_Lines(ser, index, tail);
        }
        else {
            Trim_String_Head_Tail(
                ser,
                index,
                tail,
                REF(head),
                REF(tail)
            );
        }
        break; }

    case SYM_SWAP: {
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        if (VAL_TYPE(value) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(arg));

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg))
            swap_chars(value, arg);
        break; }

    case SYM_REVERSE: {
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        REBINT len = Partial(value, 0, D_ARG(3));
        if (len > 0)
            reverse_string(value, len);
        break; }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        UNUSED(PAR(series));
        UNUSED(REF(skip));
        UNUSED(REF(compare));
        UNUSED(REF(part));

        if (REF(all)) {// Not Supported
            fail (Error_Bad_Refine_Raw(ARG(all)));
        }

        Sort_String(
            value,
            REF(case),
            ARG(size), // skip size (void if not /SKIP)
            ARG(comparator), // (void if not /COMPARE)
            ARG(limit),   // (void if not /PART)
            REF(reverse)
        );
        break; }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(value));

        if (REF(seed)) {
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

        if (REF(only)) {
            if (index >= tail)
                return R_BLANK;
            index += (REBCNT)Random_Int(REF(secure)) % (tail - index);
            if (IS_BINARY(value)) { // same as PICK
                SET_INTEGER(D_OUT, *VAL_BIN_AT_HEAD(value, index));
            }
            else
                str_to_char(D_OUT, value, index);
            return R_OUT;
        }
        Shuffle_String(value, REF(secure));
        break; }

    default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((IS_FILE(value) || IS_URL(value)))
            return T_Port(frame_, action);

        fail (Error_Illegal_Action(VAL_TYPE(value), action));
    }

    Move_Value(D_OUT, value);
    return R_OUT;

return_ser:
    Init_Any_Series(D_OUT, VAL_TYPE(value), ser);
    return R_OUT;
}
