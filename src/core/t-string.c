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

Byte *Char_Escapes;
#define MAX_ESC_CHAR (0x60-1) // size of escape table
#define IS_CHR_ESC(c) ((c) <= MAX_ESC_CHAR && Char_Escapes[c])

Byte *URL_Escapes;
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
REBINT CT_String(const Cell* a, const Cell* b, REBINT mode)
{
    REBINT num;

    if (Is_Binary(a)) {
        if (not Is_Binary(b))
            fail ("Can't compare binary to string, use AS STRING/BINARY!");

        num = Compare_Binary_Vals(a, b);
    }
    else if (Is_Binary(b))
        fail ("Can't compare binary to string, use AS STRING!/BINARY!");
    else
        num = Compare_String_Vals(a, b, mode != 1);

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
static void str_to_char(Value* out, Value* val, REBLEN idx)
{
    // Note: out may equal val, do assignment in two steps
    REBUNI codepoint = GET_ANY_CHAR(VAL_SERIES(val), idx);
    Init_Char(out, codepoint);
}


static void swap_chars(Value* val1, Value* val2)
{
    Series* s1 = VAL_SERIES(val1);
    Series* s2 = VAL_SERIES(val2);

    REBUNI c1 = GET_ANY_CHAR(s1, VAL_INDEX(val1));
    REBUNI c2 = GET_ANY_CHAR(s2, VAL_INDEX(val2));

    SET_ANY_CHAR(s1, VAL_INDEX(val1), c2);
    SET_ANY_CHAR(s2, VAL_INDEX(val2), c1);
}

static void reverse_binary(Value* v, REBLEN len)
{
    Byte *bp = Cell_Binary_At(v);

    REBLEN n = 0;
    REBLEN m = len - 1;
    for (; n < len / 2; n++, m--) {
        Byte b = bp[n];
        bp[n] = bp[m];
        bp[m] = b;
    }
}


static void reverse_string(Value* v, REBLEN len)
{
    if (len == 0)
        return; // if non-zero, at least one character in the string

    // !!! This is an inefficient method for reversing strings with
    // variable size codepoints.  Better way could work in place:
    //
    // https://stackoverflow.com/q/199260/

    DECLARE_MOLD (mo);
    Push_Mold(mo);

    REBLEN val_len_head = VAL_LEN_HEAD(v);

    String* ser = Cell_String(v);
    Ucs2(const*) up = String_Last(ser); // last exists due to len != 0
    REBLEN n;
    for (n = 0; n < len; ++n) {
        REBUNI c;
        up = Ucs2_Back(&c, up);
        Append_Utf8_Codepoint(mo->series, c);
    }

    DECLARE_VALUE (temp);
    Init_Text(temp, Pop_Molded_String(mo));

    // Effectively do a CHANGE/PART to overwrite the reversed portion of
    // the string (from the input value's index to the tail).

    DECLARE_VALUE (verb);
    Init_Word(verb, Canon(SYM_CHANGE));
    Modify_String(
        v,
        unwrap(Cell_Word_Id(verb)),
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


static REBLEN find_string(
    Series* series,
    REBLEN index,
    REBLEN end,
    Value* target,
    REBLEN target_len,
    REBLEN flags,
    REBINT skip
) {
    assert(end >= index);

    if (target_len > end - index) // series not long enough to have target
        return NOT_FOUND;

    REBLEN start = index;

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
                cast(Blob*, series),
                start,
                Cell_Binary_At(target),
                target_len,
                not (flags & AM_FIND_CASE),
                did (flags & AM_FIND_MATCH)
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
    else if (Is_Binary(target)) {
        const bool uncase = false;
        return Find_Byte_Str(
            cast(Blob*, series),
            start,
            Cell_Binary_At(target),
            target_len,
            uncase, // "don't treat case insensitively"
            did (flags & AM_FIND_MATCH)
        );
    }
    else if (Is_Char(target)) {
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
    else if (Is_Integer(target)) {
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
    else if (Is_Bitset(target)) {
        return Find_Str_Bitset(
            series,
            start,
            index,
            end,
            skip,
            Cell_Bitset(target),
            flags
        );
    }

    return NOT_FOUND;
}


static Series* MAKE_TO_String_Common(const Value* arg)
{
    Series* ser;

    // MAKE/TO <type> <binary!>
    if (Is_Binary(arg)) {
        ser = Make_Sized_String_UTF8(
            cs_cast(Cell_Binary_At(arg)), VAL_LEN_AT(arg)
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
    else if (Is_Char(arg)) {
        ser = Make_Ser_Codepoint(VAL_CHAR(arg));
    }
    else
        ser = Copy_Form_Value(arg, MOLD_FLAG_TIGHT);

    return ser;
}


static Blob* Make_Blob_BE64(const Value* arg)
{
    Blob* ser = Make_Blob(8);

    Byte *bp = Blob_Head(ser);

    REBI64 i;
    REBDEC d;
    const Byte *cp;
    if (Is_Integer(arg)) {
        assert(sizeof(REBI64) == 8);
        i = VAL_INT64(arg);
        cp = cast(const Byte*, &i);
    }
    else {
        assert(sizeof(REBDEC) == 8);
        d = VAL_DECIMAL(arg);
        cp = cast(const Byte*, &d);
    }

#ifdef ENDIAN_LITTLE
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[7 - n];
#elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[n];
#else
    #error "Unsupported CPU endian"
#endif

    Term_Blob_Len(ser, 8);
    return ser;
}


static Blob* make_binary(const Value* arg, bool make)
{
    Blob* ser;

    // MAKE BINARY! 123
    switch (VAL_TYPE(arg)) {
    case REB_INTEGER:
    case REB_DECIMAL:
        if (make) ser = Make_Blob(Int32s(arg, 0));
        else ser = Make_Blob_BE64(arg);
        break;

    // MAKE/TO BINARY! BINARY!
    case REB_BINARY:
        ser = Copy_Bytes(Cell_Binary_At(arg), VAL_LEN_AT(arg));
        break;

    // MAKE/TO BINARY! <any-string>
    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG:
//  case REB_ISSUE:
        ser = Make_Utf8_From_Cell_String_At_Limit(arg, VAL_LEN_AT(arg));
        break;

    case REB_BLOCK:
        // Join_Binary returns a shared buffer, so produce a copy:
        ser = cast(
            Blob*,
            Copy_Sequence_Core(Join_Binary(arg, -1), SERIES_FLAGS_NONE)
        );
        break;

    // MAKE/TO BINARY! <tuple!>
    case REB_TUPLE:
        ser = Copy_Bytes(VAL_TUPLE(arg), VAL_TUPLE_LEN(arg));
        break;

    // MAKE/TO BINARY! <char!>
    case REB_CHAR:
        ser = Make_Blob(6);
        TERM_SEQUENCE_LEN(ser, Encode_UTF8_Char(Blob_Head(ser), VAL_CHAR(arg)));
        break;

    // MAKE/TO BINARY! <bitset!>
    case REB_BITSET:
        ser = Copy_Bytes(Cell_Binary_Head(arg), VAL_LEN_HEAD(arg));
        break;

    case REB_MONEY:
        ser = Make_Blob(12);
        deci_to_binary(Blob_Head(ser), VAL_MONEY_AMOUNT(arg));
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
REB_R MAKE_String(Value* out, enum Reb_Kind kind, const Value* def) {
    Series* ser; // goto would cross initialization

    if (Is_Integer(def)) {
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make text! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        //
        if (kind == REB_BINARY)
            return Init_Binary(out, Make_Blob(Int32s(def, 0)));
        else
            return Init_Any_Series(out, kind, Make_String(Int32s(def, 0)));
    }
    else if (Is_Block(def)) {
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

        Cell* any_binstr = Cell_Array_At(def);
        if (!ANY_BINSTR(any_binstr))
            goto bad_make;
        if (Is_Binary(any_binstr) != (kind == REB_BINARY))
            goto bad_make;

        Cell* index = Cell_Array_At(def) + 1;
        if (!Is_Integer(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(any_binstr);
        if (i < 0 || i > cast(REBINT, VAL_LEN_AT(any_binstr)))
            goto bad_make;

        return Init_Any_Series_At(out, kind, VAL_SERIES(any_binstr), i);
    }

    if (kind == REB_BINARY)
        ser = make_binary(def, true);
    else
        ser = MAKE_TO_String_Common(def);

    if (!ser)
        goto bad_make;

    return Init_Any_Series_At(out, kind, ser, 0);

  bad_make:
    fail (Error_Bad_Make(kind, def));
}


//
//  TO_String: C
//
REB_R TO_String(Value* out, enum Reb_Kind kind, const Value* arg)
{
    Series* ser;
    if (kind == REB_BINARY)
        ser = make_binary(arg, false);
    else
        ser = MAKE_TO_String_Common(arg);

    if (ser == nullptr)
        fail (Error_Invalid(arg));

    return Init_Any_Series(out, kind, ser);
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
    REBLEN * const flags = cast(REBLEN*, thunk);

    REBUNI c1 = cast(REBUNI, *cast(const Byte*, v1));
    REBUNI c2 = cast(REBUNI, *cast(const Byte*, v2));

    if (*flags & CC_FLAG_CASE) {
        if (*flags & CC_FLAG_REVERSE)
            return *cast(const Byte*, v2) - *cast(const Byte*, v1);
        else
            return *cast(const Byte*, v1) - *cast(const Byte*, v2);
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
    Value* string,
    bool ccase,
    Value* skipv,
    Value* compv,
    Value* part,
    bool rev
){
    // !!! System appears to boot without a sort of a string.  A different
    // method will be needed for UTF-8... qsort() cannot work with variable
    // sized codepoints.  However, it could work if all the codepoints were
    // known to be ASCII range in the memory of interest, maybe common case.

    if (not IS_NULLED(compv))
        fail (Error_Bad_Refine_Raw(compv)); // !!! didn't seem to be supported (?)

    REBLEN skip = 1;
    REBLEN size = 1;
    REBLEN thunk = 0;

    REBLEN len = Part_Len_May_Modify_Index(string, part); // length of sort
    if (len <= 1)
        return;

    // Skip factor:
    if (not IS_NULLED(skipv)) {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 || len % skip != 0 || skip > len)
            fail (Error_Invalid(skipv));
    }

    // Use fast quicksort library function:
    if (skip > 1) len /= skip, size *= skip;

    if (ccase) thunk |= CC_FLAG_CASE;
    if (rev) thunk |= CC_FLAG_REVERSE;

    reb_qsort_r(
        VAL_RAW_DATA_AT(string),
        len,
        size * Series_Wide(VAL_SERIES(string)),
        &thunk,
        Compare_Chr
    );
}


//
//  PD_String: C
//
REB_R PD_String(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    Series* ser = VAL_SERIES(pvs->out);

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

    if (opt_setval == nullptr) {  // PICK-ing
        if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
            REBINT n = Int32(picker);
            if (n == 0)
                return nullptr; // Rebol2/Red convention, 0 is bad pick
            if (n < 0)
                ++n; // Rebol2/Red convention, `pick tail "abc" -1` is #"c"
            n += VAL_INDEX(pvs->out) - 1;
            if (n < 0 or cast(REBLEN, n) >= Series_Len(ser))
                return nullptr;

            if (Is_Binary(pvs->out))
                Init_Integer(pvs->out, *Blob_At(cast(Blob*, ser), n));
            else
                Init_Char(pvs->out, GET_ANY_CHAR(ser, n));

            return pvs->out;
        }

        if (
            Is_Binary(pvs->out)
            or not (Is_Word(picker) or ANY_STRING(picker))
        ){
            return R_UNHANDLED;
        }

        // !!! This is a historical and questionable feature, where path
        // picking a string or word or otherwise out of a FILE! or URL! will
        // generate a new FILE! or URL! with a slash in it.
        //
        //     >> x: %foo
        //     >> type of the x/bar
        //     == path!
        //
        //     >> x/bar
        //     == %foo/bar ;-- a FILE!
        //
        // This can only be done with evaluations, since FILE! and URL! have
        // slashes in their literal form:
        //
        //     >> type of the %foo/bar
        //     == file!
        //
        // Because Ren-C unified picking and pathing, this somewhat odd
        // feature is now part of PICKing a string from another string.

        String* copy = cast(String*, Copy_Sequence_At_Position(pvs->out));

        // This makes sure there's always a "/" at the end of the file before
        // appending new material via a picker:
        //
        //     >> x: %foo
        //     >> (x)/("bar")
        //     == %foo/bar
        //
        REBLEN len = Series_Len(copy);
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
        REBLEN skip = (ch_start == '/' || ch_start == '\\') ? 1 : 0;

        // !!! Would be nice if there was a better way of doing this that didn't
        // involve reaching into mo.start and mo.series.
        //
        const bool crlf_to_lf = false;
        Append_UTF8_May_Fail(
            copy, // dst
            cs_cast(Blob_At(mo->series, mo->start + skip)), // src
            Series_Len(mo->series) - mo->start - skip, // len
            crlf_to_lf
        );

        Drop_Mold(mo);

        // Note: pvs->out may point to pvs->store
        //
        Init_Any_Series(pvs->out, VAL_TYPE(pvs->out), copy);
        return pvs->out;
    }

    // Otherwise, POKE-ing

    Fail_If_Read_Only_Series(ser);

    if (not Is_Integer(picker))
        return R_UNHANDLED;

    REBINT n = Int32(picker);
    if (n == 0)
        fail (Error_Out_Of_Range(picker)); // Rebol2/Red convention for 0
    if (n < 0)
        ++n;
    n += VAL_INDEX(pvs->out) - 1;
    if (n < 0 or cast(REBLEN, n) >= Series_Len(ser))
        fail (Error_Out_Of_Range(picker));

    REBINT c;
    if (Is_Char(opt_setval)) {
        c = VAL_CHAR(opt_setval);
        if (c > MAX_CHAR)
            return R_UNHANDLED;
    }
    else if (Is_Integer(opt_setval)) {
        c = Int32(opt_setval);
        if (c > MAX_CHAR || c < 0)
            return R_UNHANDLED;
    }
    else if (ANY_BINSTR(opt_setval)) {
        REBLEN i = VAL_INDEX(opt_setval);
        if (i >= VAL_LEN_HEAD(opt_setval))
            fail (Error_Invalid(opt_setval));

        c = GET_ANY_CHAR(VAL_SERIES(opt_setval), i);
    }
    else
        return R_UNHANDLED;

    if (Is_Binary(pvs->out)) {
        if (c > 0xff)
            fail (Error_Out_Of_Range(opt_setval));

        Blob_Head(cast(Blob*, ser))[n] = cast(Byte, c);
        return R_INVISIBLE;
    }

    SET_ANY_CHAR(ser, n, c);

    return R_INVISIBLE;
}


typedef struct REB_Str_Flags {
    REBLEN escape;      // escaped chars
    REBLEN brace_in;    // {
    REBLEN brace_out;   // }
    REBLEN newline;     // lf
    REBLEN quote;       // "
    REBLEN paren;       // (1234)
    REBLEN chr1e;
    REBLEN malign;
} REB_STRF;


static void Sniff_String(String* str, REBLEN idx, REB_STRF *sf)
{
    // Scan to find out what special chars the string contains?

    Ucs2(const*) up = String_At(str, idx);

    REBLEN n;
    for (n = idx; n < String_Len(str); n++) {
        REBUNI c;
        up = Ucs2_Next(&c, up);

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
Byte *Form_Uni_Hex(Byte *out, REBLEN n)
{
    Byte buffer[10];
    Byte *bp = &buffer[10];

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
Byte *Emit_Uni_Char(Byte *bp, REBUNI chr, bool parened)
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


//
//  Mold_Text_Series_At: C
//
void Mold_Text_Series_At(
    REB_MOLD *mo,
    String* str,
    REBLEN index
){
    if (index >= String_Len(str)) {
        Append_Unencoded(mo->series, "\"\"");
        return;
    }

    REBLEN len_at = String_Len(str) - index;

    REB_STRF sf;
    CLEARS(&sf);
    Sniff_String(str, index, &sf);
    if (NOT_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED))
        sf.paren = 0;

    Ucs2(const*) up = String_At(str, index);

    // If it is a short quoted string, emit it as "string"
    //
    if (len_at <= MAX_QUOTED_STR && sf.quote == 0 && sf.newline < 3) {
        Byte *dp = Prep_Mold_Overestimated( // not accurate, must terminate
            mo,
            (len_at * 4) // 4 character max for unicode encoding of 1 char
                + sf.newline + sf.escape + sf.paren + sf.chr1e + 2
        );

        *dp++ = '"';

        REBLEN n;
        for (n = index; n < String_Len(str); n++) {
            REBUNI c;
            up = Ucs2_Next(&c, up);
            dp = Emit_Uni_Char(
                dp, c, GET_MOLD_FLAG(mo, MOLD_FLAG_NON_ANSI_PARENED)
            );
        }

        *dp++ = '"';
        *dp = '\0';

        Term_Blob_Len(mo->series, dp - Blob_Head(mo->series));
        return;
    }

    // It is a braced string, emit it as {string}:
    if (!sf.malign)
        sf.brace_in = sf.brace_out = 0;

    Byte *dp = Prep_Mold_Overestimated( // not accurate, must terminate
        mo,
        (len_at * 4) // 4 bytes maximum for UTF-8 encoding
            + sf.brace_in + sf.brace_out
            + sf.escape + sf.paren + sf.chr1e
            + 2
    );

    *dp++ = '{';

    REBLEN n;
    for (n = index; n < String_Len(str); n++) {
        REBUNI c;
        up = Ucs2_Next(&c, up);

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

    Term_Blob_Len(mo->series, dp - Blob_Head(mo->series));
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
static void Mold_Url(REB_MOLD *mo, const Cell* v)
{
    Series* series = VAL_SERIES(v);
    REBLEN len = VAL_LEN_AT(v);
    Byte *dp = Prep_Mold_Overestimated(mo, len * 4); // 4 bytes max UTF-8

    REBLEN n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n)
        *dp++ = GET_ANY_CHAR(series, n);

    *dp = '\0';

    Set_Series_Len(mo->series, dp - Blob_Head(mo->series)); // correction
}


static void Mold_File(REB_MOLD *mo, const Cell* v)
{
    Series* series = VAL_SERIES(v);
    REBLEN len = VAL_LEN_AT(v);

    REBLEN estimated_bytes = 4 * len; // UTF-8 characters are max 4 bytes

    // Compute extra space needed for hex encoded characters:
    //
    REBLEN n;
    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            estimated_bytes -= 1; // %xx is 3 characters instead of 4
    }

    ++estimated_bytes; // room for % at start

    Byte *dp = Prep_Mold_Overestimated(mo, estimated_bytes);

    *dp++ = '%';

    for (n = VAL_INDEX(v); n < VAL_LEN_HEAD(v); ++n) {
        REBUNI c = GET_ANY_CHAR(series, n);
        if (IS_FILE_ESC(c))
            dp = Form_Hex_Esc(dp, c); // c => %xx
        else
            *dp++ = c;
    }

    *dp = '\0';

    Set_Series_Len(mo->series, dp - Blob_Head(mo->series)); // correction
}


static void Mold_Tag(REB_MOLD *mo, const Cell* v)
{
    Append_Utf8_Codepoint(mo->series, '<');

    REBSIZ offset;
    REBSIZ size;
    Blob* temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));
    Append_Utf8_Utf8(mo->series, cs_cast(Blob_At(temp, offset)), size);

    Append_Utf8_Codepoint(mo->series, '>');
}


//
//  MF_Binary: C
//
void MF_Binary(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0)
        Pre_Mold(mo, v); // #[binary!

    REBLEN len = VAL_LEN_AT(v);

    Blob* enbased;
    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
    default:
    case 16: {
        const bool brk = (len > 32);
        enbased = Encode_Base16(Cell_Binary_At(v), len, brk);
        break; }

    case 64: {
        const bool brk = (len > 64);
        Append_Unencoded(mo->series, "64");
        enbased = Encode_Base64(Cell_Binary_At(v), len, brk);
        break; }

    case 2: {
        const bool brk = (len > 8);
        Append_Utf8_Codepoint(mo->series, '2');
        enbased = Encode_Base2(Cell_Binary_At(v), len, brk);
        break; }
    }

    Append_Unencoded(mo->series, "#{");
    Append_Utf8_Utf8(
        mo->series,
        cs_cast(Blob_Head(enbased)),
        Blob_Len(enbased)
    );
    Append_Unencoded(mo->series, "}");

    Free_Unmanaged_Series(enbased);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0)
        Post_Mold(mo, v);
}


//
//  MF_String: C
//
void MF_String(REB_MOLD *mo, const Cell* v, bool form)
{
    Blob* s = mo->series;

    assert(ANY_STRING(v));

    // Special format for MOLD/ALL string series when not at head
    //
    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) && VAL_INDEX(v) != 0) {
        Pre_Mold(mo, v); // e.g. #[file! part
        Mold_Text_Series_At(mo, Cell_String(v), 0);
        Post_Mold(mo, v);
        return;
    }

    // The R3-Alpha forming logic was that every string type besides TAG!
    // would form with no delimiters, e.g. `form #foo` is just foo
    //
    if (form and not Is_Tag(v)) {
        REBSIZ offset;
        REBSIZ size;
        Blob* temp = Temp_UTF8_At_Managed(&offset, &size, v, VAL_LEN_AT(v));

        Append_Utf8_Utf8(mo->series, cs_cast(Blob_At(temp, offset)), size);
        return;
    }

    switch (VAL_TYPE(v)) {
    case REB_TEXT:
        Mold_Text_Series_At(mo, Cell_String(v), VAL_INDEX(v));
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
    Value* v = D_ARG(1);
    assert(Is_Binary(v) || ANY_STRING(v));

    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;

    // Common operations for any series type (length, head, etc.)
    //
    REB_R r = Series_Common_Action_Maybe_Unhandled(level_, verb);
    if (r != R_UNHANDLED)
        return r;

    // Common setup code for all actions:
    //
    REBINT index = cast(REBINT, VAL_INDEX(v));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(v));

    Option(SymId) sym = Cell_Word_Id(verb);
    switch (sym) {
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        UNUSED(REF(only)); // all strings appends are /ONLY...currently unused

        REBLEN len; // length of target
        if (Cell_Word_Id(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(limit));
        else
            len = Part_Len_Append_Insert_May_Modify_Index(arg, ARG(limit));

        // Note that while inserting or removing NULL is a no-op, CHANGE with
        // a /PART can actually erase data.
        //
        if (IS_NULLED(arg) and len == 0) { // only nulls bypass write attempts
            if (sym == SYM_APPEND) // append always returns head
                VAL_INDEX(v) = 0;
            RETURN (v); // don't fail on read only if it would be a no-op
        }
        Fail_If_Read_Only_Series(VAL_SERIES(v));

        REBFLGS flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        if (Is_Binary(v)) {
            if (REF(line))
                fail ("APPEND+INSERT+CHANGE cannot use /LINE with BINARY!");

            VAL_INDEX(v) = Modify_Binary(
                v,
                unwrap(Cell_Word_Id(verb)),
                arg,
                flags,
                len,
                REF(dup) ? Int32(ARG(count)) : 1
            );
        }
        else {
            if (REF(line))
                flags |= AM_LINE;

            VAL_INDEX(v) = Modify_String(
                v,
                unwrap(Cell_Word_Id(verb)),
                arg,
                flags,
                len,
                REF(dup) ? Int32(ARG(count)) : 1
            );
        }
        RETURN (v); }

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
        if (Is_Binary(v)) {
            flags |= AM_FIND_CASE;

            if (!Is_Binary(arg) && !Is_Integer(arg) && !Is_Bitset(arg))
                fail (Error_Not_Same_Type_Raw());

            if (Is_Integer(arg)) {
                if (VAL_INT64(arg) < 0 || VAL_INT64(arg) > 255)
                    fail (Error_Out_Of_Range(arg));
                len = 1;
            }
            else
                len = VAL_LEN_AT(arg);
        }
        else {
            if (Is_Char(arg) or Is_Bitset(arg))
                len = 1;
            else {
                if (not Is_Text(arg)) {
                    //
                    // !! This FORM creates a temporary value that is handed
                    // over to the GC.  Not only could the temporary value be
                    // unmanaged (and freed), a more efficient matching could
                    // be done of `FIND "<abc...z>" <abc...z>` without having
                    // to create an entire series just for the delimiters.
                    //
                    Series* copy = Copy_Form_Value(arg, 0);
                    Init_Text(arg, copy);
                }
                len = VAL_LEN_AT(arg);
            }
        }

        if (REF(part))
            tail = Part_Tail_May_Modify_Index(v, ARG(limit));

        REBLEN skip;
        if (REF(skip))
            skip = Part_Len_May_Modify_Index(v, ARG(size));
        else
            skip = 1;

        REBLEN ret = find_string(
            VAL_SERIES(v), index, tail, arg, len, flags, skip
        );

        if (ret >= cast(REBLEN, tail))
            return nullptr;

        if (REF(only))
            len = 1;

        if (Cell_Word_Id(verb) == SYM_FIND) {
            if (REF(tail) || REF(match))
                ret += len;
            VAL_INDEX(v) = ret;
        }
        else {
            ret++;
            if (ret >= cast(REBLEN, tail))
                return nullptr;

            if (Is_Binary(v)) {
                Init_Integer(v, *Blob_At(Cell_Blob(v), ret));
            }
            else
                str_to_char(v, v, ret);
        }
        RETURN (v); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        Fail_If_Read_Only_Series(VAL_SERIES(v));

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(limit));
            if (len == 0)
                return Init_Any_Series(OUT, VAL_TYPE(v), Make_Blob(0));
        } else
            len = 1;

        // Note that /PART can change index

        if (REF(last)) {
            if (tail - len < 0) {
                VAL_INDEX(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX(v) = cast(REBLEN, tail - len);
        }

        if (cast(REBINT, VAL_INDEX(v)) >= tail) {
            if (not REF(part))
                return nullptr;
            return Init_Any_Series(OUT, VAL_TYPE(v), Make_Blob(0));
        }

        Series* ser = VAL_SERIES(v);
        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (not REF(part)) {
            if (Is_Binary(v))
                Init_Integer(OUT, *Cell_Binary_At(v));
            else
                str_to_char(OUT, v, VAL_INDEX(v));
        }
        else {
            enum Reb_Kind kind = VAL_TYPE(v);
            if (Is_Binary(v)) {
                Init_Binary(
                    OUT,
                    Copy_Sequence_At_Len(VAL_SERIES(v), VAL_INDEX(v), len)
                );
            } else
                Init_Any_Series(OUT, kind, Copy_String_At_Len(v, len));
        }
        Remove_Series(ser, VAL_INDEX(v), len);
        return OUT; }

    case SYM_CLEAR: {
        Fail_If_Read_Only_Series(VAL_SERIES(v));

        if (index < tail) {
            if (index == 0)
                Reset_Sequence(VAL_SERIES(v));
            else
                TERM_SEQUENCE_LEN(VAL_SERIES(v), cast(REBLEN, index));
        }
        RETURN (v); }

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

        REBINT len = Part_Len_May_Modify_Index(v, ARG(limit));
        UNUSED(REF(part)); // checked by if limit is nulled

        Series* ser;
        if (Is_Binary(v))
            ser = Copy_Sequence_At_Len(VAL_SERIES(v), VAL_INDEX(v), len);
        else
            ser = Copy_String_At_Len(v, len);
        return Init_Any_Series(OUT, VAL_TYPE(v), ser); }

    //-- Bitwise:

    case SYM_INTERSECT:
    case SYM_UNION:
    case SYM_DIFFERENCE: {
        if (not Is_Binary(arg))
            fail (Error_Invalid(arg));

        if (VAL_INDEX(v) > VAL_LEN_HEAD(v))
            VAL_INDEX(v) = VAL_LEN_HEAD(v);

        if (VAL_INDEX(arg) > VAL_LEN_HEAD(arg))
            VAL_INDEX(arg) = VAL_LEN_HEAD(arg);

        return Init_Any_Series(
            OUT,
            VAL_TYPE(v),
            Xandor_Binary(verb, v, arg)); }

    case SYM_COMPLEMENT: {
        if (not Is_Binary(v))
            fail (Error_Invalid(v));

        return Init_Any_Series(OUT, VAL_TYPE(v), Complement_Binary(v)); }

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
        if (not Is_Binary(v))
            fail (Error_Invalid(v));

        Fail_If_Read_Only_Series(VAL_SERIES(v));

        REBINT amount;
        if (Is_Integer(arg))
            amount = VAL_INT32(arg);
        else if (Is_Binary(arg))
            fail (Error_Invalid(arg)); // should work
        else
            fail (Error_Invalid(arg)); // what about other types?

        if (Cell_Word_Id(verb) == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) { // adding or subtracting 0 works, even #{} + 0
            Copy_Cell(OUT, v);
            return OUT;
        }
        else if (VAL_LEN_AT(v) == 0) // add/subtract to #{} otherwise
            fail (Error_Overflow_Raw());

        while (amount != 0) {
            REBLEN wheel = VAL_LEN_HEAD(v) - 1;
            while (true) {
                Byte *b = Cell_Binary_At_Head(v, wheel);
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
        RETURN (v); }

    //-- Special actions:

    case SYM_SWAP: {
        Fail_If_Read_Only_Series(VAL_SERIES(v));

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        Fail_If_Read_Only_Series(VAL_SERIES(arg));

        if (index < tail && VAL_INDEX(arg) < VAL_LEN_HEAD(arg))
            swap_chars(v, arg);
        RETURN (v); }

    case SYM_REVERSE: {
        Fail_If_Read_Only_Series(VAL_SERIES(v));

        REBINT len = Part_Len_May_Modify_Index(v, D_ARG(3));
        if (len > 0) {
            if (Is_Binary(v))
                reverse_binary(v, len);
            else
                reverse_string(v, len);
        }
        RETURN (v); }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        Fail_If_Read_Only_Series(VAL_SERIES(v));

        UNUSED(PAR(series));
        UNUSED(REF(skip));
        UNUSED(REF(compare));
        UNUSED(REF(part));

        if (REF(all)) // Not Supported
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (ANY_STRING(v))  // always true here
            fail ("UTF-8 Everywhere: String sorting temporarily unavailable");

        Sort_String(
            v,
            REF(case),
            ARG(size), // skip size (void if not /SKIP)
            ARG(comparator), // (void if not /COMPARE)
            ARG(limit),   // (void if not /PART)
            REF(reverse)
        );
        RETURN (v); }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        Fail_If_Read_Only_Series(VAL_SERIES(v));

        if (REF(seed)) {
            //
            // Use the string contents as a seed.  R3-Alpha would try and
            // treat it as byte-sized hence only take half the data into
            // account if it were REBUNI-wide.  This multiplies the number
            // of bytes by the width and offsets by the size.
            //
            Set_Random(
                Compute_CRC24(
                    Series_Data_At(
                        Series_Wide(VAL_SERIES(v)),
                        VAL_SERIES(v),
                        VAL_INDEX(v)
                    ),
                    VAL_LEN_AT(v) * Series_Wide(VAL_SERIES(v))
                )
            );
            return nullptr;
        }

        if (REF(only)) {
            if (index >= tail)
                return nullptr;
            index += (REBLEN)Random_Int(REF(secure)) % (tail - index);
            if (Is_Binary(v)) // same as PICK
                return Init_Integer(OUT, *Cell_Binary_At_Head(v, index));

            str_to_char(OUT, v, index);
            return OUT;
        }

        if (ANY_STRING(v))  // always true here
            fail ("UTF-8 Everywhere: String shuffle temporarily unavailable");

        Shuffle_String(v, REF(secure));
        RETURN (v); }

    default:
        // Let the port system try the action, e.g. OPEN %foo.txt
        //
        if ((Is_File(v) or Is_Url(v)))
            return T_Port(level_, verb);
    }

    fail (Error_Illegal_Action(VAL_TYPE(v), verb));
}


//
//  Startup_String: C
//
void Startup_String(void)
{
    Char_Escapes = ALLOC_N_ZEROFILL(Byte, MAX_ESC_CHAR + 1);

    Byte *cp = Char_Escapes;
    Byte c;
    for (c = '@'; c <= '_'; c++)
        *cp++ = c;

    Char_Escapes[cast(Byte, '\t')] = '-'; // tab
    Char_Escapes[cast(Byte, '\n')] = '/'; // line feed
    Char_Escapes[cast(Byte, '"')] = '"';
    Char_Escapes[cast(Byte, '^')] = '^';

    URL_Escapes = ALLOC_N_ZEROFILL(Byte, MAX_URL_CHAR + 1);

    for (c = 0; c <= ' '; c++)
        URL_Escapes[c] = ESC_URL | ESC_FILE;

    const Byte *dc = cb_cast(";%\"()[]{}<>");

    for (c = LEN_BYTES(dc); c > 0; c--)
        URL_Escapes[*dc++] = ESC_URL | ESC_FILE;
}


//
//  Shutdown_String: C
//
void Shutdown_String(void)
{
    FREE_N(Byte, MAX_ESC_CHAR + 1, Char_Escapes);
    FREE_N(Byte, MAX_URL_CHAR + 1, URL_Escapes);
}
