//
//  File: %t-bitset.c
//  Summary: "bitset datatype"
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

#define MAX_BITSET 0x7fffffff

INLINE bool BITS_NOT(Binary* s) {
    return MISC(s).negated;
}

INLINE void INIT_BITS_NOT(Binary* s, bool negated) {
    MISC(s).negated = negated;
}


//
//  CT_Bitset: C
//
REBINT CT_Bitset(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) return (
        BITS_NOT(Cell_Bitset(a)) == BITS_NOT(Cell_Bitset(b))
        &&
        Compare_Binary_Vals(a, b) == 0
    );
    return -1;
}


//
//  Make_Bitset: C
//
// Return a bitset series (binary.
//
// len: the # of bits in the bitset.
//
Binary* Make_Bitset(REBLEN len)
{
    Binary* ser;

    len = (len + 7) / 8;
    ser = Make_Binary(len);
    Clear_Series(ser);
    Set_Series_Len(ser, len);
    INIT_BITS_NOT(ser, false);

    return ser;
}


//
//  MF_Bitset: C
//
void MF_Bitset(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Pre_Mold(mo, v); // #[bitset! or make bitset!

    Binary* s = Cell_Bitset(v);

    if (BITS_NOT(s))
        Append_Unencoded(mo->series, "[not bits ");

    DECLARE_VALUE (alias);
    Init_Binary(alias, s);  // MF_Binary expects positional BINARY!
    MF_Binary(mo, alias, false); // false = mold, don't form

    if (BITS_NOT(s))
        Append_Utf8_Codepoint(mo->series, ']');

    End_Mold(mo);
}


//
//  MAKE_Bitset: C
//
REB_R MAKE_Bitset(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_BITSET);
    UNUSED(kind);

    REBINT len = Find_Max_Bit(arg);

    // Determine size of bitset. Returns -1 for errors.
    //
    // !!! R3-alpha construction syntax said 0xFFFFFF while the A_MAKE
    // path used 0x0FFFFFFF.  Assume A_MAKE was more likely right.
    //
    if (len < 0 || len > 0x0FFFFFFF)
        fail (Error_Invalid(arg));

    Binary* ser = Make_Bitset(len);
    Init_Bitset(out, ser);

    if (Is_Integer(arg))
        return out; // allocated at a size, no contents.

    if (Is_Binary(arg)) {
        memcpy(Binary_Head(ser), Cell_Binary_At(arg), len/8 + 1);
        return out;
    }

    Set_Bits(ser, arg, true);
    return out;
}


//
//  TO_Bitset: C
//
REB_R TO_Bitset(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Bitset(out, kind, arg);
}


//
//  Find_Max_Bit: C
//
// Return integer number for the maximum bit number defined by
// the value. Used to determine how much space to allocate.
//
REBINT Find_Max_Bit(const Cell* val)
{
    REBINT maxi = 0;
    REBINT n;

    switch (VAL_TYPE(val)) {

    case REB_CHAR:
        maxi = VAL_CHAR(val) + 1;
        break;

    case REB_INTEGER:
        maxi = Int32s(val, 0);
        break;

    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
//  case REB_ISSUE:
    case REB_TAG: {
        n = VAL_INDEX(val);
        Ucs2(const*) up = Cell_String_At(val);
        for (; n < cast(REBINT, VAL_LEN_HEAD(val)); n++) {
            REBUNI c;
            up = Ucs2_Next(&c, up);
            if (c > maxi)
                maxi = c;
        }
        maxi++;
        break; }

    case REB_BINARY:
        maxi = VAL_LEN_AT(val) * 8 - 1;
        if (maxi < 0) maxi = 0;
        break;

    case REB_BLOCK:
        for (val = Cell_Array_At(val); NOT_END(val); val++) {
            n = Find_Max_Bit(val);
            if (n > maxi) maxi = n;
        }
        //maxi++;
        break;

    case REB_BLANK:
        maxi = 0;
        break;

    default:
        return -1;
    }

    return maxi;
}


//
//  Check_Bit: C
//
// Check bit indicated. Returns true if set.
// If uncased is true, try to match either upper or lower case.
//
bool Check_Bit(Binary* bset, REBLEN c, bool uncased)
{
    REBLEN i, n = c;
    REBLEN tail = Series_Len(bset);
    bool flag = false;

    if (uncased) {
        if (n >= UNICODE_CASES)
            uncased = false; // no need to check
        else
            n = LO_CASE(c);
    }

    // Check lowercase char:
retry:
    i = n >> 3;
    if (i < tail)
        flag = did (Binary_Head(bset)[i] & (1 << (7 - (n & 7))));

    // Check uppercase if needed:
    if (uncased && !flag) {
        n = UP_CASE(c);
        uncased = false;
        goto retry;
    }

    if (BITS_NOT(bset))
        return not flag;

    return flag;
}


//
//  Set_Bit: C
//
// Set/clear a single bit. Expand if needed.
//
void Set_Bit(Binary* bset, REBLEN n, bool set)
{
    REBLEN i = n >> 3;
    REBLEN tail = Binary_Len(bset);
    Byte bit;

    // Expand if not enough room:
    if (i >= tail) {
        if (!set) return; // no need to expand
        Expand_Series(bset, tail, (i - tail) + 1);
        CLEAR(Binary_At(bset, tail), (i - tail) + 1);
    }

    bit = 1 << (7 - ((n) & 7));
    if (set)
        Binary_Head(bset)[i] |= bit;
    else
        Binary_Head(bset)[i] &= ~bit;
}


//
//  Set_Bits: C
//
// Set/clear bits indicated by strings and chars and ranges.
//
bool Set_Bits(Binary* bset, const Value* val, bool set)
{
    Fail_If_Read_Only_Series(bset);

    if (Is_Char(val)) {
        Set_Bit(bset, VAL_CHAR(val), set);
        return true;
    }

    if (Is_Integer(val)) {
        REBLEN n = Int32s(val, 0);
        if (n > MAX_BITSET)
            return false;
        Set_Bit(bset, n, set);
        return true;
    }

    if (Is_Binary(val)) {
        REBLEN i = VAL_INDEX(val);

        Byte *bp = Cell_Binary_Head(val);
        for (; i != VAL_LEN_HEAD(val); i++)
            Set_Bit(bset, bp[i], set);

        return true;
    }

    if (ANY_STRING(val)) {
        REBLEN i = VAL_INDEX(val);
        Ucs2(const*) up = Cell_String_At(val);
        for (; i < VAL_LEN_HEAD(val); ++i) {
            REBUNI c;
            up = Ucs2_Next(&c, up);
            Set_Bit(bset, c, set);
        }

        return true;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    Cell* item = Cell_Array_At(val);

    if (
        NOT_END(item)
        && Is_Word(item)
        && Cell_Word_Id(item) == SYM_NOT
    ){
        INIT_BITS_NOT(bset, true);
        item++;
    }

    // Loop through block of bit specs:

    for (; NOT_END(item); item++) {

        switch (VAL_TYPE(item)) {
        case REB_CHAR: {
            REBUNI c = VAL_CHAR(item);
            if (
                NOT_END(item + 1)
                && Is_Word(item + 1)
                && Cell_Word_Id(item + 1) == SYM_HYPHEN
            ){
                item += 2;
                if (Is_Char(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < n); // post-increment: test before overflow
                }
                else
                    fail (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Set_Bit(bset, c, set);
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(KNOWN(item), 0);
            if (n > MAX_BITSET)
                return false;
            if (
                NOT_END(item + 1)
                && Is_Word(item + 1)
                && Cell_Word_Id(item + 1) == SYM_HYPHEN
            ){
                REBUNI c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    fail (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Set_Bit(bset, n, set);
            break; }

        case REB_BINARY:
        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG:
//      case REB_ISSUE:
            Set_Bits(bset, KNOWN(item), set);
            break;

        case REB_WORD: {
            // Special: BITS #{000...}
            if (not Is_Word(item) or Cell_Word_Id(item) != SYM_BITS)
                return false;
            item++;
            if (not Is_Binary(item))
                return false;
            REBLEN n = VAL_LEN_AT(item);
            REBUNI c = Series_Len(bset);
            if (n >= c) {
                Expand_Series(bset, c, (n - c));
                CLEAR(Binary_At(bset, c), (n - c));
            }
            memcpy(Binary_Head(bset), Cell_Binary_At(item), n);
            break; }

        default:
            return false;
        }
    }

    return true;
}


//
//  Check_Bits: C
//
// Check bits indicated by strings and chars and ranges.
// If uncased is true, try to match either upper or lower case.
//
bool Check_Bits(Binary* bset, const Value* val, bool uncased)
{
    if (Is_Char(val))
        return Check_Bit(bset, VAL_CHAR(val), uncased);

    if (Is_Integer(val))
        return Check_Bit(bset, Int32s(val, 0), uncased);

    if (Is_Binary(val)) {
        REBLEN i = VAL_INDEX(val);
        Byte *bp = Cell_Binary_Head(val);
        for (; i != VAL_LEN_HEAD(val); ++i)
            if (Check_Bit(bset, bp[i], uncased))
                return true;
        return false;
    }

    if (ANY_STRING(val)) {
        REBLEN i = VAL_INDEX(val);
        Ucs2(const*) up = Cell_String_At(val);
        for (; i != VAL_LEN_HEAD(val); ++i) {
            REBUNI c;
            up = Ucs2_Next(&c, up);
            if (Check_Bit(bset, c, uncased))
                return true;
        }

        return false;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    Cell* item;
    for (item = Cell_Array_At(val); NOT_END(item); item++) {

        switch (VAL_TYPE(item)) {

        case REB_CHAR: {
            REBUNI c = VAL_CHAR(item);
            if (Is_Word(item + 1) && Cell_Word_Id(item + 1) == SYM_HYPHEN) {
                item += 2;
                if (Is_Char(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(KNOWN(item), 0);
            if (n > 0xffff)
                return false;
            if (Is_Word(item + 1) && Cell_Word_Id(item + 1) == SYM_HYPHEN) {
                REBUNI c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Check_Bit(bset, n, uncased))
                    return true;
            break; }

        case REB_BINARY:
        case REB_TEXT:
        case REB_FILE:
        case REB_EMAIL:
        case REB_URL:
        case REB_TAG:
//      case REB_ISSUE:
            if (Check_Bits(bset, KNOWN(item), uncased))
                return true;
            break;

        default:
            fail (Error_Invalid_Type(VAL_TYPE(item)));
        }
    }
    return false;
}


//
//  PD_Bitset: C
//
REB_R PD_Bitset(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    Binary* ser = Cell_Bitset(pvs->out);

    if (opt_setval == nullptr) {
        if (Check_Bits(ser, picker, false))
            return Init_True(pvs->out);
        return nullptr; // !!! Red false on out of range, R3-Alpha NONE! (?)
    }

    if (Set_Bits(
        ser,
        picker,
        BITS_NOT(ser)
            ? IS_FALSEY(opt_setval)
            : IS_TRUTHY(opt_setval)
    )){
        return R_INVISIBLE;
    }

    return R_UNHANDLED;
}


//
//  Trim_Tail_Zeros: C
//
// Remove extra zero bytes from end of byte string.
//
void Trim_Tail_Zeros(Binary* ser)
{
    REBLEN len = Binary_Len(ser);
    Byte *bp = Binary_Head(ser);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    Term_Binary_Len(ser, len);
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;
    Binary* ser;

    // !!! Set_Bits does locked series check--what should the more general
    // responsibility be for checking?

    switch (Cell_Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(value)); // covered by `value`
        Option(SymId) property = Cell_Word_Id(ARG(property));
        assert(property != SYM_0);

        switch (property) {
        case SYM_LENGTH:
            return Init_Integer(value, VAL_LEN_HEAD(value) * 8);

        case SYM_TAIL_Q:
            // Necessary to make EMPTY? work:
            return Init_Logic(OUT, VAL_LEN_HEAD(value) == 0);

        default:
            break;
        }

        break; }

    // Add AND, OR, XOR

    case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(PAR(series));
        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(only))
            fail (Error_Bad_Refines_Raw());
        if (REF(skip)) {
            UNUSED(ARG(size));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(last))
            fail (Error_Bad_Refines_Raw());
        if (REF(reverse))
            fail (Error_Bad_Refines_Raw());
        if (REF(tail))
            fail (Error_Bad_Refines_Raw());
        if (REF(match))
            fail (Error_Bad_Refines_Raw());

        if (not Check_Bits(Cell_Bitset(value), arg, REF(case)))
            return nullptr;
        return Init_Bar(OUT);
    }

    case SYM_COMPLEMENT:
    case SYM_NEGATE:
        ser = cast(Binary*,
            Copy_Sequence_Core(Cell_Bitset(value), NODE_FLAG_MANAGED)
        );
        INIT_BITS_NOT(ser, not BITS_NOT(Cell_Bitset(value)));
        Init_Bitset(value, ser);
        goto return_bitset;

    case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
    case SYM_INSERT: {
        if (IS_NULLED_OR_BLANK(arg)) {
            RETURN (value); // don't fail on read only if it would be a no-op
        }
        Fail_If_Read_Only_Series(Cell_Bitset(value));

        bool diff;
        if (BITS_NOT(Cell_Bitset(value)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(Cell_Bitset(value), arg, diff))
            fail (Error_Invalid(arg));
        goto return_bitset; }

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PAR(series));
        if (REF(map)) {
            UNUSED(ARG(key));
            fail (Error_Bad_Refines_Raw());
        }

        if (not REF(part))
            fail (Error_Missing_Arg_Raw());

        if (not Set_Bits(Cell_Bitset(value), ARG(limit), false))
            fail (ARG(limit));

        goto return_bitset; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PAR(value));
        if (REF(part)) {
            UNUSED(ARG(limit));
            fail (Error_Bad_Refines_Raw());
        }
        if (REF(deep))
            fail (Error_Bad_Refines_Raw());
        if (REF(types)) {
            UNUSED(ARG(kinds));
            fail (Error_Bad_Refines_Raw());
        }

        Init_Any_Series_At(
            OUT,
            REB_BITSET,
            Copy_Sequence_At_Position(value),
            VAL_INDEX(value) // !!! can bitset ever not be at 0?
        );
        INIT_BITS_NOT(Cell_Bitset(OUT), BITS_NOT(Cell_Bitset(value)));
        return OUT; }

    case SYM_CLEAR:
        Fail_If_Read_Only_Series(Cell_Bitset(value));
        Clear_Series(Cell_Bitset(value));
        goto return_bitset;

    case SYM_INTERSECT:
    case SYM_UNION:
    case SYM_DIFFERENCE:
        if (!Is_Bitset(arg) && !Is_Binary(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));
        ser = Xandor_Binary(verb, value, arg);
        Trim_Tail_Zeros(ser);
        return Init_Any_Series(OUT, VAL_TYPE(value), ser);

    default:
        break;
    }

    fail (Error_Illegal_Action(REB_BITSET, verb));

  return_bitset:;

    Copy_Cell(OUT, value);
    return OUT;
}
