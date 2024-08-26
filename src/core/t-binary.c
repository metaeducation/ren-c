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


static Binary* Make_Binary_BE64(const Value* arg)
{
    Binary* bin = Make_Binary(8);
    Byte* bp = Binary_Head(bin);

    REBI64 i;
    REBDEC d;
    const Byte* cp;
    if (Is_Integer(arg)) {
        assert(sizeof(REBI64) == 8);
        i = VAL_INT64(arg);
        cp = c_cast(Byte*, &i);
    }
    else {
        assert(sizeof(REBDEC) == 8);
        d = VAL_DECIMAL(arg);
        cp = c_cast(Byte*, &d);
    }

  #ifdef ENDIAN_LITTLE
  blockscope {
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[7 - n];
  }
  #elif defined(ENDIAN_BIG)
  blockscope {
    REBLEN n;
    for (n = 0; n < 8; ++n)
        bp[n] = cp[n];
  }
  #else
    #error "Unsupported CPU endian"
  #endif

    Term_Binary_Len(bin, 8);
    return bin;
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
static Bounce MAKE_TO_Binary_Common(Level* level_, const Value* arg)
{
    switch (VAL_TYPE(arg)) {
    case REB_BINARY: {
        Size size;
        const Byte* data = Cell_Binary_Size_At(&size, arg);
        return Init_Binary(OUT, Copy_Bytes(data, size)); }

      case REB_TEXT:
      case REB_FILE:
      case REB_EMAIL:
      case REB_URL:
      case REB_TAG:
      case REB_ISSUE: {
        Size utf8_size;
        Utf8(const*) utf8 = Cell_Utf8_Size_At(&utf8_size, arg);

        Binary* bin = Make_Binary(utf8_size);
        memcpy(Binary_Head(bin), utf8, utf8_size);
        Term_Binary_Len(bin, utf8_size);
        return Init_Binary(OUT, bin); }

      case REB_BLOCK: {
        Join_Binary_In_Byte_Buf(arg, -1);
        Binary* bin = cast(
            Binary*,
            Copy_Flex_Core(BYTE_BUF, FLEX_FLAGS_NONE)
        );
        return Init_Binary(OUT, bin); }

      case REB_TUPLE: {
        REBLEN len = Cell_Sequence_Len(arg);
        Binary* bin = Make_Binary(len);
        if (Did_Get_Sequence_Bytes(Binary_Head(bin), arg, len)) {
            Term_Binary_Len(bin, len);
            return Init_Binary(OUT, bin);
        }
        fail ("TUPLE! did not consist entirely of INTEGER! values 0-255"); }

      case REB_BITSET:
        return Init_Binary(
            OUT,
            Copy_Bytes(Binary_Head(Cell_Binary(arg)), Cell_Series_Len_Head(arg))
        );

      case REB_MONEY: {
        Binary* bin = Make_Binary(12);
        deci_to_binary(Binary_Head(bin), VAL_MONEY_AMOUNT(arg));
        Term_Binary_Len(bin, 12);
        return Init_Binary(OUT, bin); }

      default:
        return RAISE(Error_Bad_Make(REB_BINARY, arg));
    }
}


//
//  MAKE_Binary: C
//
// See also: MAKE_String, which is similar.
//
Bounce MAKE_Binary(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* def
){
    assert(kind == REB_BINARY);

    if (parent)
        fail (Error_Bad_Make_Parent(kind, unwrap parent));

    if (Is_Integer(def)) {
        //
        // !!! R3-Alpha tolerated decimal, e.g. `make string! 3.14`, which
        // is semantically nebulous (round up, down?) and generally bad.
        //
        return Init_Binary(OUT, Make_Binary(Int32s(def, 0)));
    }

    if (Is_Block(def)) {  // was construction syntax, #[binary [#{0001} 2]]
        rebPushContinuation(
            cast(Value*, OUT),
            LEVEL_MASK_NONE,
            Canon(TO), Canon(BINARY_X),
                Canon(REDUCE), rebQ(def)  // rebQ() copies cell, survives frame
        );
        return BOUNCE_DELEGATE;
    }

    return MAKE_TO_Binary_Common(level_, def);
}


//
//  TO_Binary: C
//
Bounce TO_Binary(Level* level_, Kind kind, const Value* arg)
{
    assert(kind == REB_BINARY);
    UNUSED(kind);

    if (Is_Integer(arg) or Is_Decimal(arg))
        return Init_Series(OUT, REB_BINARY, Make_Binary_BE64(arg));

    return MAKE_TO_Binary_Common(level_, arg);
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
void MF_Binary(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    if (GET_MOLD_FLAG(mo, MOLD_FLAG_ALL) and VAL_INDEX(v) != 0)
        Pre_Mold(mo, v); // #[binary!

    Size size;
    const Byte* data = Cell_Binary_Size_At(&size, v);

    switch (Get_System_Int(SYS_OPTIONS, OPTIONS_BINARY_BASE, 16)) {
      default:
      case 16: {
        Append_Ascii(mo->series, "#{"); // default, so #{...} not #16{...}

        const bool brk = (size > 32);
        Form_Base16(mo, data, size, brk);
        break; }

      case 64: {
        Append_Ascii(mo->series, "64#{");

        const bool brk = (size > 64);
        Form_Base64(mo, data, size, brk);
        break; }

      case 2: {
        Append_Ascii(mo->series, "2#{");

        const bool brk = (size > 8);
        Form_Base2(mo, data, size, brk);
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
    Value* v = D_ARG(1);
    assert(Is_Binary(v));

    Option(SymId) id = Symbol_Id(verb);

    switch (id) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n;
        if (not Did_Get_Series_Index_From_Picker(&n, v, picker))
            return nullptr;

        Byte b = *Binary_At(Cell_Binary(v), n);

        return Init_Integer(OUT, b);
      }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n;
        if (not Did_Get_Series_Index_From_Picker(&n, v, picker))
            fail (Error_Out_Of_Range(picker));

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
            fail (PARAM(value));
        }

        if (i > 0xff)
            fail (Error_Out_Of_Range(setval));

        Binary* bin = Cell_Binary_Ensure_Mutable(v);
        Binary_Head(bin)[n] = cast(Byte, i);

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
        // a /PART can actually erase data.
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
        else if (Is_Antiform(arg)) {  // only SPLICE! in typecheck
            fail (Error_Bad_Antiform(arg));  // ...but that doesn't filter yet
        }
        else if (Any_List(arg) or Any_Sequence(arg))
            fail (ARG(value));

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
            fail (pattern);

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

        if (ret >= cast(REBLEN, tail))
            return nullptr;  // Don't Proxy_Multi_Returns()

        if (id == SYM_FIND) {
            Init_Series_At(
                ARG(tail),
                REB_BINARY,
                Cell_Binary(v),
                ret + size
            );
            Init_Series_At(
                OUT,
                REB_BINARY,
                Cell_Binary(v),
                ret
            );
            return Proxy_Multi_Returns(level_);
        }

        ret++;
        if (ret >= cast(REBLEN, tail))
            return nullptr;

        return Init_Integer(OUT, *Binary_At(Cell_Binary(v), ret)); }

      case SYM_TAKE: {
        INCLUDE_PARAMS_OF_TAKE;

        Binary* bin = Cell_Binary_Ensure_Mutable(v);

        UNUSED(PARAM(series));

        if (REF(deep))
            fail (Error_Bad_Refines_Raw());

        REBINT len;
        if (REF(part)) {
            len = Part_Len_May_Modify_Index(v, ARG(part));
            if (len == 0) {
                Heart heart = Cell_Heart_Ensure_Noquote(v);
                return Init_Series(OUT, heart, Make_Binary(0));
            }
        } else
            len = 1;

        // Note that /PART can change index

        REBINT tail = cast(REBINT, Cell_Series_Len_Head(v));

        if (REF(last)) {
            if (tail - len < 0) {
                VAL_INDEX_RAW(v) = 0;
                len = tail;
            }
            else
                VAL_INDEX_RAW(v) = cast(REBLEN, tail - len);
        }

        if (cast(REBINT, VAL_INDEX(v)) >= tail) {
            if (not REF(part))
                return RAISE(Error_Nothing_To_Take_Raw());

            Heart heart = Cell_Heart_Ensure_Noquote(v);
            return Init_Series(OUT, heart, Make_Binary(0));
        }

        // if no /PART, just return value, else return string
        //
        if (not REF(part)) {
            Init_Integer(OUT, *Cell_Binary_At(v));
        }
        else {
            Init_Binary(
                OUT,
                Copy_Binary_At_Len(bin, VAL_INDEX(v), len)
            );
        }
        Remove_Any_Series_Len(v, VAL_INDEX(v), len);  // bad UTF-8 alias fails
        return OUT; }

      case SYM_CLEAR: {
        Binary* bin = Cell_Binary_Ensure_Mutable(v);

        REBINT tail = cast(REBINT, Cell_Series_Len_Head(v));
        REBINT index = cast(REBINT, VAL_INDEX(v));

        if (index >= tail)
            return COPY(v); // clearing after available data has no effect

        // !!! R3-Alpha would take this opportunity to make it so that if the
        // series is now empty, it reclaims the "bias" (unused capacity at
        // the head of the Flex).  One of many behaviors worth reviewing.
        //
        if (index == 0 and Get_Flex_Flag(bin, DYNAMIC))
            Unbias_Flex(bin, false);

        Term_Binary_Len(bin, cast(REBLEN, index));  // may have string alias
        return COPY(v); }

    //-- Creation:

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(value));
        UNUSED(REF(deep));  // /DEEP is historically ignored on BINARY!

        REBINT len = Part_Len_May_Modify_Index(v, ARG(part));

        return Init_Series(
            OUT,
            REB_BINARY,
            Copy_Binary_At_Len(Cell_Flex(v), VAL_INDEX(v), len)
        ); }

    //-- Bitwise:

      case SYM_BITWISE_AND:
      case SYM_BITWISE_OR:
      case SYM_BITWISE_XOR:
      case SYM_BITWISE_AND_NOT: {
        Value* arg = D_ARG(2);
        if (not Is_Binary(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));

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

        return Init_Binary(OUT, b); }

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
        Value* arg = D_ARG(2);
        Binary* bin = Cell_Binary_Ensure_Mutable(v);

        REBINT amount;
        if (Is_Integer(arg))
            amount = VAL_INT32(arg);
        else if (Is_Binary(arg))
            fail (arg); // should work
        else
            fail (arg); // what about other types?

        if (id == SYM_SUBTRACT)
            amount = -amount;

        if (amount == 0) // adding or subtracting 0 works, even #{} + 0
            return COPY(v);

        if (Cell_Series_Len_At(v) == 0) // add/subtract to #{} otherwise
            fail (Error_Overflow_Raw());

        while (amount != 0) {
            REBLEN wheel = Cell_Series_Len_Head(v) - 1;
            while (true) {
                Byte* b = Binary_At(bin, wheel);
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
        return COPY(v); }

    //-- Special actions:

      case SYM_SWAP: {
        Value* arg = D_ARG(2);

        if (VAL_TYPE(v) != VAL_TYPE(arg))
            fail (Error_Not_Same_Type_Raw());

        Byte* v_at = Cell_Binary_At_Ensure_Mutable(v);
        Byte* arg_at = Cell_Binary_At_Ensure_Mutable(arg);

        REBINT tail = cast(REBINT, Cell_Series_Len_Head(v));
        REBINT index = cast(REBINT, VAL_INDEX(v));

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
        Byte* bp = Cell_Binary_At_Ensure_Mutable(v);  // index may've changed

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
            fail (Error_Bad_Refines_Raw());

        if (REF(case)) {
            // Ignored...all BINARY! sorts are case-sensitive.
        }

        if (REF(compare))
            fail (Error_Bad_Refines_Raw());  // !!! not in R3-Alpha

        Flags thunk = 0;

        Copy_Cell(OUT, v);  // copy to output before index adjustment

        REBLEN len = Part_Len_May_Modify_Index(v, ARG(part));
        Byte* data_at = Cell_Binary_At_Ensure_Mutable(v);  // ^ index changes

        if (len <= 1)
            return OUT;

        REBLEN skip;
        if (not REF(skip))
            skip = 1;
        else {
            skip = Get_Num_From_Arg(ARG(skip));
            if (skip <= 0 or (len % skip != 0) or skip > len)
                fail (PARAM(skip));
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
            return TRASH;
        }

        REBINT tail = cast(REBINT, Cell_Series_Len_Head(v));
        REBINT index = cast(REBINT, VAL_INDEX(v));

        if (REF(only)) {
            if (index >= tail)
                return Init_Blank(OUT);

            index += cast(REBLEN, Random_Int(REF(secure)))
                % (tail - index);
            const Binary* bin = Cell_Binary(v);
            return Init_Integer(OUT, *Binary_At(bin, index));  // PICK
        }

        Binary* bin = Cell_Binary_Ensure_Mutable(v);

        bool secure = REF(secure);
        REBLEN n;
        for (n = Binary_Len(bin) - index; n > 1;) {
            REBLEN k = index + cast(REBLEN, Random_Int(secure)) % n;
            n--;
            Byte swap = *Binary_At(bin, k);
            *Binary_At(bin, k) = *Binary_At(bin, n + index);
            *Binary_At(bin, n + index) = swap;
        }
        return COPY(v); }

      default:
        break;
    }

    fail (UNHANDLED);
}


//
//  enbin: native [
//
//  "Encode value as a Little Endian or Big Endian BINARY!, signed/unsigned"
//
//      return: [binary!]
//      settings "[<LE or BE> <+ or +/-> <number of bytes>] (pre-COMPOSE'd)"
//          [block!]
//      value "Value to encode (currently only integers are supported)"
//          [integer!]
//  ]
//
DECLARE_NATIVE(enbin)
//
// !!! This routine may wind up being folded into ENCODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
{
    INCLUDE_PARAMS_OF_ENBIN;

    Value* settings = rebValue("compose", ARG(settings));
    if (Cell_Series_Len_At(settings) != 3)
        fail ("ENBIN requires list of length 3 for settings for now");
    bool little = rebUnboxLogic(
        "switch first", settings, "[",
            "'BE [false] 'LE [true]",
            "fail {First element of ENBIN settings must be BE or LE}",
        "]"
    );
    bool no_sign = rebUnboxLogic(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of ENBIN settings must be + or +/-}",
        "]"
    );
    REBINT num_bytes = rebUnboxInteger(
        "(match integer! third", settings, ") else [",
            "fail {Third element of ENBIN settings must be an integer}",
        "]"
    );
    if (num_bytes <= 0)
        fail ("Size for ENBIN encoding must be at least 1");
    rebRelease(settings);

    // !!! Implementation is somewhat inefficient, but trying to not violate
    // the C standard and write code that is general (and may help generalize
    // with BigNum conversions as well).  Improvements welcome, but trying
    // to be correct for starters...

    Binary* bin = Make_Binary(num_bytes);

    REBINT delta = little ? 1 : -1;
    Byte* bp = Binary_Head(bin);
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
        rebJumps(
            "fail [", ARG(value), "{exceeds}", rebI(num_bytes), "{bytes}]"
        );

    // The process of byte production of a positive number shouldn't give us
    // something with the high bit set in a signed representation.
    //
    if (not no_sign and not negative and *(bp - delta) >= 0x80)
        rebJumps(
            "fail [",
                ARG(value), "{aliases a negative value with signed}",
                "{encoding of only}", rebI(num_bytes), "{bytes}",
            "]"
        );

    Term_Binary_Len(bin, num_bytes);
    return Init_Binary(OUT, bin);
}


//
//  debin: native [
//
//  "Decode BINARY! as Little Endian or Big Endian, signed/unsigned value"
//
//      return: [integer!]
//      settings "[<LE or BE> <+ or +/-> <number of bytes>] (pre-COMPOSE'd)"
//          [block!]
//      binary "Decoded (defaults length of binary for number of bytes)"
//          [binary!]
//  ]
//
DECLARE_NATIVE(debin)
//
// !!! This routine may wind up being folded into DECODE as a block-oriented
// syntax for talking to the "little endian" and "big endian" codecs, but
// giving it a unique name for now.
{
    INCLUDE_PARAMS_OF_DEBIN;

    Size bin_size;
    const Byte* bin_data = Cell_Binary_Size_At(&bin_size, ARG(binary));

    Value* settings = rebValue("compose", ARG(settings));

    REBLEN arity = Cell_Series_Len_At(settings);
    if (arity != 2 and arity != 3)
        fail("DEBIN requires list of length 2 or 3 for settings for now");
    bool little = rebUnboxLogic(
        "switch first", settings, "[",
            "'BE [false] 'LE [true]",
            "fail {First element of DEBIN settings must be BE or LE}",
        "]"
    );
    bool no_sign = rebUnboxLogic(
        "switch second", settings, "[",
            "'+ [true] '+/- [false]",
            "fail {Second element of DEBIN settings must be + or +/-}",
        "]"
    );
    REBLEN num_bytes;
    if (arity == 2)
        num_bytes = bin_size;
    else {
        num_bytes = rebUnboxInteger(
            "(match integer! third", settings, ") else [",
                "fail {Third element of DEBIN settings must be an integer}",
            "]"
        );
        if (bin_size != num_bytes)
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
        fail (Error_Out_Of_Range(ARG(binary)));

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
        fail (Error_Out_Of_Range(ARG(binary)));
    }

    return Init_Integer(OUT, i);
}
