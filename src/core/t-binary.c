//
//  File: %t-binary.c
//  Summary: "BINARY! datatype"
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
#include "sys-int-funcs.h"

#include "datatypes/sys-money.h"


//
//  CT_Binary: C
//
REBINT CT_Binary(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    assert(CELL_KIND(a) == REB_BINARY);
    assert(CELL_KIND(b) == REB_BINARY);

    REBINT num = Compare_Binary_Vals(a, b);

    if (mode >= 0) return (num == 0) ? 1 : 0;
    if (mode == -1) return (num >= 0) ? 1 : 0;
    return (num > 0) ? 1 : 0;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/


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


//
//  find_binary: C
//
REBCNT find_binary(
    REBCNT *size,  // match size (if TAG! pattern, not VAL_LEN_AT(pattern))
    REBSER *bin,
    REBCNT index,
    REBCNT end,
    const RELVAL *pattern,
    REBCNT flags,
    REBINT skip
) {
    assert(end >= index);

    REBCNT start;
    if (skip < 0)
        start = 0;
    else
        start = index;

    if (ANY_STRING(pattern)) {
        if (skip != 1)
            fail ("String search in BINARY! only supports /SKIP 1 for now.");

        REBSTR *formed = nullptr;

        REBYTE *bp2;
        REBCNT len2;
        if (not IS_TEXT(pattern)) { // !!! for TAG!, but what about FILE! etc?
            formed = Copy_Form_Value(pattern, 0);
            len2 = STR_LEN(formed);
            bp2 = STR_HEAD(formed);
            *size = STR_SIZE(formed);
        }
        else {
            len2 = VAL_LEN_AT(pattern);
            bp2 = VAL_STRING_AT(pattern);
            *size = VAL_SIZE_LIMIT_AT(NULL, pattern, len2);
        }

        if (*size > end - index)  // series not long enough for pattern
            return NOT_FOUND;

        REBCNT result = Find_Str_In_Bin(
            bin,
            start,
            bp2,
            len2,
            *size,
            flags & (AM_FIND_MATCH | AM_FIND_CASE)
        );

        if (formed)
            Free_Unmanaged_Series(SER(formed));

        return result;
    }
    else if (IS_BINARY(pattern)) {
        if (skip != 1)
            fail ("Search for BINARY! in BINARY! only supports /SKIP 1 ATM");

        *size = VAL_LEN_AT(pattern);
        return Find_Bin_In_Bin(
            bin,
            start,
            VAL_BIN_AT(pattern),
            *size,
            flags & AM_FIND_MATCH
        );
    }
    else if (IS_CHAR(pattern)) {
        //
        // Technically speaking the upper and lowercase sizes of a character
        // may not be the same.  It's okay here since we only do cased.
        //
        // https://stackoverflow.com/q/14792841/
        //
        *size = VAL_CHAR_ENCODED_SIZE(pattern);
        return Find_Char_In_Bin(
            VAL_CHAR(pattern),
            bin,
            start,
            index,
            end,
            skip,
            flags & (AM_FIND_CASE | AM_FIND_MATCH)
        );
    }
    else if (IS_INTEGER(pattern)) {  // specific byte value, never apply case
        if (VAL_INT64(pattern) < 0 or VAL_INT64(pattern) > 255)
            fail (Error_Out_Of_Range(KNOWN(pattern)));

        *size = 1;

        REBYTE byte = cast(REBYTE, VAL_INT64(pattern));

        return Find_Bin_In_Bin(
            bin,
            start,
            &byte,
            *size,
            flags & AM_FIND_MATCH
        );
    }
    else if (IS_BITSET(pattern)) {
        *size = 1;

        return Find_Bin_Bitset(
            bin,
            start,
            index,
            end,
            skip,
            VAL_BITSET(pattern),
            flags & AM_FIND_MATCH  // no AM_FIND_CASE
        );
    }
    else
        fail ("Unsupported pattern type passed to find_binary()");
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


// Common behaviors for:
//
//     MAKE BINARY! ...
//     TO BINARY! ...
//
// !!! MAKE and TO were not historically very clearly differentiated in
// Rebol, and so often they would "just do the same thing".  Ren-C ultimately
// will seek to limit the synonyms/polymorphism, e.g. MAKE or TO BINARY! of a
// BINARY! acting as COPY, in favor of having the user call COPY explicilty.
//
// Note also the existence of AS and storing strings as UTF-8 should reduce
// copying, e.g. `as binary! some-string` will be cheaper than TO or MAKE.
//
static REBSER *MAKE_TO_Binary_Common(const REBVAL *arg)
{
    switch (VAL_TYPE(arg)) {
    case REB_BINARY:
        return Copy_Bytes(VAL_BIN_AT(arg), VAL_LEN_AT(arg));

    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_TAG: {  // !!! What should REB_ISSUE do?
        REBSIZ offset = VAL_OFFSET_FOR_INDEX(arg, VAL_INDEX(arg));

        REBSIZ size = VAL_SIZE_LIMIT_AT(NULL, arg, -1);

        REBSER *bin = Make_Binary(size);
        memcpy(BIN_HEAD(bin), BIN_AT(VAL_SERIES(arg), offset), size);
        TERM_BIN_LEN(bin, size);
        return bin; }

    case REB_BLOCK:
        Join_Binary_In_Byte_Buf(arg, -1);
        return Copy_Sequence_Core(BYTE_BUF, SERIES_FLAGS_NONE);

    case REB_TUPLE:
        return Copy_Bytes(VAL_TUPLE(arg), VAL_TUPLE_LEN(arg));

    case REB_CHAR: {
        REBSER *bin = Make_Binary(6);
        TERM_SEQUENCE_LEN(bin, Encode_UTF8_Char(BIN_HEAD(bin), VAL_CHAR(arg)));
        return bin;
    }

    case REB_BITSET:
        return Copy_Bytes(VAL_BIN_HEAD(arg), VAL_LEN_HEAD(arg));

    case REB_MONEY: {
        REBSER *bin = Make_Binary(12);
        deci_to_binary(BIN_HEAD(bin), VAL_MONEY_AMOUNT(arg));
        TERM_SEQUENCE_LEN(bin, 12);
        return bin; }

    default:
        fail (Error_Bad_Make(REB_BINARY, arg));
    }
}


//
//  MAKE_Binary: C
//
// See also: MAKE_String, which is similar.
//
REB_R MAKE_Binary(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *def
){
    assert(kind == REB_BINARY);

    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    if (IS_INTEGER(def)) {
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make string! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        //
        return Init_Binary(out, Make_Binary(Int32s(def, 0)));
    }

    if (IS_BLOCK(def)) {
        //
        // The construction syntax for making binaries preloaded with an
        // offset into the data is #[binary [#{0001} 2]].
        //
        // !!! R3-Alpha make definitions didn't have to be a single value
        // (they are for compatibility between construction syntax and MAKE
        // in Ren-C).  So the positional syntax was #[binary! #{0001} 2]...
        // while #[binary [#{0001} 2]] would join the pieces together in order
        // to produce #{000102}.  That behavior is not available in Ren-C.

        if (VAL_ARRAY_LEN_AT(def) != 2)
            goto bad_make;

        RELVAL *first = VAL_ARRAY_AT(def);
        if (not IS_BINARY(first))
            goto bad_make;

        RELVAL *index = VAL_ARRAY_AT(def) + 1;
        if (not IS_INTEGER(index))
            goto bad_make;

        REBINT i = Int32(index) - 1 + VAL_INDEX(first);
        if (i < 0 or i > cast(REBINT, VAL_LEN_AT(first)))
            goto bad_make;

        return Init_Any_Series_At(out, REB_BINARY, VAL_SERIES(first), i);
    }

    return Init_Any_Series(out, REB_BINARY, MAKE_TO_Binary_Common(def));

bad_make:
    fail (Error_Bad_Make(REB_BINARY, def));
}


//
//  TO_Binary: C
//
REB_R TO_Binary(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    assert(kind == REB_BINARY);
    UNUSED(kind);

    if (IS_INTEGER(arg) or IS_DECIMAL(arg))
        return Init_Any_Series(out, REB_BINARY, Make_Binary_BE64(arg));

    return Init_Any_Series(out, REB_BINARY, MAKE_TO_Binary_Common(arg));
}


enum COMPARE_CHR_FLAGS {
    CC_FLAG_CASE = 1 << 0, // Case sensitive sort
    CC_FLAG_REVERSE = 1 << 1 // Reverse sort order
};


//
//  Compare_Byte: C
//
// This function is called by qsort_r, on behalf of the string sort
// function.  The `thunk` is an argument passed through from the caller
// and given to us by the sort routine, which tells us about the string
// and the kind of sort that was requested.
//
static int Compare_Byte(void *thunk, const void *v1, const void *v2)
{
    REBFLGS * const flags = cast(REBFLGS*, thunk);

    REBYTE b1 = *cast(const REBYTE*, v1);
    REBYTE b2 = *cast(const REBYTE*, v2);

    if (*flags & CC_FLAG_REVERSE)
        return b2 - b1;
    else
        return b1 - b2;
}


//
//  Sort_Binary: C
//
static void Sort_Binary(
    REBVAL *binary,
    REBVAL *skipv,
    REBVAL *compv,
    REBVAL *part,
    bool rev
){
    assert(IS_BINARY(binary));

    if (not IS_BLANK(compv))
        fail (Error_Bad_Refine_Raw(compv));  // !!! R3-Alpha didn't support

    REBFLGS thunk = 0;

    REBCNT len = Part_Len_May_Modify_Index(binary, part);  // length of sort
    if (len <= 1)
        return;

    REBCNT skip;
    if (IS_BLANK(skipv))
        skip = 1;
    else {
        skip = Get_Num_From_Arg(skipv);
        if (skip <= 0 or (len % skip != 0) or skip > len)
            fail (skipv);
    }

    REBCNT size = 1;
    if (skip > 1) {
        len /= skip;
        size *= skip;
    }

    if (rev)
        thunk |= CC_FLAG_REVERSE;

    reb_qsort_r(
        VAL_RAW_DATA_AT(binary),
        len,
        size,
        &thunk,
        Compare_Byte
    );
}


//
//  PD_Binary: C
//
REB_R PD_Binary(
    REBPVS *pvs,
    const REBVAL *picker,
    const REBVAL *opt_setval
){
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

    if (not opt_setval) { // PICK-ing
        if (IS_INTEGER(picker)) {
            REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
            if (n < 0 or cast(REBCNT, n) >= SER_LEN(ser))
                return nullptr;

            Init_Integer(pvs->out, *BIN_AT(ser, n));
            return pvs->out;
        }

        return R_UNHANDLED;
    }

    // Otherwise, POKE-ing

    FAIL_IF_READ_ONLY(pvs->out);

    if (not IS_INTEGER(picker))
        return R_UNHANDLED;

    REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
    if (n < 0 or cast(REBCNT, n) >= SER_LEN(ser))
        fail (Error_Out_Of_Range(picker));

    REBINT c;
    if (IS_CHAR(opt_setval)) {
        c = VAL_CHAR(opt_setval);
        if (c > cast(REBI64, MAX_UNI))
            return R_UNHANDLED;
    }
    else if (IS_INTEGER(opt_setval)) {
        c = Int32(opt_setval);
        if (c > cast(REBI64, MAX_UNI) or c < 0)
            return R_UNHANDLED;
    }
    else if (ANY_BINSTR(opt_setval)) {
        REBCNT i = VAL_INDEX(opt_setval);
        if (i >= VAL_LEN_HEAD(opt_setval))
            fail (opt_setval);

        c = GET_CHAR_AT(VAL_STRING(opt_setval), i);
    }
    else
        return R_UNHANDLED;

    if (c > 0xff)
        fail (Error_Out_Of_Range(opt_setval));

    BIN_HEAD(ser)[n] = cast(REBYTE, c);
    return R_INVISIBLE;
}


//
//  MF_Binary: C
//
void MF_Binary(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0)
        Pre_Mold(mo, v); // #[binary!

    REBCNT len = VAL_LEN_AT(v);

    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
      default:
      case 16: {
        Append_Ascii(mo->series, "#{"); // default, so #{...} not #16{...}

        const bool brk = (len > 32);
        Form_Base16(mo, VAL_BIN_AT(v), len, brk);
        break; }

      case 64: {
        Append_Ascii(mo->series, "64#{");

        const bool brk = (len > 64);
        Form_Base64(mo, VAL_BIN_AT(v), len, brk);
        break; }

      case 2: {
        Append_Ascii(mo->series, "2#{");

        const bool brk = (len > 8);
        Form_Base2(mo, VAL_BIN_AT(v), len, brk);
        break; }
    }

    Append_Codepoint(mo->series, '}');

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0)
        Post_Mold(mo, v);
}


//
//  REBTYPE: C
//
REBTYPE(Binary)
{
    REBVAL *v = D_ARG(1);
    assert(IS_BINARY(v));

    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    // Common setup code for all actions:
    //
    REBINT index = cast(REBINT, VAL_INDEX(v));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(v));

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {

        // INTERSECT, UNION, DIFFERENCE handled later in the switch
        //
    case SYM_REFLECT:
    case SYM_SKIP:
    case SYM_AT:
    case SYM_REMOVE:
        return Series_Common_Action_Maybe_Unhandled(frame_, verb);

    //-- Modification:
    case SYM_APPEND:
    case SYM_INSERT:
    case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;

        FAIL_IF_READ_ONLY(v);

        UNUSED(PAR(series));
        UNUSED(PAR(value));

        if (REF(only)) {
            // !!! Doesn't pay attention...all binary appends are /ONLY
        }

        REBCNT len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Len_Append_Insert_May_Modify_Index(arg, ARG(part));

        REBFLGS flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        VAL_INDEX(v) = Modify_String_Or_Binary(
            v,
            VAL_WORD_CANON(verb),
            arg,
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        RETURN (v); }

    //-- Search:
    case SYM_SELECT:
    case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        UNUSED(PAR(series));
        REBVAL *pattern = ARG(pattern);

        // !!! R3-Alpha FIND/MATCH historically implied /TAIL.  Should it?
        //
        REBFLGS flags = (
            (REF(only) ? AM_FIND_ONLY : 0)
            | (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        flags |= AM_FIND_CASE;

        if (REF(part))
            tail = Part_Tail_May_Modify_Index(v, ARG(part));

        REBCNT skip;
        if (REF(skip))
            skip = Part_Len_May_Modify_Index(v, ARG(part));
        else
            skip = 1;

        REBCNT len;
        REBCNT ret = find_binary(
            &len, VAL_SERIES(v), index, tail, pattern, flags, skip
        );

        if (ret >= cast(REBCNT, tail))
            return nullptr;

        if (sym == SYM_FIND) {
            if (REF(tail) or REF(match))
                ret += len;
            return Init_Any_Series_At(D_OUT, REB_BINARY, VAL_SERIES(v), ret);
        }

        ret++;
        if (ret >= cast(REBCNT, tail))
            return nullptr;

        return Init_Integer(D_OUT, *BIN_AT(VAL_SERIES(v), ret)); }

    case SYM_TAKE_P: {
        INCLUDE_PARAMS_OF_TAKE_P;

        FAIL_IF_READ_ONLY(v);

        UNUSED(PAR(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(part));
            if (len == 0)
                return Init_Any_Series(D_OUT, VAL_TYPE(v), Make_Binary(0));
        } else
            len = 1;

        // Note that /PART can change index

        if (REF(last)) {
            if (tail - len < 0) {
                VAL_INDEX(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX(v) = cast(REBCNT, tail - len);
        }

        if (cast(REBINT, VAL_INDEX(v)) >= tail) {
            if (not REF(part))
                return Init_Blank(D_OUT);

            return Init_Any_Series(D_OUT, VAL_TYPE(v), Make_Binary(0));
        }

        REBSER *ser = VAL_SERIES(v);
        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (not REF(part)) {
            Init_Integer(D_OUT, *VAL_BIN_AT(v));
        }
        else {
            Init_Binary(
                D_OUT,
                Copy_Sequence_At_Len(VAL_SERIES(v), VAL_INDEX(v), len)
            );
        }
        Remove_Series_Units(ser, VAL_INDEX(v), len);
        return D_OUT; }

    case SYM_CLEAR: {
        REBSER *ser = VAL_SERIES(v);
        FAIL_IF_READ_ONLY(v);

        if (index >= tail)
            RETURN (v); // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the series).  One of many behaviors worth reviewing.
        //
        if (index == 0 and IS_SER_DYNAMIC(ser))
            Unbias_Series(ser, false);

        TERM_SEQUENCE_LEN(ser, cast(REBCNT, index));
        RETURN (v); }

    //-- Creation:

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));

        if (REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());
 
        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));

        return Init_Any_Series(
            D_OUT,
            REB_BINARY,
            Copy_Sequence_At_Len(VAL_SERIES(v), VAL_INDEX(v), len)
        ); }

    //-- Bitwise:

    case SYM_INTERSECT:
    case SYM_UNION:
    case SYM_DIFFERENCE: {
        if (VAL_INDEX(v) > VAL_LEN_HEAD(v))
            VAL_INDEX(v) = VAL_LEN_HEAD(v);

        if (VAL_INDEX(arg) > VAL_LEN_HEAD(arg))
            VAL_INDEX(arg) = VAL_LEN_HEAD(arg);

        return Init_Any_Series(
            D_OUT,
            REB_BINARY,
            Xandor_Binary(verb, v, arg)
        ); }

    case SYM_COMPLEMENT: {
        return Init_Any_Series(
            D_OUT,
            REB_BINARY,
            Complement_Binary(v)
        ); }

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
        FAIL_IF_READ_ONLY(v);

        REBINT amount;
        if (IS_INTEGER(arg))
            amount = VAL_INT32(arg);
        else if (IS_BINARY(arg))
            fail (arg); // should work
        else
            fail (arg); // what about other types?

        if (sym == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) // adding or subtracting 0 works, even #{} + 0
            RETURN (v);

        if (VAL_LEN_AT(v) == 0) // add/subtract to #{} otherwise
            fail (Error_Overflow_Raw());

        while (amount != 0) {
            REBCNT wheel = VAL_LEN_HEAD(v) - 1;
            while (true) {
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
        RETURN (v); }

    //-- Special actions:

    case SYM_SWAP: {
        FAIL_IF_READ_ONLY(v);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        FAIL_IF_READ_ONLY(arg);

        if (index < tail and VAL_INDEX(arg) < VAL_LEN_HEAD(arg)) {
            REBYTE temp = *VAL_BIN_AT(v);
            *VAL_BIN_AT(v) = *VAL_BIN_AT(arg);
            *VAL_BIN_AT(arg) = temp;
        }
        RETURN (v); }

    case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        FAIL_IF_READ_ONLY(v);

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len > 0)
            reverse_binary(v, len);
        RETURN (v); }

    case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;

        FAIL_IF_READ_ONLY(v);

        UNUSED(PAR(series));

        if (REF(all))
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (REF(case)) {
            // Ignored...all BINARY! sorts are case-sensitive.
        }

        Sort_Binary(
            v,
            ARG(skip),  // blank! if not /SKIP
            ARG(compare),  // (blank! if not /COMPARE)
            ARG(part),   // (blank! if not /PART)
            REF(reverse)
        );
        RETURN (v); }

    case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(seed)) { // binary contents are the seed
            Set_Random(Compute_CRC24(VAL_BIN_AT(v), VAL_LEN_AT(v)));
            return Init_Void(D_OUT);
        }

        FAIL_IF_READ_ONLY(v);

        if (REF(only)) {
            if (index >= tail)
                return Init_Blank(D_OUT);

            index += cast(REBCNT, Random_Int(REF(secure))) % (tail - index);
            return Init_Integer(D_OUT, *VAL_BIN_AT_HEAD(v, index)); // PICK
        }

        Shuffle_String(v, REF(secure));
        RETURN (v); }

    default:
        break;
    }

    return R_UNHANDLED;
}
