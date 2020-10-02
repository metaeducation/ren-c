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
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
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
REBINT CT_Binary(REBCEL(const*) a, REBCEL(const*) b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    REBLEN l1 = VAL_LEN_AT(a);
    REBLEN l2 = VAL_LEN_AT(b);
    REBLEN len = MIN(l1, l2);

    REBINT n = memcmp(VAL_BIN_AT(a), VAL_BIN_AT(b), len);

    if (n != 0)  // not guaranteed to be strictly in [-1 0 1]
        return n > 0 ? 1 : -1;

    if (l1 == l2)
        return 0;

    return l1 > l2 ? 1 : -1;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/


//
//  find_binary: C
//
REBLEN find_binary(
    REBLEN *size,  // match size (if TAG! pattern, not VAL_LEN_AT(pattern))
    const REBSER *bin,
    REBLEN index,
    REBLEN end,
    REBCEL(const*) pattern,
    REBLEN flags,
    REBINT skip
) {
    assert(end >= index);

    REBLEN start;
    if (skip < 0)
        start = 0;
    else
        start = index;

    if (ANY_STRING_KIND(CELL_KIND(pattern))) {
        if (skip != 1)
            fail ("String search in BINARY! only supports /SKIP 1 for now.");

        REBSTR *formed = nullptr;

        const REBYTE *bp2;
        REBLEN len2;
        if (CELL_KIND(pattern) != REB_TEXT) { // !!! <tag>...but FILE! etc?
            formed = Copy_Form_Cell(pattern, 0);
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

        REBLEN result = Find_Str_In_Bin(
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
    else if (CELL_KIND(pattern) == REB_BINARY) {
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
    else if (CELL_KIND(pattern) == REB_CHAR) {
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
    else if (CELL_KIND(pattern) == REB_INTEGER) {  // specific byte (exact)
        if (VAL_INT64(pattern) < 0 or VAL_INT64(pattern) > 255)
            fail (Error_Out_Of_Range(SPECIFIC(CELL_TO_VAL(pattern))));

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
    else if (CELL_KIND(pattern) == REB_BITSET) {
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

        REBSIZ size = VAL_SIZE_LIMIT_AT(NULL, arg, UNKNOWN);

        REBSER *bin = Make_Binary(size);
        memcpy(BIN_HEAD(bin), BIN_AT(VAL_SERIES(arg), offset), size);
        TERM_BIN_LEN(bin, size);
        return bin; }

    case REB_BLOCK:
        Join_Binary_In_Byte_Buf(arg, -1);
        return Copy_Series_Core(BYTE_BUF, SERIES_FLAGS_NONE);

    case REB_TUPLE:
        fail ("Revisit TUPLE! to BINARY!");

    case REB_CHAR: {
        REBUNI c = VAL_CHAR(arg);
        REBSIZ encoded_size = Encoded_Size_For_Codepoint(c);
        REBSER *bin = Make_Binary(encoded_size);
        Encode_UTF8_Char(BIN_HEAD(bin), c, encoded_size);
        TERM_SEQUENCE_LEN(bin, encoded_size);
        return bin;
    }

    case REB_BITSET:
        return Copy_Bytes(BIN_HEAD(VAL_BINARY(arg)), VAL_LEN_HEAD(arg));

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

        const RELVAL *first = VAL_ARRAY_AT(def);
        if (not IS_BINARY(first))
            goto bad_make;

        const RELVAL *index = VAL_ARRAY_AT(def) + 1;
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
//  PD_Binary: C
//
REB_R PD_Binary(
    REBPVS *pvs,
    const RELVAL *picker,
    const REBVAL *opt_setval
){
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
        const REBBIN *bin = VAL_BINARY(pvs->out);
        if (IS_INTEGER(picker)) {
            REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
            if (n < 0 or cast(REBLEN, n) >= BIN_LEN(bin))
                return nullptr;

            Init_Integer(pvs->out, *BIN_AT(bin, n));
            return pvs->out;
        }

        return R_UNHANDLED;
    }

    // Otherwise, POKE-ing

    REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(pvs->out);

    if (not IS_INTEGER(picker))
        return R_UNHANDLED;

    REBINT n = Int32(picker) + VAL_INDEX(pvs->out) - 1;
    if (n < 0 or cast(REBLEN, n) >= BIN_LEN(bin))
        fail (Error_Out_Of_Range(SPECIFIC(picker)));

    if (IS_CHAR(opt_setval)) {
        Init_Integer(pvs->out, VAL_CHAR(opt_setval));
    }
    else if (IS_INTEGER(opt_setval)) {
        Move_Value(pvs->out, opt_setval);
    }
    else {
        // !!! See notes in the REBTYPE(String) about alternate cases
        // for the POKE'd value.
        //
        return R_UNHANDLED;
    }

    REBINT i = Int32(pvs->out);
    if (i > 0xff)
        fail (Error_Out_Of_Range(opt_setval));

    BIN_HEAD(bin)[n] = cast(REBYTE, i);
    return R_INVISIBLE;
}


//
//  MF_Binary: C
//
void MF_Binary(REB_MOLD *mo, REBCEL(const*) v, bool form)
{
    UNUSED(form);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0)
        Pre_Mold(mo, v); // #[binary!

    REBLEN len = VAL_LEN_AT(v);

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

    // Common setup code for all actions:
    //
    REBINT index = cast(REBINT, VAL_INDEX(v));
    REBINT tail = cast(REBINT, VAL_LEN_HEAD(v));

    REBSYM sym = VAL_WORD_SYM(verb);
    switch (sym) {

        // Note: INTERSECT, UNION, DIFFERENCE handled later in the switch
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
        INCLUDE_PARAMS_OF_INSERT;  // compatible frame with APPEND, CHANGE
        UNUSED(PAR(series));  // covered by `v`

        if (REF(only)) {
            // !!! Doesn't pay attention...all binary appends are /ONLY
        }

        REBLEN len; // length of target
        if (VAL_WORD_SYM(verb) == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        REBFLGS flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        VAL_INDEX(v) = Modify_String_Or_Binary(
            v,
            cast(enum Reb_Symbol, sym),
            ARG(value),
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        RETURN (v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(PAR(series));  // covered by `v`

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

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

        REBLEN skip;
        if (REF(skip))
            skip = Part_Len_May_Modify_Index(v, ARG(part));
        else
            skip = 1;

        REBLEN len;
        REBLEN ret = find_binary(
            &len, VAL_SERIES(v), index, tail, pattern, flags, skip
        );

        if (ret >= cast(REBLEN, tail))
            return nullptr;

        if (sym == SYM_FIND) {
            if (REF(tail) or REF(match))
                ret += len;
            return Init_Any_Series_At(D_OUT, REB_BINARY, VAL_SERIES(v), ret);
        }

        ret++;
        if (ret >= cast(REBLEN, tail))
            return nullptr;

        return Init_Integer(D_OUT, *BIN_AT(VAL_SERIES(v), ret)); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(v);

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
                VAL_INDEX(v) = cast(REBLEN, tail - len);
        }

        if (cast(REBINT, VAL_INDEX(v)) >= tail) {
            if (not REF(part))
                return Init_Blank(D_OUT);

            return Init_Any_Series(D_OUT, VAL_TYPE(v), Make_Binary(0));
        }

        index = VAL_INDEX(v);

        // if no /PART, just return value, else return string
        //
        if (not REF(part)) {
            Init_Integer(D_OUT, *VAL_BIN_AT(v));
        }
        else {
            Init_Binary(
                D_OUT,
                Copy_Series_At_Len(bin, VAL_INDEX(v), len)
            );
        }
        Remove_Any_Series_Len(v, VAL_INDEX(v), len);  // bad UTF-8 alias fails
        return D_OUT; }

      case SYM_CLEAR: {
        REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(v);

        if (index >= tail)
            RETURN (v); // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the series).  One of many behaviors worth reviewing.
        //
        if (index == 0 and IS_SER_DYNAMIC(bin))
            Unbias_Series(bin, false);

        TERM_SEQUENCE_LEN(bin, cast(REBLEN, index));
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
            Copy_Series_At_Len(VAL_SERIES(v), VAL_INDEX(v), len)
        ); }

    //-- Bitwise:

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE: {
        REBVAL *arg = D_ARG(2);

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
        REBVAL *arg = D_ARG(2);
        REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(v);

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
            REBLEN wheel = VAL_LEN_HEAD(v) - 1;
            while (true) {
                REBYTE *b = BIN_AT(bin, wheel);
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
        REBVAL *arg = D_ARG(2);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        REBYTE *v_at = VAL_BIN_AT_ENSURE_MUTABLE(v);
        REBYTE *arg_at = VAL_BIN_AT_ENSURE_MUTABLE(arg);

        if (index < tail and VAL_INDEX(arg) < VAL_LEN_HEAD(arg)) {
            REBYTE temp = *v_at;
            *v_at = *arg_at;
            *arg_at = temp;
        }
        RETURN (v); }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        REBYTE *bp = VAL_BIN_AT_ENSURE_MUTABLE(v);

        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len > 0) {
            REBLEN n = 0;
            REBLEN m = len - 1;
            for (; n < len / 2; n++, m--) {
                REBYTE b = bp[n];
                bp[n] = bp[m];
                bp[m] = b;
            }
        }
        RETURN (v); }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;
        UNUSED(PAR(series));

        REBYTE *data_at = VAL_BIN_AT_ENSURE_MUTABLE(v);

        if (REF(all))
            fail (Error_Bad_Refine_Raw(ARG(all)));

        if (REF(case)) {
            // Ignored...all BINARY! sorts are case-sensitive.
        }

        if (REF(compare))
            fail (Error_Bad_Refine_Raw(ARG(compare)));  // !!! not in R3-Alpha

        REBFLGS thunk = 0;

        Move_Value(D_OUT, v);  // copy to output before index adjustment

        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        if (len <= 1)
            return D_OUT;

        REBLEN skip;
        if (not REF(skip))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or (len % skip != 0) or skip > len)
                fail (PAR(skip));
        }

        REBSIZ size = 1;
        if (skip > 1) {
            len /= skip;
            size *= skip;
        }

        if (did REF(reverse))
            thunk |= CC_FLAG_REVERSE;

        reb_qsort_r(
            data_at,
            len,
            size,
            &thunk,
            Compare_Byte
        );
        return D_OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PAR(value));

        if (REF(seed)) { // binary contents are the seed
            Set_Random(Compute_CRC24(VAL_BIN_AT(v), VAL_LEN_AT(v)));
            return Init_Void(D_OUT);
        }

        if (REF(only)) {
            if (index >= tail)
                return Init_Blank(D_OUT);

            index += cast(REBLEN, Random_Int(did REF(secure)))
                % (tail - index);
            const REBBIN *bin = VAL_BINARY(v);
            return Init_Integer(D_OUT, *BIN_AT(bin, index));  // PICK
        }

        REBBIN *bin = VAL_BINARY_ENSURE_MUTABLE(v);

        bool secure = did REF(secure);
        REBLEN n;
        for (n = BIN_LEN(bin) - index; n > 1;) {
            REBLEN k = index + cast(REBLEN, Random_Int(secure)) % n;
            n--;
            REBYTE swap = *BIN_AT(bin, k);
            *BIN_AT(bin, k) = *BIN_AT(bin, n + index);
            *BIN_AT(bin, n + index) = swap;
        }
        RETURN (v); }

      default:
        break;
    }

    return R_UNHANDLED;
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
    REBLEN index = VAL_INDEX(settings);
    bool no_sign = rebDid(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of ENBIN settings must be + or +/-}",
        "]",
        rebEND);
    const RELVAL *third = VAL_ARRAY_AT_HEAD(settings, index + 2);
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

    REBBIN* bin = Make_Binary(num_bytes);

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
    REBLEN index = VAL_INDEX(settings);
    bool no_sign = rebDid(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of DEBIN settings must be + or +/-}",
        "]",
        rebEND);
    REBLEN num_bytes;
    const RELVAL *third = VAL_ARRAY_AT_HEAD(settings, index + 2);
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
    const REBYTE* bp = VAL_BIN_AT(ARG(binary));
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
