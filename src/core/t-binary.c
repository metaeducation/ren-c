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

#undef Byte  // sys-zlib.h defines it compatibly (unsigned char)
#include "sys-zlib.h"  // for crc32_z()

#include "cells/cell-money.h"


//
//  CT_Binary: C
//
REBINT CT_Binary(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // no lax form of comparison

    Size size1;
    const Byte* data1 = Cell_Binary_Size_At(&size1, a);

    Size size2;
    const Byte* data2 = Cell_Binary_Size_At(&size2, b);

    Size size = MIN(size1, size2);

    REBINT n = memcmp(data1, data2, size);

    if (n != 0)  // not guaranteed to be strictly in [-1 0 1]
        return n > 0 ? 1 : -1;

    if (size1 == size2)
        return 0;

    return size1 > size2 ? 1 : -1;
}


/***********************************************************************
**
**  Local Utility Functions
**
***********************************************************************/



//
//  /encode-IEEE-754: native [
//      "Encode a decimal as binary blob according to the IEEE-754 standard"
//
//      return: "Default return is double format (64 bits, 53-bit precision)"
//          [binary!]
//      arg [decimal!]  ; REVIEW: ~NaN~, ~inf~ as antiforms
//      options "[single] -> Use single format (32 bits, 24-bit precision)"
//          [block!]
//  ]
//
DECLARE_NATIVE(encode_ieee_754) {
    INCLUDE_PARAMS_OF_ENCODE_IEEE_754;

    Value* arg = ARG(arg);

    if (Cell_Series_Len_At(ARG(options)))
        return FAIL("IEEE-754 single precision not currently supported");

    assert(sizeof(REBDEC) == 8);

    Binary* bin = Make_Binary(8);
    Byte* bp = Binary_Head(bin);

    REBDEC d = VAL_DECIMAL(arg);
    const Byte* cp = c_cast(Byte*, &d);

  #if defined(ENDIAN_LITTLE)
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

    Term_Binary_Len(bin, 8);
    return Init_Blob(OUT, bin);
}


//
//  /decode-IEEE-754: native [
//      "Decode binary blob as decimal according to the IEEE-754 standard"
//
//      return: [decimal!]  ; review ~NaN~, ~inf~ as antiforms
//      blob [binary!]
//      options "[single] -> Use single format (32 bits, 24-bit precision)"
//          [block!]
//  ]
//
DECLARE_NATIVE(decode_ieee_754) {
    INCLUDE_PARAMS_OF_DECODE_IEEE_754;

    Element* blob = cast(Element*, ARG(blob));

    if (Cell_Series_Len_At(ARG(options)))
        return FAIL("IEEE-754 single precision not currently supported");

    Size size;
    const Byte* at = Cell_Binary_Size_At(&size, blob);
    if (size < 8)
        return RAISE(blob);

    Reset_Cell_Header_Untracked(TRACK(OUT), CELL_MASK_DECIMAL);

    Byte* dp = cast(Byte*, &VAL_DECIMAL(OUT));

  #if defined(ENDIAN_LITTLE)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = at[7 - n];
  #elif defined(ENDIAN_BIG)
    REBLEN n;
    for (n = 0; n < 8; ++n)
        dp[n] = at[n];
  #else
    #error "Unsupported CPU endian"
  #endif

    return OUT;
}


//
//  Makehook_Binary: C
//
// See also: Makehook_String(), which is similar.
//
Bounce Makehook_Binary(Level* level_, Kind kind, Element* arg) {
    assert(kind == REB_BINARY);
    UNUSED(kind);

    switch (VAL_TYPE(arg)) {
      case REB_INTEGER:  // !!! R3-Alpha nebulously tolerated DECIMAL! :-(
        return Init_Blob(OUT, Make_Binary(Int32s(arg, 0)));

      case REB_BLOCK: {
        Join_Binary_In_Byte_Buf(arg, -1);
        Binary* b = cast(
            Binary*,
            Copy_Flex_Core(FLEX_FLAGS_NONE, BYTE_BUF)
        );
        return Init_Blob(OUT, b); }

      case REB_TUPLE: {
        REBLEN len = Cell_Sequence_Len(arg);
        Binary* b = Make_Binary(len);
        Byte* head = Binary_Head(b);
        if (Try_Get_Sequence_Bytes(head, c_cast(Element*, arg), len)) {
            Term_Binary_Len(b, len);
            return Init_Blob(OUT, b);
        }
        return FAIL(
            "TUPLE! did not consist entirely of INTEGER! values 0-255"
        ); }

      case REB_BITSET:
        return Init_Blob(
            OUT,
            Make_Binary_From_Sized_Bytes(
                Binary_Head(Cell_Binary(arg)),
                Cell_Series_Len_Head(arg)
            )
        );

      case REB_MONEY: {
        Binary* b = Make_Binary(12);
        deci_to_binary(Binary_Head(b), VAL_MONEY_AMOUNT(arg));
        Term_Binary_Len(b, 12);
        return Init_Blob(OUT, b); }

      default:
        break;
    }

    return RAISE(Error_Bad_Make(REB_BINARY, arg));
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
    Flags * const flags = cast(Flags*, thunk);

    Byte b1 = *c_cast(Byte*, v1);
    Byte b2 = *c_cast(Byte*, v2);

    if (*flags & CC_FLAG_REVERSE)
        return b2 - b1;
    else
        return b1 - b2;
}


//
//  MF_Binary: C
//
// 1. Historial Rebol let you set your binary base molding in a global way.
//    If this is to be a console setting, that's one thing...but having a
//    flag like this changing the fundamental behavior is bad.  In addition
//    to the general variability of how a program would run, it was using
//    a setting in the system object...which is not avaliable in early boot.
//
void MF_Binary(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form);

    Size size;
    const Byte* data = Cell_Binary_Size_At(&size, v);

    REBINT binary_base = 16;  // molding based on system preference is bad [1]
    /* binary_base = Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16); */

    switch (binary_base) {
      default:
      case 16: {
        Append_Ascii(mo->string, "#{"); // default, so #{...} not #16{...}

        const bool brk = (size > 32);
        Form_Base16(mo, data, size, brk);
        break; }

      case 64: {
        Append_Ascii(mo->string, "64#{");

        const bool brk = (size > 64);
        Form_Base64(mo, data, size, brk);
        break; }

      case 2: {
        Append_Ascii(mo->string, "2#{");

        const bool brk = (size > 8);
        Form_Base2(mo, data, size, brk);
        break; }
    }

    Append_Codepoint(mo->string, '}');
}


//
//  REBTYPE: C
//
REBTYPE(Binary)
{
    Value* v = D_ARG(1);
    assert(Is_Binary(v));

    Option(SymId) id = Symbol_Id(verb);

    switch (id) {

    //=//// TO CONVERSIONS ////////////////////////////////////////////////=//

    // 1. !!! Historically TO would convert binaries to strings.  But as the
    //    definition of TO has been questioned and evolving, that no longer
    //    seems to make sense (e.g. if `TO TEXT! 1` is "1", the concept of
    //    implementation transformations doesn't fit).  Keep compatible for
    //    right now, but ultimately MAKE or AS should be used for this.

      case SYM_TO_P: {
        INCLUDE_PARAMS_OF_TO_P;
        UNUSED(ARG(element));  // v
        Heart to = VAL_TYPE_HEART(ARG(type));
        assert(REB_BINARY != to);  // TO should have called COPY in this case

        if (Any_String_Kind(to)) {  // (to text! binary) questionable [1]
            Size size;
            const Byte* at = Cell_Binary_Size_At(&size, v);
            return Init_Any_String(
                OUT,
                to,
                Append_UTF8_May_Fail(nullptr, cs_cast(at), size, STRMODE_NO_CR)
            );
        }

        return FAIL(Error_Bad_Cast_Raw(v, ARG(type))); }

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n;
        if (not Try_Get_Series_Index_From_Picker(&n, v, picker))
            return RAISE(Error_Bad_Pick_Raw(picker));

        Byte b = *Binary_At(Cell_Binary(v), n);

        return Init_Integer(OUT, b);
      }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n;
        if (not Try_Get_Series_Index_From_Picker(&n, v, picker))
            return FAIL(Error_Out_Of_Range(picker));

        Value* setval = ARG(value);

        REBINT i;
        if (IS_CHAR(setval)) {
            i = Cell_Codepoint(setval);
        }
        else if (Is_Integer(setval)) {
            i = Int32(setval);
        }
        else {
            // !!! See notes in the REBTYPE(String) about alternate cases
            // for the POKE'd value.
            //
            return FAIL(PARAM(value));
        }

        if (i > 0xff)
            return FAIL(Error_Out_Of_Range(setval));

        Binary* b = Cell_Binary_Ensure_Mutable(v);
        Binary_Head(b)[n] = cast(Byte, i);

        return nullptr; }  // caller's Binary* is not stale, no update needed


      case SYM_UNIQUE:
      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE:
        //
      case SYM_REFLECT:
      case SYM_SKIP:
      case SYM_AT:
      case SYM_REMOVE:
        return Series_Common_Action_Maybe_Unhandled(level_, verb);

    //-- Modification:
      case SYM_APPEND:
      case SYM_INSERT:
      case SYM_CHANGE: {
        INCLUDE_PARAMS_OF_INSERT;  // compatible frame with APPEND, CHANGE
        UNUSED(PARAM(series));  // covered by `v`

        Value* arg = ARG(value);
        assert(not Is_Nulled(arg));  // not an ~null~ parameter

        REBLEN len; // length of target
        if (id == SYM_CHANGE)
            len = Part_Len_May_Modify_Index(v, ARG(part));
        else
            len = Part_Limit_Append_Insert(ARG(part));

        // Note that while inserting or appending VOID is a no-op, CHANGE with
        // a :PART can actually erase data.
        //
        if (Is_Void(arg) and len == 0) {
            if (id == SYM_APPEND) // append always returns head
                VAL_INDEX_RAW(v) = 0;
            return COPY(v);  // don't fail on read only if would be a no-op
        }

        Flags flags = 0;
        if (REF(part))
            flags |= AM_PART;
        if (REF(line))
            flags |= AM_LINE;

        // !!! This mimics the historical behavior for now:
        //
        //     rebol2>> append "abc" 'd
        //     == "abcd"
        //
        //     rebol2>> append/only "abc" [d e]  ; like appending (the '[d e])
        //     == "abcde"
        //
        // But for consistency, it would seem that if the incoming value is
        // quoted that should give molding semantics, so quoted blocks include
        // their brackets.  Review.
        //
        if (Is_Void(arg)) {
            // not necessarily a no-op (e.g. CHANGE can erase)
        }
        else if (Is_Splice(arg)) {
            QUOTE_BYTE(arg) = NOQUOTE_1;  // make plain group
        }
        else if (Any_List(arg) or Any_Sequence(arg))
            return FAIL(ARG(value));
        else
            assert(not Is_Antiform(arg));

        VAL_INDEX_RAW(v) = Modify_String_Or_Binary(
            v,
            unwrap id,
            ARG(value),
            flags,
            len,
            REF(dup) ? Int32(ARG(dup)) : 1
        );
        return COPY(v); }

    //-- Search:
      case SYM_SELECT:
      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;
        UNUSED(PARAM(series));  // covered by `v`

        Value* pattern = ARG(pattern);
        if (Is_Antiform(pattern))
            return FAIL(pattern);

        Flags flags = (
            (REF(match) ? AM_FIND_MATCH : 0)
            | (REF(case) ? AM_FIND_CASE : 0)
        );

        REBINT tail = Part_Tail_May_Modify_Index(v, ARG(part));

        REBINT skip;
        if (REF(skip))
            skip = VAL_INT32(ARG(skip));
        else
            skip = 1;

        REBLEN size;
        REBLEN ret = Find_Value_In_Binstr(  // returned length is byte index
            &size, v, tail, pattern, flags, skip
        );

        if (ret == NOT_FOUND)
            return nullptr;

        if (id == SYM_FIND) {
            Source* pack = Make_Source_Managed(2);
            Set_Flex_Len(pack, 2);

            Copy_Meta_Cell(Array_At(pack, 0), v);
            VAL_INDEX_RAW(Array_At(pack, 0)) = ret;

            Copy_Meta_Cell(Array_At(pack, 1), v);
            VAL_INDEX_RAW(Array_At(pack, 1)) = ret + size;

            return Init_Pack(OUT, pack);
        }
        else
            assert(id == SYM_SELECT);

        ret++;
        if (ret >= tail)
            return nullptr;

        return Init_Integer(OUT, *Binary_At(Cell_Binary(v), ret)); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        Binary* b = Cell_Binary_Ensure_Mutable(v);

        UNUSED(PARAM(series));

        if (REF(deep))
            return FAIL(Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(part));
            if (len == 0) {
                Heart heart = Cell_Heart_Ensure_Noquote(v);
                return Init_Series(OUT, heart, Make_Binary(0));
            }
        } else
            len = 1;

        // Note that :PART can change index

        REBINT tail = Cell_Series_Len_Head(v);

        if (REF(last)) {
            if (tail - len < 0) {
                VAL_INDEX_RAW(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX_RAW(v) = tail - len;
        }

        if (VAL_INDEX(v) >= tail) {
            if (not REF(part))
                return RAISE(Error_Nothing_To_Take_Raw());

            Heart heart = Cell_Heart_Ensure_Noquote(v);
            return Init_Series(OUT, heart, Make_Binary(0));
        }

        // if no :PART, just return value, else return string
        //
        if (not REF(part)) {
            Init_Integer(OUT, *Cell_Blob_At(v));
        }
        else {
            Init_Blob(
                OUT,
                Copy_Binary_At_Len(b, VAL_INDEX(v), len)
            );
        }
        Remove_Any_Series_Len(v, VAL_INDEX(v), len);  // bad UTF-8 alias fails
        return OUT; }

      case SYM_CLEAR: {
        Binary* b = Cell_Binary_Ensure_Mutable(v);

        REBINT tail = Cell_Series_Len_Head(v);
        REBINT index = VAL_INDEX(v);

        if (index >= tail)
            return COPY(v); // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the Flex).  One of many behaviors worth reviewing.
        //
        if (index == 0 and Get_Stub_Flag(b, DYNAMIC))
            Unbias_Flex(b, false);

        Term_Binary_Len(b, index);  // may have string alias
        return COPY(v); }

    //-- Creation:

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(value));
        UNUSED(REF(deep));  // :DEEP is historically ignored on BINARY!

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));

        return Init_Series(
            OUT,
            REB_BINARY,
            Copy_Binary_At_Len(Cell_Binary(v), VAL_INDEX(v), len)
        ); }

    //-- Bitwise:

      case SYM_BITWISE_AND:
      case SYM_BITWISE_OR:
      case SYM_BITWISE_XOR:
      case SYM_BITWISE_AND_NOT: {
        Value* arg = D_ARG(2);
        if (not Is_Binary(arg))
            return FAIL(Error_Math_Args(VAL_TYPE(arg), verb));

        Size t0;
        const Byte* p0 = Cell_Binary_Size_At(&t0, v);

        Size t1;
        const Byte* p1 = Cell_Binary_Size_At(&t1, arg);

        Size smaller = MIN(t0, t1);  // smaller array size
        Size larger = MAX(t0, t1);

        Binary* b = Make_Binary(larger);
        Term_Binary_Len(b, larger);

        Byte* dest = Binary_Head(b);

        switch (id) {
          case SYM_BITWISE_AND: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ & *p1++;
            memset(dest, 0, larger - smaller);
            break; }

          case SYM_BITWISE_OR: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ | *p1++;
            memcpy(dest, ((t0 > t1) ? p0 : p1), larger - smaller);
            break; }

          case SYM_BITWISE_XOR: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ ^ *p1++;
            memcpy(dest, ((t0 > t1) ? p0 : p1), larger - smaller);
            break; }

          case SYM_BITWISE_AND_NOT: {
            REBLEN i;
            for (i = 0; i < smaller; i++)
                *dest++ = *p0++ & ~*p1++;
            if (t0 > t1)
                memcpy(dest, p0, t0 - t1);
            break; }

          default:
            assert(false);  // not reachable
        }

        return Init_Blob(OUT, b); }

      case SYM_BITWISE_NOT: {
        Size size;
        const Byte* bp = Cell_Binary_Size_At(&size, v);

        Binary* bin = Make_Binary(size);
        Term_Binary_Len(bin, size);  // !!! size is decremented, must set now

        Byte* dp = Binary_Head(bin);
        for (; size > 0; --size, ++bp, ++dp)
            *dp = ~(*bp);

        return Init_Series(OUT, REB_BINARY, bin); }

    // Arithmetic operations are allowed on BINARY!, because it's too limiting
    // to not allow `#{4B} + 1` => `#{4C}`.  Allowing the operations requires
    // a default semantic of binaries as unsigned arithmetic, since one
    // does not want `#{FF} + 1` to be #{FE}.  It uses a big endian
    // interpretation, so `#{00FF} + 1` is #{0100}
    //
    // Since Rebol is a language with mutable semantics by default, `add x y`
    // will mutate x by default (if X is not an immediate type).  `+` is an
    // infixing of `add-of` which copies the first argument before adding.
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
        Value* arg = D_ARG(2);
        Binary* bin = Cell_Binary_Ensure_Mutable(v);

        REBINT amount;
        if (Is_Integer(arg))
            amount = VAL_INT32(arg);
        else if (Is_Binary(arg))
            return FAIL(arg); // should work
        else
            return FAIL(arg); // what about other types?

        if (id == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) // adding or subtracting 0 works, even #{} + 0
            return COPY(v);

        if (Cell_Series_Len_At(v) == 0) // add/subtract to #{} otherwise
            return FAIL(Error_Overflow_Raw());

        while (amount != 0) {
            REBLEN wheel = Cell_Series_Len_Head(v) - 1;
            while (true) {
                Byte* b = Binary_At(bin, wheel);
                if (amount > 0) {
                    if (*b == 255) {
                        if (wheel == VAL_INDEX(v))
                            return FAIL(Error_Overflow_Raw());

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
                            return FAIL(Error_Overflow_Raw());

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
        return COPY(v); }

    //-- Special actions:

      case SYM_SWAP: {
        Value* arg = D_ARG(2);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            return FAIL(Error_Not_Same_Type_Raw());

        Byte* v_at = Cell_Blob_At_Ensure_Mutable(v);
        Byte* arg_at = Cell_Blob_At_Ensure_Mutable(arg);

        REBINT tail = Cell_Series_Len_Head(v);
        REBINT index = VAL_INDEX(v);

        if (index < tail and VAL_INDEX(arg) < Cell_Series_Len_Head(arg)) {
            Byte temp = *v_at;
            *v_at = *arg_at;
            *arg_at = temp;
        }
        return COPY(v); }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(ARG(series));

        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        Byte* bp = Cell_Blob_At_Ensure_Mutable(v);  // index may've changed

        if (len > 0) {
            REBLEN n = 0;
            REBLEN m = len - 1;
            for (; n < len / 2; n++, m--) {
                Byte b = bp[n];
                bp[n] = bp[m];
                bp[m] = b;
            }
        }
        return COPY(v); }

      case SYM_SORT: {
        INCLUDE_PARAMS_OF_SORT;
        UNUSED(PARAM(series));

        if (REF(all))
            return FAIL(Error_Bad_Refines_Raw());

        if (REF(case)) {
            // Ignored...all BINARY! sorts are case-sensitive.
        }

        if (REF(compare))
            return FAIL(Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

        Flags thunk = 0;

        Copy_Cell(OUT, v);  // copy to output before index adjustment

        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        Byte* data_at = Cell_Blob_At_Ensure_Mutable(v);  // ^ index changes

        if (len <= 1)
            return OUT;

        REBLEN skip;
        if (not REF(skip))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or (len % skip != 0) or skip > len)
                return FAIL(PARAM(skip));
        }

        Size size = 1;
        if (skip > 1) {
            len /= skip;
            size *= skip;
        }

        if (REF(reverse))
            thunk |= CC_FLAG_REVERSE;

        reb_qsort_r(
            data_at,
            len,
            size,
            &thunk,
            Compare_Byte
        );
        return OUT; }

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));

        if (REF(seed)) { // binary contents are the seed
            Size size;
            const Byte* data = Cell_Binary_Size_At(&size, v);
            Set_Random(crc32_z(0L, data, size));
            return NOTHING;
        }

        REBINT tail = Cell_Series_Len_Head(v);
        REBINT index = VAL_INDEX(v);

        if (REF(only)) {
            if (index >= tail)
                return Init_Blank(OUT);

            index += Random_Int(REF(secure)) % (tail - index);
            const Binary* b = Cell_Binary(v);
            return Init_Integer(OUT, *Binary_At(b, index));  // PICK
        }

        Binary* b = Cell_Binary_Ensure_Mutable(v);

        bool secure = REF(secure);
        REBLEN n;
        for (n = Binary_Len(b) - index; n > 1;) {
            REBLEN k = index + Random_Int(secure) % n;
            n--;
            Byte swap = *Binary_At(b, k);
            *Binary_At(b, k) = *Binary_At(b, n + index);
            *Binary_At(b, n + index) = swap;
        }
        return COPY(v); }

      default:
        break;
    }

    return UNHANDLED;
}


//
//  /encode-integer: native [
//
//  "Encode integer as a Little Endian or Big Endian BINARY!, signed/unsigned"
//
//      return: [binary!]
//      num [integer!]
//      options "[<+ or +/-> <number of bytes>]"
//          [block!]
//      :LE "Encode as little-endian (default is big-endian)"
//  ]
//
DECLARE_NATIVE(encode_integer)
{
    INCLUDE_PARAMS_OF_ENCODE_INTEGER;

    bool little = REF(le);

    Value* options = ARG(options);
    if (Cell_Series_Len_At(options) != 2)
        return FAIL("ENCODE-INTEER needs length 2 options for now");

    bool no_sign = rebUnboxBoolean(
        "switch first", options, "[",
            "'+ ['true] '+/- ['false]",
            "fail -{First ENCODE-INTEGER option must be + or +/-}-",
        "]"
    );
    REBINT num_bytes = rebUnboxInteger(
        "(match integer! second", options, ") else [",
            "fail -{Second ENCODE-INTEGER option must be an integer}-",
        "]"
    );
    if (num_bytes <= 0)
        return FAIL("Size for ENCODE-INTEGER encoding must be at least 1");

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    Binary* bin = Make_Binary(num_bytes);

    REBINT delta = little ? 1 : -1;
    Byte* bp = Binary_Head(bin);
    if (not little)
        bp += num_bytes - 1;  // go backwards for big endian

    REBI64 i = VAL_INT64(ARG(num));
    if (no_sign and i < 0)
        return FAIL("Unsigned ENCODE-INTEGER received signed input value");

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
        return rebDelegate(
            "fail [", ARG(num), "-{exceeds}-", rebI(num_bytes), "-{bytes}-]"
        );

    // The process of byte production of a positive number shouldn't give us
    // something with the high bit set in a signed representation.
    //
    if (not no_sign and not negative and *(bp - delta) >= 0x80)
        return rebDelegate(
            "fail [",
                ARG(num), "-{aliases a negative value with signed}-",
                "-{encoding of only}-", rebI(num_bytes), "-{bytes}-",
            "]"
        );

    Term_Binary_Len(bin, num_bytes);
    return Init_Blob(OUT, bin);
}


//
//  /decode-integer: native [
//
//  "Decode BINARY! as Little Endian or Big Endian, signed/unsigned integer"
//
//      return: [integer!]
//      binary "Decoded (defaults length of binary for number of bytes)"
//          [binary!]
//      options "[<+ or +/-> <number of bytes>]"
//          [block!]
//      :LE "Decode as little-endian (default is big-endian)"
//  ]
//
DECLARE_NATIVE(decode_integer)
//
// !!! This routine may wind up being folded into DECODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
{
    INCLUDE_PARAMS_OF_DECODE_INTEGER;

    bool little = REF(le);

    Size bin_size;
    const Byte* bin_data = Cell_Binary_Size_At(&bin_size, ARG(binary));

    Value* options = ARG(options);

    REBLEN arity = Cell_Series_Len_At(options);
    if (arity != 1 and arity != 2)
        fail("DECODE-INTEGER requires length 1 or 2 options for now");
    bool no_sign = rebUnboxBoolean(  // signed is C keyword
        "switch first", options, "[",
            "'+ ['true] '+/- ['false]",
            "fail -{First DECODE-INTEGER option must be + or +/-}-",
        "]"
    );
    REBLEN num_bytes;
    if (arity == 1)
        num_bytes = bin_size;
    else {
        num_bytes = rebUnboxInteger(
            "(match integer! second", options, ") else [",
                "fail -{Second DECODE-INTEGER option must be an integer}-",
            "]"
        );
        if (bin_size != num_bytes)
            return FAIL("Input length mistmatches DECODE-INTEGER size option");
    }
    if (num_bytes <= 0) {
        //
        // !!! Should #{} empty binary be 0 or error?  (Historically, 0.)
        //
        fail("Size for DEBIN decoding must be at least 1");
    }

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    REBINT delta = little ? -1 : 1;
    const Byte* bp = bin_data;
    if (little)
        bp += num_bytes - 1;  // go backwards

    REBINT n = num_bytes;

    if (n == 0)
        return Init_Integer(OUT, 0);  // !!! Only if we let num_bytes = 0

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
            return Init_Integer(OUT, -1);
        }
        return Init_Integer(OUT, 0);
    }

    // Not using BigNums (yet) so max representation is 8 bytes after
    // leading 0x00 or 0xFF stripped away
    //
    if (n > 8)
        return FAIL(Error_Out_Of_Range(ARG(binary)));

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

    if (no_sign and i < 0)  // may become signed via shift due to 63-bit limit
        return FAIL(Error_Out_Of_Range(ARG(binary)));

    return Init_Integer(OUT, i);
}
