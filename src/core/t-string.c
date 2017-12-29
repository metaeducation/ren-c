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


#define MAX_QUOTED_STR  50  // max length of "string" before going to { }

REBYTE *Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR && Char_Escapes[c])

REBYTE *URL_Escapes;
#define MAX_URL_CHAR (0x80-1)
#define IS_URL_ESC(c)  ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_URL))
#define IS_FILE_ESC(c) ((c) <= MAX_URL_CHAR && (URL_Escapes[c] & ESC_FILE))

enum {
    ESC_URL = 1,
    ESC_FILE = 2,
    ESC_EMAIL = 4
};


//
//  CT_String: C
//
REBINT CT_String(const RELVAL *a, const RELVAL *b, REBINT mode)
{
    REBINT num;

    if (IS_BINARY(a)) {
        if (NOT(IS_BINARY(b)))
            fail ("Can't compare binary to string, use AS STRING/BINARY!");

        num = Compare_Binary_Vals(a, b);
    }
    else if (IS_BINARY(b))
        fail ("Can't compare binary to string, use AS STRING!/BINARY!");
    else
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
    Init_Char(out, codepoint);
}


static void swap_chars(REBVAL *val1, REBVAL *val2)
{
    REBSER *s1 = VAL_SERIES(val1);
    REBSER *s2 = VAL_SERIES(val2);

    REBUNI c1 = GET_ANY_CHAR(s1, VAL_INDEX(val1));
    REBUNI c2 = GET_ANY_CHAR(s2, VAL_INDEX(val2));

    SET_ANY_CHAR(s1, VAL_INDEX(val1), c2);
    SET_ANY_CHAR(s2, VAL_INDEX(val2), c1);
}

static void reverse_binary(REBVAL *v, REBCNT len)
{
    REBYTE *bp = VAL_BIN_AT(v);

    REBCNT n = 0;
    REBCNT m = len - 1;
    for (; n < len / 2; n++, m--) {
        REBYTE b = bp[n];
        bp[n] = bp[m];
        bp[m] = b;
    }
}


static void reverse_string(REBVAL *v, REBCNT len)
{
    if (len == 0)
        return; // if non-zero, at least one character in the string

    if (Is_String_ASCII(v))
        reverse_binary(v, len);
    else {
        // !!! This is an inefficient method for reversing strings with
        // variable size codepoints.  Better way could work in place:
        //
        // https://stackoverflow.com/q/199260/

        DECLARE_MOLD (mo);
        Push_Mold(mo);

        REBCNT val_len_head = VAL_LEN_HEAD(v);

        REBSER *ser = VAL_SERIES(v);
        REBCHR(const *) up = UNI_LAST(ser); // last exists due to len != 0
        REBCNT n;
        for (n = 0; n < len; ++n) {
            REBUNI c;
            up = BACK_CHR(&c, up);
            Append_Utf8_Codepoint(mo->series, c);
        }

        DECLARE_LOCAL (temp);
        Init_String(temp, Pop_Molded_String(mo));

        // Effectively do a CHANGE/PART to overwrite the reversed portion of
        // the string (from the input value's index to the tail).

        Modify_String(
            v,
            SYM_CHANGE,
            temp,
            0, // not AM_PART, we want to change all len bytes
            len,
            1 // dup count
        );

        // Regardless of whether the whole string was reversed or just some
        // part from the index to the tail, the length shouldn't change.
        //
        assert(VAL_LEN_HEAD(v) == val_len_head);
        UNUSED(val_len_head);
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
    REBSER *ser;

    // MAKE/TO <type> <binary!>
    if (IS_BINARY(arg)) {
        ser = Make_Sized_String_UTF8(
            cs_cast(VAL_BIN_AT(arg)), VAL_LEN_AT(arg)
        );
    }
    // MAKE/TO <type> <any-string>
    else if (ANY_STRING(arg)) {
        ser = Copy_String_At_Len(arg, -1);
    }
    // MAKE/TO <type> <any-word>
    else if (ANY_WORD(arg)) {
        ser = Copy_Mold_Value(arg, MOLD_FLAG_0);
    }
    // MAKE/TO <type> #"A"
    else if (IS_CHAR(arg)) {
        ser = Make_Series_Codepoint(VAL_CHAR(arg));
    }
    else
        ser = Copy_Form_Value(arg, MOLD_FLAG_TIGHT);

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
        ser = Make_UTF8_From_Any_String(arg, VAL_LEN_AT(arg));
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
        ser = Copy_Bytes(VAL_BIN_HEAD(arg), VAL_LEN_HEAD(arg));
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
        if (kind == REB_BINARY)
            Init_Binary(out, Make_Binary(Int32s(def, 0)));
        else
            Init_Any_Series(out, kind, Make_Unicode(Int32s(def, 0)));
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

    if (ser == NULL)
        fail (arg);

    Init_Any_Series(out, kind, ser);
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_CASE = 1 << 0, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 1 // Reverse sort order
};


//
//  Compare_Chr: C
//
// This function is called by qsort_r, on behalf of the string sort
// function.  The `thunk` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
// !!! As of UTF-8 everywhere, this will only work on all-ASCII strings.
//
static int Compare_Chr(void *thunk, const void *v1, const void *v2)
{
    REBCNT * const flags = cast(REBCNT*, thunk);

    REBUNI c1 = cast(REBUNI, *cast(const REBYTE*, v1));
    REBUNI c2 = cast(REBUNI, *cast(const REBYTE*, v2));

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
){
    // !!! System appears to boot without a sort of a string.  A different
    // method will be needed for UTF-8... qsort() cannot work with variable
    // sized codepoints.  However, it could work if all the codepoints were
    // known to be ASCII range in the memory of interest, maybe common case.

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
            fail (skipv);
    }

    // Use fast quicksort library function:
    if (skip > 1) len /= skip, size *= skip;

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
REB_R PD_String(REBPVS *pvs, const REBVAL *picker, const REBVAL *opt_setval)
{
    REBSER *ser = VAL_SERIES(pvs->out);

    // Note: There was some more careful management of overflow here in the
    // PICK and POKE actions, before unification.  But otherwise the code
    // was less thorough.  Consider integrating this bit, though it seems
    // that a more codebase-wide review should be given to the issue.
    //
    /*
        REBINT len = Get_Num_From_Arg(arg);
        if (
            REB_I32_SUB_OF(len, 1, &len)
            || REB_I32_ADD_OF(index, len, &index)
            || index < 0 || index >= tail
        ){
            fail (Error_Out_Of_Range(arg));
        }
    */

    if (opt_setval == NULL) { // PICK-ing
        if (IS_INTEGER(picker)) {
            REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
            if (n < 0 || cast(REBCNT, n) >= SER_LEN(ser)) {
                Init_Void(pvs->out);
                return R_OUT;
            }

            if (IS_BINARY(pvs->out))
                Init_Integer(pvs->out, *BIN_AT(ser, n));
            else
                Init_Char(pvs->out, GET_ANY_CHAR(ser, n));

            return R_OUT;
        }

        if (
            IS_BINARY(pvs->out)
            || NOT(IS_WORD(picker) || ANY_STRING(picker))
        ){
            return R_UNHANDLED;
        }

        // !!! This is a historical and questionable feature, where path
        // picking a string or word or otherwise out of a FILE! or URL! will
        // generate a new FILE! or URL! with a slash in it.
        //
        //     >> x: %foo
        //     >> type of quote x/bar
        //     == path!
        //
        //     >> x/bar
        //     == %foo/bar ;-- a FILE!
        //
        // This can only be done with evaluations, since FILE! and URL! have
        // slashes in their literal form:
        //
        //     >> type of quote %foo/bar
        //     == file!
        //
        // Because Ren-C unified picking and pathing, this somewhat odd
        // feature is now part of PICKing a string from another string.

        REBSER *copy = Copy_Sequence_At_Position(pvs->out);

        // This makes sure there's always a "/" at the end of the file before
        // appending new material via a picker:
        //
        //     >> x: %foo
        //     >> (x)/("bar")
        //     == %foo/bar
        //
        REBCNT len = SER_LEN(copy);
        if (len == 0)
            Append_Codepoint(copy, '/');
        else {
            REBUNI ch_last = GET_ANY_CHAR(copy, len - 1);
            if (ch_last != '/')
                Append_Codepoint(copy, '/');
        }

        DECLARE_MOLD (mo);
        Push_Mold(mo);

        Form_Value(mo, picker);

        // The `skip` logic here regarding slashes and backslashes apparently
        // is for an exception to the rule of appending the molded content.
        // It doesn't want two slashes in a row:
        //
        //     >> x/("/bar")
        //     == %foo/bar
        //
        // !!! Review if this makes sense under a larger philosophy of string
        // path composition.
        //
        REBUNI ch_start = GET_ANY_CHAR(mo->series, mo->start);
        REBCNT skip = (ch_start == '/' || ch_start == '\\') ? 1 : 0;

        // !!! Would be nice if there was a better way of doing this that didn't
        // involve reaching into mo.start and mo.series.
        //
        const REBOOL crlf_to_lf = FALSE;
        Append_UTF8_May_Fail(
            copy, // dst
            cs_cast(BIN_AT(mo->series, mo->start + skip)), // src
            SER_LEN(mo->series) - mo->start - skip, // len
            crlf_to_lf
        );

        Drop_Mold(mo);

        // Note: pvs->out may point to pvs->store
        //
        Init_Any_Series(pvs->out, VAL_TYPE(pvs->out), copy);
        return R_OUT;
    }

    // Otherwise, POKE-ing

    FAIL_IF_READ_ONLY_SERIES(ser);

    if (NOT(IS_INTEGER(picker)))
        return R_UNHANDLED;

    REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
    if (n < 0 || cast(REBCNT, n) >= SER_LEN(ser))
        fail (Error_Out_Of_Range(picker));

    REBINT c;
    if (IS_CHAR(opt_setval)) {
        c = VAL_CHAR(opt_setval);
        if (c > MAX_CHAR)
            return R_UNHANDLED;
    }
    else if (IS_INTEGER(opt_setval)) {
        c = Int32(opt_setval);
        if (c > MAX_CHAR || c < 0)
            return R_UNHANDLED;
    }
    else if (ANY_BINSTR(opt_setval)) {
        REBCNT i = VAL_INDEX(opt_setval);
        if (i >= VAL_LEN_HEAD(opt_setval))
            fail (opt_setval);

        c = GET_ANY_CHAR(VAL_SERIES(opt_setval), i);
    }
    else
        return R_UNHANDLED;

    if (IS_BINARY(pvs->out)) {
        if (c > 0xff)
            fail (Error_Out_Of_Range(opt_setval));

        BIN_HEAD(ser)[n] = cast(REBYTE, c);
        return R_INVISIBLE;
    }

    SET_ANY_CHAR(ser, n, c);

    return R_INVISIBLE;
}


typedef struct REB_Str_Flags {
    REBCNT escape;      // escaped chars
    REBCNT brace_in;    // {
    REBCNT brace_out;   // }
    REBCNT newline;     // lf
    REBCNT quote;       // "
    REBCNT paren;       // (1234)
    REBCNT chr1e;
    REBCNT malign;
} REB_STRF;


static void Sniff_String(REBSER *ser, REBCNT idx, REB_STRF *sf)
{
    // Scan to find out what special chars the string contains?

    REBCHR(const *) up = UNI_AT(ser, idx);

    REBCNT n;
    for (n = idx; n < UNI_LEN(ser); n++) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        switch (c) {
        case '{':
            sf->brace_in++;
            break;

        case '}':
            sf->brace_out++;
            if (sf->brace_out > sf->brace_in)
                sf->malign++;
            break;

        case '"':
            sf->quote++;
            break;

        case '\n':
            sf->newline++;
            break;

        default:
            if (c == 0x1e)
                sf->chr1e += 4; // special case of ^(1e)
            else if (IS_CHR_ESC(c))
                sf->escape++;
            else if (c >= 0x1000)
                sf->paren += 6; // ^(1234)
            else if (c >= 0x100)
                sf->paren += 5; // ^(123)
            else if (c >= 0x80)
                sf->paren += 4; // ^(12)
        }
    }

    if (sf->brace_in != sf->brace_out)
        sf->malign++;
}


//
//  Form_Uni_Hex: C
//
// Fast var-length hex output for uni-chars.
// Returns next position (just past the insert).
//
REBYTE *Form_Uni_Hex(REBYTE *out, REBCNT n)
{
    REBYTE buffer[10];
    REBYTE *bp = &buffer[10];

    while (n != 0) {
        *(--bp) = Hex_Digits[n & 0xf];
        n >>= 4;
    }

    while (bp < &buffer[10])
        *out++ = *bp++;

    return out;
}


//
//  Emit_Uni_Char: C
//
// !!! These heuristics were used in R3-Alpha to decide when to output
// characters in strings as escape for molding.  It's not clear where to
// draw the line with it...should most printable characters just be emitted
// normally in the UTF-8 string with a few exceptions (like newline as ^/)?
//
// For now just preserve what was there, but do it as UTF8 bytes.
//
REBYTE *Emit_Uni_Char(REBYTE *bp, REBUNI chr, REBOOL parened)
{
    // !!! The UTF-8 "Byte Order Mark" is an insidious thing which is not
    // necessary for UTF-8, not recommended by the Unicode standard, and
    // Rebol should not invisibly be throwing it out of strings or file reads:
    //
    // https://stackoverflow.com/q/2223882/
    //
    // But the codepoint (U+FEFF, byte sequence #{EF BB BF}) has no printable
    // representation.  So if it's going to be loaded as-is then it should
    // give some hint that it's there.
    //
    // !!! 0x1e is "record separator" which is handled specially too.  The
    // following rationale is suggested by @MarkI:
    //
    //     "Rebol special-cases RS because traditionally it is escape-^
    //      but Rebol uses ^ to indicate escaping so it has to do
    //      something else with that one."

    if (chr >= 0x7F || chr == 0x1E || chr == 0xFEFF) {
        //
        // non ASCII, "^" (RS), or byte-order-mark must be ^(00) escaped.
        //
        // !!! Comment here said "do not AND with the above"
        //
        if (parened || chr == 0x1E || chr == 0xFEFF) {
            *bp++ = '^';
            *bp++ = '(';
            bp = Form_Uni_Hex(bp, chr);
            *bp++ = ')';
            return bp;
        }

        // fallthrough...
    }
    else if (IS_CHR_ESC(chr)) {
        *bp++ = '^';
        *bp++ = Char_Escapes[chr];
        return bp;
    }

    bp += Encode_UTF8_Char(bp, chr);
    return bp;
}


static void Mold_String_Series(REB_MOLD *mo, const RELVAL *v)
{
    REBSER *out = mo->series;

    REBCNT len = VAL_LEN_AT(v);
    REBSER *series = VAL_SERIES(v);
    REBCNT index = VAL_INDEX(v);

    if (index >= VAL_LEN_HEAD(v)) {
        Append_Unencoded(out, "\"\"");
        return;
    }

    REB_STRF sf;
    CLEARS(&sf);
    Sniff_String(series, index, &sf);
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED))
        sf.paren = 0;

    REBCHR(const *) up = UNI_AT(series, index);

    // If it is a short quoted string, emit it as "string"
    //
    if (len <= MAX_QUOTED_STR && sf.quote == 0 && sf.newline < 3) {
        REBYTE *dp = Prep_Mold_Overestimated( // not accurate, must terminate
            mo,
            (len * 4) // 4 character max for unicode encoding of 1 char
                + sf.newline + sf.escape + sf.paren + sf.chr1e + 2
        );

        *dp++ = '"';

        REBCNT n;
        for (n = index; n < VAL_LEN_HEAD(v); n++) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            dp = Emit_Uni_Char(
                dp, c, GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED)
            );
        }

        *dp++ = '"';
        *dp = '\0';

        TERM_BIN_LEN(out, dp - BIN_HEAD(out));
        return;
    }

    // It is a braced string, emit it as {string}:
    if (!sf.malign)
        sf.brace_in = sf.brace_out = 0;

    REBYTE *dp = Prep_Mold_Overestimated( // not accurate, must terminate
        mo,
        (len * 4) // 4 bytes maximum for UTF-8 encoding
            + sf.brace_in + sf.brace_out
            + sf.escape + sf.paren + sf.chr1e
            + 2
    );

    *dp++ = '{';

    REBCNT n;
    for (n = index; n < VAL_LEN_HEAD(v); n++) {
        REBUNI c;
        up = NEXT_CHR(&c, up);

        switch (c) {
        case '{':
        case '}':
            if (sf.malign) {
                *dp++ = '^';
                *dp++ = c;
                break;
            }
            // fall through
        case '\n':
        case '"':
            *dp++ = c;
            break;

        default:
            dp = Emit_Uni_Char(
                dp, c, GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED)
            );
        }
    }

    *dp++ = '}';
    *dp = '\0';

    TERM_BIN_LEN(out, dp - BIN_HEAD(out));
}


// R3-Alpha's philosophy on URL! was:
//
// "Only alphanumerics [0-9a-zA-Z], the special characters $-_.+!*'(),
//  and reserved characters used for their reserved purposes may be used
//  unencoded within a URL."
//
// http://www.blooberry.com/indexdot/html/topics/urlencoding.htm
//
// Ren-C is working with a different model, where URL! is generic to custom
// schemes which may or may not follow the RFC for Internet URLs.  It also
// wishes to preserve round-trip copy-and-paste from URL bars in browsers
// to source and back.  Encoding concerns are handled elsewhere.
//
static void Mold_Url(REB_MOLD *mo, const RELVAL *v)
{
    REBSER *series = VAL_SERIES(v);
    REBCNT len = VAL_LEN_AT(v);
    REBYTE *dp = Prep_Mold_Overestimated(mo, len * 4); // 4 bytes max UTF-8

    REBCNT n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n)
        *dp++ = GET_ANY_CHAR(series, n);

    *dp = '\0';

    SET_SERIES_LEN(mo->series, dp - BIN_HEAD(mo->series)); // correction
}


static void Mold_File(REB_MOLD *mo, const RELVAL *v)
{
    REBSER *series = VAL_SERIES(v);
    REBCNT len = VAL_LEN_AT(v);

    REBCNT estimated_bytes = 4 * len; // UTF-8 characters are max 4 bytes

    // Compute extra space needed for hex encoded characters:
    //
    REBCNT n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            estimated_bytes -= 1; // %xx is 3 characters instead of 4
    }

    ++estimated_bytes; // room for % at start

    REBYTE *dp = Prep_Mold_Overestimated(mo, estimated_bytes);

    *dp++ = '%';

    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            dp = Form_Hex_Esc(dp, c); // c => %xx
        else
            *dp++ = c;
    }

    *dp = '\0';

    SET_SERIES_LEN(mo->series, dp - BIN_HEAD(mo->series)); // correction
}


static void Mold_Tag(REB_MOLD *mo, const RELVAL *v)
{
    Append_Utf8_Codepoint(mo->series, '<');

    REBSIZ offset;
    REBSIZ size;
    REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));
    Append_Utf8_Utf8(mo->series, cs_cast(BIN_AT(temp, offset)), size);

    Append_Utf8_Codepoint(mo->series, '>');
}


//
//  MF_Binary: C
//
void MF_Binary(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    UNUSED(form);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0)
        Pre_Mold(mo, v); // #[binary!

    REBCNT len = VAL_LEN_AT(v);

    REBSER *enbased;
    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
    default:
    case 16: {
        const REBOOL brk = LOGICAL(len > 32);
        enbased = Encode_Base16(VAL_BIN_AT(v), len, brk);
        break; }

    case 64: {
        const REBOOL brk = LOGICAL(len > 64);
        Append_Unencoded(mo->series, "64");
        enbased = Encode_Base64(VAL_BIN_AT(v), len, brk);
        break; }

    case 2: {
        const REBOOL brk = LOGICAL(len > 8);
        Append_Utf8_Codepoint(mo->series, '2');
        enbased = Encode_Base2(VAL_BIN_AT(v), len, brk);
        break; }
    }

    Append_Unencoded(mo->series, "#{");
    Append_Utf8_Utf8(mo->series, cs_cast(BIN_HEAD(enbased)), BIN_LEN(enbased));
    Append_Unencoded(mo->series, "}");

    Free_Series(enbased);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0)
        Post_Mold(mo, v);
}


//
//  MF_String: C
//
void MF_String(REB_MOLD *mo, const RELVAL *v, REBOOL form)
{
    REBSER *s = mo->series;

    assert(ANY_STRING(v));

    // Special format for MOLD/ALL string series when not at head
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
        Pre_Mold(mo, v); // e.g. #[file! part

        DECLARE_LOCAL (head);
        VAL_RESET_HEADER(head, REB_STRING);
        head->payload.any_series.series = VAL_SERIES(v);
        VAL_INDEX(head) = 0;

        Mold_String_Series(mo, head);

        Post_Mold(mo, v);
        return;
    }

    // The R3-Alpha forming logic was that every string type besides TAG!
    // would form with no delimiters, e.g. `form #foo` is just foo
    //
    if (form && NOT(IS_TAG(v))) {
        REBSIZ offset;
        REBSIZ size;
        REBSER *temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));

        Append_Utf8_Utf8(mo->series, cs_cast(BIN_AT(temp, offset)), size);
        return;
    }

    switch(VAL_TYPE(v)) {
    case REB_STRING:
        Mold_String_Series(mo, v);
        break;

    case REB_FILE:
        if (VAL_LEN_AT(v) == 0) {
            Append_Unencoded(s, "%\"\"");
            break;
        }
        Mold_File(mo, v);
        break;

    case REB_EMAIL:
    case REB_URL:
        Mold_Url(mo, v);
        break;

    case REB_TAG:
        Mold_Tag(mo, v);
        break;

    default:
        panic (v);
    }
}


//
//  REBTYPE: C
//
// Common action handler for BINARY! and ANY-STRING!
//
// !!! BINARY! seems different enough to warrant its own handler.
//
REBTYPE(String)
{
    REBVAL *v = D_ARG(1);
    assert(IS_BINARY(v) || ANY_STRING(v));

    REBSER *ser;
    TRASH_POINTER_IF_DEBUG(ser); // `goto return_ser;` will return this

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Common operations for any series type (length, head, etc.)
    {
        REB_R r = Series_Common_Action_Maybe_Unhandled(frame_, action);
        if (r != R_UNHANDLED)
            return r;
    }

    // Common setup code for all actions:
    //
    REBINT index = cast(REBINT, VAL_INDEX(v));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(v));

    switch (action) {

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        if (REF(only)) {
            // !!! Doesn't pay attention...all string appends are /ONLY
        }

        REBINT len;
        Partial1(
            (action == SYM_CHANGE) ? v : arg,
            ARG(limit),
            cast(REBCNT*, &len)
        );

        REBFLGS flags = 0;
        if (REF(part))
            flags |= AM_PART;

        if (IS_BINARY(v))
            VAL_INDEX(v) = Modify_Binary(
                v,
                action,
                arg,
                flags,
                len,
                REF(dup) ? Int32(ARG(count)) : 1
            );
        else
            VAL_INDEX(v) = Modify_String(
                v,
                action,
                arg,
                flags,
                len,
                REF(dup) ? Int32(ARG(count)) : 1
            );

        break; }

    //-- Search:
    case SYM_SELECT_P:
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
        if (IS_BINARY(v)) {
            flags |= AM_FIND_CASE;

            if (!IS_BINARY(arg) && !IS_INTEGER(arg) && !IS_BITSET(arg))
                fail (Error_Not_Same_Type_Raw());

            if (IS_INTEGER(arg)) {
                if (VAL_INT64(arg) < 0 || VAL_INT64(arg) > 255)
                    fail (Error_Out_Of_Range(arg));
                len = 1;
            }
            else
                len = VAL_LEN_AT(arg);
        }
        else {
            if (IS_CHAR(arg) || IS_BITSET(arg))
                len = 1;
            else {
                if (!IS_STRING(arg)) {
                    //
                    // !! This FORM creates a temporary value that is handed
                    // over to the GC.  Not only could the temporary value be
                    // unmanaged (and freed), a more efficient matching could
                    // be done of `FIND "<abc...z>" <abc...z>` without having
                    // to create an entire series just for the delimiters.
                    //
                    REBSER *copy = Copy_Form_Value(arg, 0);
                    Init_String(arg, copy);
                }
                len = VAL_LEN_AT(arg);
            }
        }

        if (REF(part))
            tail = Partial(v, 0, ARG(limit));

        REBCNT skip;
        if (REF(skip))
            skip = Partial(v, 0, ARG(size));
        else
            skip = 1;

        REBCNT ret = find_string(
            VAL_SERIES(v), index, tail, arg, len, flags, skip
        );

        if (ret >= cast(REBCNT, tail))
            return R_BLANK;

        if (REF(only))
            len = 1;

        if (action == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(v) = ret;
        }
        else {
            ret++;
            if (ret >= cast(REBCNT, tail))
                return R_BLANK;
            if (IS_BINARY(v)) {
                Init_Integer(v, *BIN_AT(VAL_SERIES(v), ret));
            }
            else
                str_to_char(v, v, ret);
        }
        break; }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Partial(v, 0, ARG(limit));
            if (len == 0) {
                Init_Any_Series(D_OUT, VAL_TYPE(v), Make_Binary(0));
                return R_OUT;
            }
        } else
            len = 1;

        // Note that /PART can change index

        if (REF(last)) {
            if (tail - len < 0)
                VAL_INDEX(v) = 0;
            else
                VAL_INDEX(v) = cast(REBCNT, tail - len);
        }

        if (VAL_INDEX(v) == 0 || cast(REBINT, VAL_INDEX(v)) >= tail) {
            if (NOT(REF(part)))
                return R_BLANK;
            Init_Any_Series(D_OUT, VAL_TYPE(v), Make_Binary(0));
            return R_OUT;
        }

        ser = VAL_SERIES(v);
        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (NOT(REF(part))) {
            if (IS_BINARY(v)) {
                Init_Integer(v, *VAL_BIN_AT(v));
            } else
                str_to_char(v, v, index);
        }
        else {
            enum Reb_Kind kind = VAL_TYPE(v);
            Init_Any_Series(v, kind, Copy_String_At_Len(v, len));
        }
        Remove_Series(ser, index, len);
        break; }

    case SYM_CLEAR: {
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        if (index < tail) {
            if (index == 0)
                Reset_Sequence(VAL_SERIES(v));
            else
                TERM_SEQUENCE_LEN(VAL_SERIES(v), cast(REBCNT, index));
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
        REBINT len = Partial(v, 0, ARG(limit)); // Can modify value index.

        if (IS_BINARY(v))
            ser = Copy_Sequence_At_Len(VAL_SERIES(v), VAL_INDEX(v), len);
        else
            ser = Copy_String_At_Len(v, len);
        goto return_ser; }

    //-- Bitwise:

    case SYM_AND_T:
    case SYM_OR_T:
    case SYM_XOR_T: {
        if (NOT(IS_BINARY(arg)))
            fail (arg);

        if (VAL_INDEX(v) > VAL_LEN_HEAD(v))
            VAL_INDEX(v) = VAL_LEN_HEAD(v);

        if (VAL_INDEX(arg) > VAL_LEN_HEAD(arg))
            VAL_INDEX(arg) = VAL_LEN_HEAD(arg);

        ser = Xandor_Binary(action, v, arg);
        goto return_ser; }

    case SYM_COMPLEMENT: {
        if (NOT(IS_BINARY(v)))
            fail (v);

        ser = Complement_Binary(v);
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
        if (NOT(IS_BINARY(v)))
            fail (v);

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        REBINT amount;
        if (IS_INTEGER(arg))
            amount = VAL_INT32(arg);
        else if (IS_BINARY(arg))
            fail (arg); // should work
        else
            fail (arg); // what about other types?

        if (action == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) { // adding or subtracting 0 works, even #{} + 0
            Move_Value(D_OUT, v);
            return R_OUT;
        }
        else if (VAL_LEN_AT(v) == 0) // add/subtract to #{} otherwise
            fail (Error_Overflow_Raw());

        while (amount != 0) {
            REBCNT wheel = VAL_LEN_HEAD(v) - 1;
            while (TRUE) {
                REBYTE *b = VAL_BIN_AT_HEAD(v, wheel);
                if (amount > 0) {
                    if (*b == 255) {
                        if (wheel == VAL_INDEX(v))
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
                        if (wheel == VAL_INDEX(v))
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
        Move_Value(D_OUT, v);
        return R_OUT; }

    //-- Special actions:

    case SYM_TRIM: {
        INCLUDE_PARAMS_OF_TRIM;
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        UNUSED(PAR(series));

        ser = VAL_SERIES(v);

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
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(arg));

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg))
            swap_chars(v, arg);
        break; }

    case SYM_REVERSE: {
        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        REBINT len = Partial(v, 0, D_ARG(3));
        if (len > 0) {
            if (IS_BINARY(v))
                reverse_binary(v, len);
            else
                reverse_string(v, len);
        }
        break; }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

        UNUSED(PAR(series));
        UNUSED(REF(skip));
        UNUSED(REF(compare));
        UNUSED(REF(part));

        if (REF(all)) // Not Supported
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (ANY_STRING(v) && NOT(Is_String_ASCII(v)))
            fail ("UTF-8 Everywhere: String sorting temporarily unavailable");

        Sort_String(
            v,
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

        FAIL_IF_READ_ONLY_SERIES(VAL_SERIES(v));

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
                        SER_WIDE(VAL_SERIES(v)),
                        VAL_SERIES(v),
                        VAL_INDEX(v)
                    ),
                    VAL_LEN_AT(v) * SER_WIDE(VAL_SERIES(v))
                )
            );
            return R_VOID;
        }

        if (REF(only)) {
            if (index >= tail)
                return R_BLANK;
            index += (REBCNT)Random_Int(REF(secure)) % (tail - index);
            if (IS_BINARY(v)) { // same as PICK
                Init_Integer(D_OUT, *VAL_BIN_AT_HEAD(v, index));
            }
            else
                str_to_char(D_OUT, v, index);
            return R_OUT;
        }

        if (ANY_STRING(v) && NOT(Is_String_ASCII(v)))
            fail ("UTF-8 Everywhere: String shuffle temporarily unavailable");

        Shuffle_String(v, REF(secure));
        break; }

    default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((IS_FILE(v) || IS_URL(v)))
            return T_Port(frame_, action);

        fail (Error_Illegal_Action(VAL_TYPE(v), action));
    }

    Move_Value(D_OUT, v);
    return R_OUT;

return_ser:
    Init_Any_Series(D_OUT, VAL_TYPE(v), ser);
    return R_OUT;
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    Char_Escapes = ALLOC_N_ZEROFILL(REBYTE, MAX_ESC_CHAR + 1);

    REBYTE *cp = Char_Escapes;
    REBYTE c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    Char_Escapes[cast(REBYTE, '\t')] = '-'; // tab
    Char_Escapes[cast(REBYTE, '\n')] = '/'; // line feed
    Char_Escapes[cast(REBYTE, '"')] = '"';
    Char_Escapes[cast(REBYTE, '^')] = '^';

    URL_Escapes = ALLOC_N_ZEROFILL(REBYTE, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        URL_Escapes[c] = ESC_URL | ESC_FILE;

    const REBYTE *dc = cb_cast(";%\"()[]{}<>");

    for (c = LEN_BYTES(dc); c > 0; c--)
        URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_String: C
//
void Shutdown_String(void)
{
    FREE_N(REBYTE, MAX_ESC_CHAR + 1, Char_Escapes);
    FREE_N(REBYTE, MAX_URL_CHAR + 1, URL_Escapes);
}
