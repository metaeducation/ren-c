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
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Bitset: C
//
// !!! Bitset comparison including the NOT is somewhat nebulous.  If you have
// a bitset of 8 bits length as 11111111, is it equal to the negation of
// a bitset of 8 bits length of 00000000 or not?  For the moment, this does
// not attempt to answer any existential questions--as comparisons in R3-Alpha
// need significant review.
//
REBINT CT_Bitset(const Cell* a, const Cell* b, bool strict)
{
    DECLARE_LOCAL (atemp);
    DECLARE_LOCAL (btemp);
    Init_Binary(atemp, VAL_BITSET(a));
    Init_Binary(btemp, VAL_BITSET(b));

    if (BITS_NOT(VAL_BITSET(a)) != BITS_NOT(VAL_BITSET(b)))
        return 1;

    return CT_Binary(atemp, btemp, strict);
}


//
//  Make_Bitset: C
//
Binary* Make_Bitset(REBLEN num_bits)
{
    REBLEN num_bytes = (num_bits + 7) / 8;
    Binary* bin = Make_Binary(num_bytes);
    Clear_Series(bin);
    Term_Binary_Len(bin, num_bytes);
    INIT_BITS_NOT(bin, false);
    return bin;
}


//
//  MF_Bitset: C
//
void MF_Bitset(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Pre_Mold(mo, v); // #[bitset! or make bitset!

    const Binary* s = VAL_BITSET(v);

    if (BITS_NOT(s))
        Append_Ascii(mo->series, "[not bits ");

    DECLARE_LOCAL (binary);
    Init_Binary(binary, s);
    MF_Binary(mo, binary, false); // false = mold, don't form

    if (BITS_NOT(s))
        Append_Codepoint(mo->series, ']');

    End_Mold(mo);
}


//
//  MAKE_Bitset: C
//
Bounce MAKE_Bitset(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    assert(kind == REB_BITSET);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap(parent)));

    REBINT len = Find_Max_Bit(arg);
    if (len == NOT_FOUND)
        return RAISE(arg);

    Binary* bin = Make_Bitset(cast(REBLEN, len));
    Manage_Series(bin);
    Init_Bitset(OUT, bin);

    if (Is_Integer(arg))
        return OUT; // allocated at a size, no contents.

    if (Is_Binary(arg)) {
        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, arg);
        memcpy(Binary_Head(bin), at, (size / 8) + 1);
        return OUT;
    }

    Set_Bits(bin, arg, true);
    return OUT;
}


//
//  TO_Bitset: C
//
Bounce TO_Bitset(Level* level_, Kind kind, const Value* arg)
{
    return MAKE_Bitset(level_, kind, nullptr, arg);
}


//
//  Find_Max_Bit: C
//
// Return integer number for the maximum bit number defined by
// the value. Used to determine how much space to allocate.
//
REBINT Find_Max_Bit(const Value* val)
{
    REBLEN maxi = 0;

    switch (VAL_TYPE(val)) {

    case REB_INTEGER:
        maxi = Int32s(val, 0);
        break;

    case REB_TEXT:
    case REB_FILE:
    case REB_EMAIL:
    case REB_URL:
    case REB_ISSUE:
    case REB_TAG: {
        REBLEN len;
        Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = Utf8_Next(&c, up);
            if (c > maxi)
                maxi = cast(REBINT, c);
        }
        maxi++;
        break; }

    case REB_BINARY:
        if (Cell_Series_Len_At(val) != 0)
            maxi = Cell_Series_Len_At(val) * 8 - 1;
        break;

    case REB_BLOCK: {
        const Element* tail;
        const Element* item = Cell_Array_At(&tail, val);
        for (; item != tail; ++item) {
            REBINT n = Find_Max_Bit(item);
            if (n != NOT_FOUND and cast(REBLEN, n) > maxi)
                maxi = cast(REBLEN, n);
        }
        //maxi++;
        break; }

    case REB_BLANK:
        maxi = 0;
        break;

    default:
        return NOT_FOUND;
    }

    return maxi;
}


//
//  Check_Bit: C
//
// Check bit indicated. Returns true if set.
// If uncased is true, try to match either upper or lower case.
//
bool Check_Bit(const Binary* bset, REBLEN c, bool uncased)
{
    REBLEN i, n = c;
    REBLEN tail = Binary_Len(bset);
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
        memset(Binary_At(bset, tail), 0, (i - tail) + 1);
        Term_Series_If_Necessary(bset);
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
    if (Is_Integer(val)) {
        REBLEN n = Int32s(val, 0);
        if (n > MAX_BITSET)
            return false;
        Set_Bit(bset, n, set);
        return true;
    }

    if (Is_Binary(val)) {
        REBLEN i = VAL_INDEX(val);

        const Byte* bp = Binary_Head(Cell_Binary(val));
        for (; i != Cell_Series_Len_Head(val); i++)
            Set_Bit(bset, bp[i], set);

        return true;
    }

    if (Is_Issue(val) or Any_String(val)) {
        REBLEN len;
        Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = Utf8_Next(&c, up);
            Set_Bit(bset, c, set);
        }

        return true;
    }

    if (!Any_Array(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    const Element* tail;
    const Element* item = Cell_Array_At(&tail, val);

    if (
        item != tail
        && Is_Word(item)
        && Cell_Word_Id(item) == SYM_NOT_1  // see TO-C-NAME
    ){
        INIT_BITS_NOT(bset, true);
        item++;
    }

    // Loop through block of bit specs:

    for (; item != tail; item++) {

        switch (VAL_TYPE(item)) {
        case REB_ISSUE: {
            if (not IS_CHAR(item)) {  // no special handling for hyphen
                Set_Bits(bset, item, set);
                break;
            }
            Codepoint c = Cell_Codepoint(item);
            if (
                item + 1 != tail
                && Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == Canon(HYPHEN_1)
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    Codepoint c2 = Cell_Codepoint(item);
                    if (c2 < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < c2);  // post-increment test BEFORE overflow
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                Set_Bit(bset, c, set);
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(item, 0);
            if (n > MAX_BITSET)
                return false;
            if (
                item + 1 != tail
                && Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == Canon(HYPHEN_1)
            ){
                Codepoint c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(item, 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    fail (Error_Bad_Value(item));
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
            Set_Bits(bset, item, set);
            break;

        case REB_WORD: {
            // Special: BITS #{000...}
            if (not Is_Word(item) or Cell_Word_Id(item) != SYM_BITS)
                return false;
            item++;
            if (not Is_Binary(item))
                return false;

            Size n;
            const Byte* at = Cell_Binary_Size_At(&n, item);

            Codepoint c = Binary_Len(bset);
            if (n >= c) {
                Expand_Series(bset, c, (n - c));
                memset(Binary_At(bset, c), 0, (n - c));
            }
            memcpy(Binary_Head(bset), at, n);
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
bool Check_Bits(const Binary* bset, const Value* val, bool uncased)
{
    if (IS_CHAR(val))
        return Check_Bit(bset, Cell_Codepoint(val), uncased);

    if (Is_Integer(val))
        return Check_Bit(bset, Int32s(val, 0), uncased);

    if (Is_Binary(val)) {
        REBLEN i = VAL_INDEX(val);
        const Byte* bp = Binary_Head(Cell_Binary(val));
        for (; i != Cell_Series_Len_Head(val); ++i)
            if (Check_Bit(bset, bp[i], uncased))
                return true;
        return false;
    }

    if (Any_String(val)) {
        REBLEN len;
        Utf8(const*) up = Cell_Utf8_Len_Size_At(&len, nullptr, val);
        for (; len > 0; --len) {
            Codepoint c;
            up = Utf8_Next(&c, up);
            if (Check_Bit(bset, c, uncased))
                return true;
        }

        return false;
    }

    if (!Any_Array(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    const Element* tail;
    const Element* item = Cell_Array_At(&tail, val);
    for (; item != tail; item++) {

        switch (VAL_TYPE(item)) {

        case REB_ISSUE: {
            if (not IS_CHAR(item)) {
                if (Check_Bits(bset, item, uncased))
                    return true;
            }
            Codepoint c = Cell_Codepoint(item);
            if (
                Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == Canon(HYPHEN_1)
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    Codepoint c2 = Cell_Codepoint(item);
                    if (c2 < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= c2; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value(item));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(item, 0);
            if (n > 0xffff)
                return false;
            if (
                Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == Canon(HYPHEN_1)
            ){
                Codepoint c = n;
                item += 2;
                if (Is_Integer(item)) {
                    n = Int32s(item, 0);
                    if (n < c)
                        fail (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value(item));
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
            if (Check_Bits(bset, item, uncased))
                return true;
            break;

        default:
            fail (Error_Invalid_Type(VAL_TYPE(item)));
        }
    }
    return false;
}


//
//  Trim_Tail_Zeros: C
//
// Remove extra zero bytes from end of byte string.
//
void Trim_Tail_Zeros(Binary* ser)
{
    REBLEN len = Binary_Len(ser);
    Byte* bp = Binary_Head(ser);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    Set_Series_Len(ser, len);
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    Value* v = D_ARG(1);

    Option(SymId) sym = Symbol_Id(verb);
    switch (sym) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        bool bit = Check_Bits(VAL_BITSET(v), picker, false);

        return bit ? Init_True(OUT) : Init_Nulled(OUT); }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);

        Value* setval = ARG(value);

        Binary* bset = cast(Binary*, VAL_BITSET_Ensure_Mutable(v));
        if (not Set_Bits(
            bset,
            picker,
            BITS_NOT(bset) ? Is_Falsey(setval) : Is_Truthy(setval)
        )){
            fail (PARAM(picker));
        }
        return nullptr; }


      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by `v`

        Option(SymId) property = Cell_Word_Id(ARG(property));
        switch (property) {
          case SYM_LENGTH:
            return Init_Integer(v, Binary_Len(VAL_BITSET(v)) * 8);

          case SYM_TAIL_Q:
            // Necessary to make EMPTY? work:
            return Init_Logic(OUT, Binary_Len(VAL_BITSET(v)) == 0);

          default:
            break;
        }

        break; }

    // Add AND, OR, XOR

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        if (Is_Antiform(ARG(value)))
            fail (ARG(value));

        UNUSED(PARAM(series));  // covered by `v`
        UNUSED(PARAM(tail));  // no feature for tail output

        if (REF(part) or REF(skip) or REF(match))
            fail (Error_Bad_Refines_Raw());

        if (not Check_Bits(VAL_BITSET(v), ARG(value), REF(case)))
            return nullptr;
        return Init_True(OUT); }

      case SYM_COMPLEMENT: {
        Binary* copy = cast(
            Binary*,
            Copy_Series_Core(VAL_BITSET(v), NODE_FLAG_MANAGED)
        );
        INIT_BITS_NOT(copy, not BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(OUT, copy); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        Value* arg = D_ARG(2);
        if (Is_Void(arg))
            return COPY(v);  // don't fail on read only if it would be a no-op

        if (Is_Antiform(arg))
            fail (arg);

        Binary* bin = VAL_BITSET_Ensure_Mutable(v);

        bool diff;
        if (BITS_NOT(VAL_BITSET(v)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(bin, arg, diff))
            fail (arg);
        return COPY(v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(series));  // covered by `v`

        Binary* bin = VAL_BITSET_Ensure_Mutable(v);

        if (not REF(part))
            fail (Error_Missing_Arg_Raw());

        if (not Set_Bits(bin, ARG(part), false))
            fail (PARAM(part));

        return COPY(v); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        if (REF(part) or REF(deep))
            fail (Error_Bad_Refines_Raw());

        Binary* copy = cast(
            Binary*,
            Copy_Series_Core(VAL_BITSET(v), NODE_FLAG_MANAGED)
        );
        INIT_BITS_NOT(copy, BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(OUT, copy); }

      case SYM_CLEAR: {
        Binary* bin = VAL_BITSET_Ensure_Mutable(v);
        INIT_BITS_NOT(bin, false);
        Clear_Series(bin);
        return COPY(v); }

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE:
      case SYM_EXCLUDE: {
        Value* arg = D_ARG(2);
        if (Is_Bitset(arg)) {
            if (BITS_NOT(VAL_BITSET(arg))) {  // !!! see #2365
                fail ("Bitset negation not handled by set operations");
            }
            const Binary* bin = VAL_BITSET(arg);
            Init_Binary(arg, bin);
        }
        else if (not Is_Binary(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));

        bool negated_result = false;

        if (BITS_NOT(VAL_BITSET(v))) {  // !!! see #2365
            //
            // !!! Narrowly handle the case of exclusion from a negated bitset
            // as simply unioning, because %pdf-maker.r uses this.  General
            // answer is on the Roaring Bitsets branch--this R3 stuff is junk.
            //
            if (sym == SYM_EXCLUDE) {
                negated_result = true;
                sym = SYM_UNION;
            }
            else
                fail ("Bitset negation not handled by (most) set operations");
        }

        const Binary* bin = VAL_BITSET(v);
        Init_Binary(v, bin);

        // !!! Until the replacement implementation with Roaring Bitmaps, the
        // bitset is based on a BINARY!.  Reuse the code on the generated
        // proxy values.
        //
        Value* action;
        switch (sym) {
          case SYM_INTERSECT:
            action = rebValue("unrun :bitwise-and");
            break;

          case SYM_UNION:
            action = rebValue("unrun :bitwise-or");
            break;

          case SYM_DIFFERENCE:
            action = rebValue("unrun :bitwise-xor");
            break;

          case SYM_EXCLUDE:
            action = rebValue("unrun :bitwise-and-not");
            break;

          default:
            panic (nullptr);
        }

        Value* processed = rebValue(rebR(action), rebQ(v), rebQ(arg));

        Binary* bits = Cell_Binary_Known_Mutable(processed);
        rebRelease(processed);

        INIT_BITS_NOT(bits, negated_result);
        Trim_Tail_Zeros(bits);
        return Init_Bitset(OUT, bits); }

      default:
        break;
    }

    fail (UNHANDLED);
}
