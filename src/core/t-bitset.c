//
//  file: %t-bitset.c
//  summary: "bitset datatype"
//  section: datatypes
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    Binary* flex;

    len = (len + 7) / 8;
    flex = Make_Binary(len);
    Clear_Flex(flex);
    Set_Flex_Len(flex, len);
    INIT_BITS_NOT(flex, false);

    return flex;
}


//
//  MF_Bitset: C
//
void MF_Bitset(Molder* mo, const Cell* v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Begin_Non_Lexical_Mold(mo, v); // #[bitset! or make bitset!

    Binary* s = Cell_Bitset(v);

    if (BITS_NOT(s))
        Append_Unencoded(mo->utf8flex, "[not bits ");

    DECLARE_VALUE (alias);
    Init_Blob(alias, s);  // MF_Binary expects positional BINARY!
    MF_Binary(mo, alias, false); // false = mold, don't form

    if (BITS_NOT(s))
        Append_Codepoint(mo->utf8flex, ']');

    End_Non_Lexical_Mold(mo);
}


//
//  MAKE_Bitset: C
//
Bounce MAKE_Bitset(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == TYPE_BITSET);
    UNUSED(kind);

    REBINT len = Find_Max_Bit(arg);

    // Determine size of bitset. Returns -1 for errors.
    //
    // !!! R3-alpha construction syntax said 0xFFFFFF while the A_MAKE
    // path used 0x0FFFFFFF.  Assume A_MAKE was more likely right.
    //
    if (len < 0 || len > 0x0FFFFFFF)
        panic (Error_Invalid(arg));

    Binary* flex = Make_Bitset(len);
    Init_Bitset(out, flex);

    if (Is_Integer(arg))
        return out; // allocated at a size, no contents.

    if (Is_Binary(arg)) {
        memcpy(Binary_Head(flex), Blob_At(arg), len/8 + 1);
        return out;
    }

    Set_Bits(flex, arg, true);
    return out;
}


//
//  TO_Bitset: C
//
Bounce TO_Bitset(Value* out, enum Reb_Kind kind, const Value* arg)
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

    switch (Type_Of(val)) {

    case TYPE_CHAR:
        maxi = VAL_CHAR(val) + 1;
        break;

    case TYPE_INTEGER:
        maxi = Int32s(val, 0);
        break;

    case TYPE_TEXT:
    case TYPE_FILE:
    case TYPE_EMAIL:
    case TYPE_URL:
//  case TYPE_ISSUE:
    case TYPE_TAG: {
        n = VAL_INDEX(val);
        Ucs2(const*) up = String_At(val);
        for (; n < cast(REBINT, VAL_LEN_HEAD(val)); n++) {
            Ucs2Unit c;
            up = Ucs2_Next(&c, up);
            if (c > maxi)
                maxi = c;
        }
        maxi++;
        break; }

    case TYPE_BINARY:
        maxi = Series_Len_At(val) * 8 - 1;
        if (maxi < 0) maxi = 0;
        break;

    case TYPE_BLOCK:
        for (val = Cell_List_At(val); NOT_END(val); val++) {
            n = Find_Max_Bit(val);
            if (n > maxi) maxi = n;
        }
        //maxi++;
        break;

    case TYPE_BLANK:
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
    REBLEN tail = Flex_Len(bset);
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
        Expand_Flex(bset, tail, (i - tail) + 1);
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
    Panic_If_Read_Only_Flex(bset);

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

        Byte *bp = Blob_Head(val);
        for (; i != VAL_LEN_HEAD(val); i++)
            Set_Bit(bset, bp[i], set);

        return true;
    }

    if (Any_String(val)) {
        REBLEN i = VAL_INDEX(val);
        Ucs2(const*) up = String_At(val);
        for (; i < VAL_LEN_HEAD(val); ++i) {
            Ucs2Unit c;
            up = Ucs2_Next(&c, up);
            Set_Bit(bset, c, set);
        }

        return true;
    }

    if (!Any_List(val))
        panic (Error_Invalid_Type(Type_Of(val)));

    Cell* item = Cell_List_At(val);

    if (
        NOT_END(item)
        && Is_Word(item)
        && Word_Id(item) == SYM_NOT
    ){
        INIT_BITS_NOT(bset, true);
        item++;
    }

    // Loop through block of bit specs:

    for (; NOT_END(item); item++) {

        switch (Type_Of(item)) {
        case TYPE_CHAR: {
            Ucs2Unit c = VAL_CHAR(item);
            if (
                NOT_END(item + 1)
                && Is_Word(item + 1)
                && Word_Id(item + 1) == SYM_HYPHEN_1
            ){
                item += 2;
                if (Is_Char(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        panic (Error_Past_End_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < n); // post-increment: test before overflow
                }
                else
                    panic (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Set_Bit(bset, c, set);
            break; }

        case TYPE_INTEGER: {
            REBLEN n = Int32s(KNOWN(item), 0);
            if (n > MAX_BITSET)
                return false;
            if (
                NOT_END(item + 1)
                && Is_Word(item + 1)
                && Word_Id(item + 1) == SYM_HYPHEN_1
            ){
                Ucs2Unit c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        panic (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    panic (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Set_Bit(bset, n, set);
            break; }

        case TYPE_BINARY:
        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG:
//      case TYPE_ISSUE:
            Set_Bits(bset, KNOWN(item), set);
            break;

        case TYPE_WORD: {
            // Special: BITS #{000...}
            if (not Is_Word(item) or Word_Id(item) != SYM_BITS)
                return false;
            item++;
            if (not Is_Binary(item))
                return false;
            REBLEN n = Series_Len_At(item);
            Ucs2Unit c = Flex_Len(bset);
            if (n >= c) {
                Expand_Flex(bset, c, (n - c));
                CLEAR(Binary_At(bset, c), (n - c));
            }
            memcpy(Binary_Head(bset), Blob_At(item), n);
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
        Byte *bp = Blob_Head(val);
        for (; i != VAL_LEN_HEAD(val); ++i)
            if (Check_Bit(bset, bp[i], uncased))
                return true;
        return false;
    }

    if (Any_String(val)) {
        REBLEN i = VAL_INDEX(val);
        Ucs2(const*) up = String_At(val);
        for (; i != VAL_LEN_HEAD(val); ++i) {
            Ucs2Unit c;
            up = Ucs2_Next(&c, up);
            if (Check_Bit(bset, c, uncased))
                return true;
        }

        return false;
    }

    if (!Any_List(val))
        panic (Error_Invalid_Type(Type_Of(val)));

    // Loop through block of bit specs

    Cell* item;
    for (item = Cell_List_At(val); NOT_END(item); item++) {

        switch (Type_Of(item)) {

        case TYPE_CHAR: {
            Ucs2Unit c = VAL_CHAR(item);
            if (Is_Word(item + 1) && Word_Id(item + 1) == SYM_HYPHEN_1) {
                item += 2;
                if (Is_Char(item)) {
                    REBLEN n = VAL_CHAR(item);
                    if (n < c)
                        panic (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    panic (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case TYPE_INTEGER: {
            REBLEN n = Int32s(KNOWN(item), 0);
            if (n > 0xffff)
                return false;
            if (Is_Word(item + 1) && Word_Id(item + 1) == SYM_HYPHEN_1) {
                Ucs2Unit c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        panic (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    panic (Error_Invalid_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Check_Bit(bset, n, uncased))
                    return true;
            break; }

        case TYPE_BINARY:
        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG:
//      case TYPE_ISSUE:
            if (Check_Bits(bset, KNOWN(item), uncased))
                return true;
            break;

        default:
            panic (Error_Invalid_Type(Type_Of(item)));
        }
    }
    return false;
}


//
//  PD_Bitset: C
//
Bounce PD_Bitset(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    Binary* flex = Cell_Bitset(pvs->out);

    if (opt_setval == nullptr) {
        if (Check_Bits(flex, picker, false))
            return Init_Logic(pvs->out, true);
        return nullptr; // !!! Red false on out of range, R3-Alpha NONE! (?)
    }

    if (Set_Bits(
        flex,
        picker,
        BITS_NOT(flex)
            ? IS_FALSEY(opt_setval)
            : IS_TRUTHY(opt_setval)
    )){
        return BOUNCE_INVISIBLE;
    }

    return BOUNCE_UNHANDLED;
}


//
//  Trim_Tail_Zeros: C
//
// Remove extra zero bytes from end of byte string.
//
void Trim_Tail_Zeros(Binary* flex)
{
    REBLEN len = Binary_Len(flex);
    Byte *bp = Binary_Head(flex);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    Term_Binary_Len(flex, len);
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    Value* value = D_ARG(1);
    Value* arg = D_ARGC > 1 ? D_ARG(2) : nullptr;
    Binary* flex;

    // !!! Set_Bits does locked series check--what should the more general
    // responsibility be for checking?

    switch (Word_Id(verb)) {

    case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;

        UNUSED(ARG(VALUE)); // covered by `value`
        Option(SymId) property = Word_Id(ARG(PROPERTY));
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

        UNUSED(PARAM(SERIES));
        UNUSED(PARAM(VALUE));
        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(ONLY))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(SKIP)) {
            UNUSED(ARG(SIZE));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(LAST))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(REVERSE))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(TAIL))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(MATCH))
            panic (Error_Bad_Refines_Raw());

        if (not Check_Bits(Cell_Bitset(value), arg, Bool_ARG(CASE)))
            return LOGIC(false);
        return LOGIC(true);
    }

    case SYM_COMPLEMENT:
    case SYM_NEGATE:
        flex = cast(Binary*,
            Copy_Non_Array_Flex_Core(Cell_Bitset(value), NODE_FLAG_MANAGED)
        );
        INIT_BITS_NOT(flex, not BITS_NOT(Cell_Bitset(value)));
        Init_Bitset(value, flex);
        goto return_bitset;

    case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
    case SYM_INSERT: {
        PANIC_IF_ERROR(arg);
        if (Is_Nulled(arg) or Is_Blank(arg))
            RETURN (value); // don't panic on read only if it would be a no-op

        Panic_If_Read_Only_Flex(Cell_Bitset(value));

        bool diff;
        if (BITS_NOT(Cell_Bitset(value)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(Cell_Bitset(value), arg, diff))
            panic (Error_Invalid(arg));
        goto return_bitset; }

    case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;

        UNUSED(PARAM(SERIES));
        if (Bool_ARG(MAP)) {
            UNUSED(ARG(KEY));
            panic (Error_Bad_Refines_Raw());
        }

        if (not Bool_ARG(PART))
            panic (Error_Missing_Arg_Raw());

        if (not Set_Bits(Cell_Bitset(value), ARG(LIMIT), false))
            panic (ARG(LIMIT));

        goto return_bitset; }

    case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;

        UNUSED(PARAM(VALUE));
        if (Bool_ARG(PART)) {
            UNUSED(ARG(LIMIT));
            panic (Error_Bad_Refines_Raw());
        }
        if (Bool_ARG(DEEP))
            panic (Error_Bad_Refines_Raw());
        if (Bool_ARG(TYPES)) {
            UNUSED(ARG(KINDS));
            panic (Error_Bad_Refines_Raw());
        }

        Init_Any_Series_At(
            OUT,
            TYPE_BITSET,
            Copy_Sequence_At_Position(value),
            VAL_INDEX(value) // !!! can bitset ever not be at 0?
        );
        INIT_BITS_NOT(Cell_Bitset(OUT), BITS_NOT(Cell_Bitset(value)));
        return OUT; }

    case SYM_CLEAR:
        Panic_If_Read_Only_Flex(Cell_Bitset(value));
        Clear_Flex(Cell_Bitset(value));
        goto return_bitset;

    case SYM_INTERSECT:
    case SYM_UNION:
    case SYM_DIFFERENCE:
        if (!Is_Bitset(arg) && !Is_Binary(arg))
            panic (Error_Math_Args(Type_Of(arg), verb));
        flex = Xandor_Binary(verb, value, arg);
        Trim_Tail_Zeros(flex);
        return Init_Any_Series(OUT, Type_Of(value), flex);

    default:
        break;
    }

    panic (Error_Illegal_Action(TYPE_BITSET, verb));

  return_bitset:;

    Copy_Cell(OUT, value);
    return OUT;
}
