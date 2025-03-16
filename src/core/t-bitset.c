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
    DECLARE_ATOM (atemp);
    DECLARE_ATOM (btemp);
    Init_Blob(atemp, VAL_BITSET(a));
    Init_Blob(btemp, VAL_BITSET(b));

    if (BITS_NOT(VAL_BITSET(a)) != BITS_NOT(VAL_BITSET(b)))
        return 1;

    return CT_Blob(atemp, btemp, strict);
}


IMPLEMENT_GENERIC(equal_q, bitset)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;

    return LOGIC(CT_Bitset(ARG(value1), ARG(value2), REF(strict)) == 0);
}


IMPLEMENT_GENERIC(lesser_q, bitset)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    return LOGIC(CT_Bitset(ARG(value1), ARG(value2), true) == -1);
}


//
//  Make_Bitset: C
//
Binary* Make_Bitset(REBLEN num_bits)
{
    REBLEN num_bytes = (num_bits + 7) / 8;
    Binary* bset = Make_Binary(num_bytes);
    Clear_Flex(bset);
    Term_Binary_Len(bset, num_bytes);
    INIT_BITS_NOT(bset, false);
    return bset;
}


IMPLEMENT_GENERIC(moldify, bitset)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(element);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(molder));
    bool form = REF(form);

    UNUSED(form); // all bitsets are "molded" at this time

    Begin_Non_Lexical_Mold(mo, v); // #[bitset! or make bitset!

    const Binary* bset = VAL_BITSET(v);

    if (BITS_NOT(bset))
        Append_Ascii(mo->string, "[not bits ");

    Init_Blob(v, bset);
    Init_Nulled(ARG(form));  // form = false
    Bounce bounce = GENERIC_CFUNC(moldify, blob)(LEVEL);
    assert(bounce == NOTHING);  // !!! generically it could BOUNCE_CONTINUE...
    UNUSED(bounce);

    if (BITS_NOT(bset))
        Append_Codepoint(mo->string, ']');

    End_Non_Lexical_Mold(mo);

    return NOTHING;
}


IMPLEMENT_GENERIC(make, bitset)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(VAL_TYPE_KIND(ARG(type)) == REB_BITSET);
    UNUSED(ARG(type));

    Element* arg = Element_ARG(def);

    REBINT len = Find_Max_Bit(arg);
    if (len == NOT_FOUND)
        return RAISE(arg);

    Binary* bset = Make_Bitset(len);
    Manage_Flex(bset);
    Init_Bitset(OUT, bset);

    if (Is_Integer(arg))
        return OUT; // allocated at a size, no contents.

    if (Is_Blob(arg)) {  // size accounted for by Find_Max_Bit()
        const Byte* at = Cell_Blob_Size_At(nullptr, arg);
        memcpy(Binary_Head(bset), at, (len / 8) + 1);
        return OUT;
    }

    Set_Bits(bset, arg, true);
    return OUT;
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
            if (Cast_Signed(c) > maxi)
                maxi = c;
        }
        maxi++;
        break; }

    case REB_BLOB:
        if (Cell_Series_Len_At(val) != 0)
            maxi = Cell_Series_Len_At(val) * 8 - 1;
        break;

    case REB_BLOCK: {
        const Element* tail;
        const Element* item = Cell_List_At(&tail, val);
        for (; item != tail; ++item) {
            REBINT n = Find_Max_Bit(item);
            if (n != NOT_FOUND and n > maxi)
                maxi = n;
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
        Expand_Flex(bset, tail, (i - tail) + 1);
        memset(Binary_At(bset, tail), 0, (i - tail) + 1);
        Term_Flex_If_Necessary(bset);
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

    if (Is_Blob(val)) {
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

    if (not Is_Block(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    const Element* tail;
    const Element* item = Cell_List_At(&tail, val);

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
        if (IS_CHAR(item)) {  // may be #{00} for NUL
            Codepoint c = Cell_Codepoint(item);
            if (
                item + 1 != tail
                && Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
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
        }
        else switch (VAL_TYPE(item)) {
        case REB_ISSUE: {
            if (not IS_CHAR(item)) {  // no special handling for hyphen
                Set_Bits(bset, item, set);
                break;
            }
            break; }

        case REB_INTEGER: {
            REBLEN n = Int32s(item, 0);
            if (n > MAX_BITSET)
                return false;
            if (
                item + 1 != tail
                && Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
            ){
                REBINT c = n;
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

        case REB_BLOB:
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
            if (not Is_Blob(item))
                return false;

            Size n;
            const Byte* at = Cell_Blob_Size_At(&n, item);

            Codepoint c = Binary_Len(bset);
            if (n >= Cast_Signed(c)) {
                Expand_Flex(bset, c, (n - c));
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

    if (Is_Blob(val)) {
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

    if (!Any_List(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    const Element* tail;
    const Element* item = Cell_List_At(&tail, val);
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
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
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
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
            ){
                REBINT c = n;
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

        case REB_BLOB:
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
void Trim_Tail_Zeros(Binary* bin)
{
    REBLEN len = Binary_Len(bin);
    Byte* bp = Binary_Head(bin);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    Set_Flex_Len(bin, len);
}


IMPLEMENT_GENERIC(oldgeneric, bitset)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*, ARG_N(1));
    assert(Is_Bitset(v));

    switch (id) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        bool bit = Check_Bits(VAL_BITSET(v), picker, false);

        return Init_Logic(OUT, bit); }

    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE: {
        INCLUDE_PARAMS_OF_POKE;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);

        Value* setval = ARG(value);

        Binary* bset = cast(Binary*, VAL_BITSET_Ensure_Mutable(v));
        if (not Set_Bits(
            bset,
            picker,
            BITS_NOT(bset) ? Is_Inhibitor(setval) : Is_Trigger(setval)
        )){
            return FAIL(PARAM(picker));
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
            return FAIL(ARG(value));

        UNUSED(PARAM(series));  // covered by `v`

        if (REF(part) or REF(skip) or REF(match))
            return FAIL(Error_Bad_Refines_Raw());

        if (not Check_Bits(VAL_BITSET(v), ARG(value), REF(case)))
            return nullptr;
        return Init_Logic(OUT, true); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        Value* arg = ARG_N(2);
        if (Is_Void(arg))
            return COPY(v);  // don't fail on read only if it would be a no-op

        if (Is_Antiform(arg))
            return FAIL(arg);

        Binary* bset = VAL_BITSET_Ensure_Mutable(v);

        bool diff;
        if (BITS_NOT(VAL_BITSET(v)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(bset, arg, diff))
            return FAIL(arg);
        return COPY(v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(series));  // covered by `v`

        Binary* bset = VAL_BITSET_Ensure_Mutable(v);

        if (not REF(part))
            return FAIL(Error_Missing_Arg_Raw());

        if (not Set_Bits(bset, ARG(part), false))
            return FAIL(PARAM(part));

        return COPY(v); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PARAM(value));

        if (REF(part) or REF(deep))
            return FAIL(Error_Bad_Refines_Raw());

        Binary* copy = cast(
            Binary*,
            Copy_Flex_Core(NODE_FLAG_MANAGED, VAL_BITSET(v))
        );
        INIT_BITS_NOT(copy, BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(OUT, copy); }

      case SYM_CLEAR: {
        Binary* bset = VAL_BITSET_Ensure_Mutable(v);
        INIT_BITS_NOT(bset, false);
        Clear_Flex(bset);
        return COPY(v); }

      default:
        break;
    }

    return UNHANDLED;
}


IMPLEMENT_GENERIC(complement, bitset)
{
    INCLUDE_PARAMS_OF_COMPLEMENT;

    Element* bset = Element_ARG(value);

    Binary* copy = cast(
        Binary*,
        Copy_Flex_Core(NODE_FLAG_MANAGED, VAL_BITSET(bset))
    );
    INIT_BITS_NOT(copy, not BITS_NOT(VAL_BITSET(bset)));
    return Init_Bitset(OUT, copy);
}


// !!! Until Roaring Bitmaps replacement, bitset is just a BLOB!, and reuses
// the implementation of bitwise operators on BLOB! for set operations.
//
Option(Error*) Blobify_Args_For_Bitset_Arity_2_Set_Operation(
    Sink(Element*) blob1,
    Sink(Element*) blob2,
    SymId id,
    Level* level_
){
    INCLUDE_PARAMS_OF_INTERSECT;  // assume arg compatibility

    Element* bset = Element_ARG(value1);
    Element* arg = Element_ARG(value2);

    if (REF(skip))
        return Error_Bad_Refines_Raw();

    if (Is_Bitset(arg)) {
        if (BITS_NOT(VAL_BITSET(arg))) {  // !!! see #2365
            return Error_User(
                "Bitset negation not handled by set operations"
            );
        }
        Init_Blob(arg, VAL_BITSET(arg));
    }
    else if (not Is_Blob(arg))
        return Error_Math_Args(VAL_TYPE(arg), Canon_Symbol(id));

    if (BITS_NOT(VAL_BITSET(bset))) {  // !!! see #2365
        //
        // !!! Narrowly handle the case of exclusion from a negated bitset
        // as simply unioning, because %pdf-maker.r uses this.  General
        // answer is on the Roaring Bitsets branch--this R3 stuff is junk.
        //
        if (id != SYM_EXCLUDE)
            return Error_User(
                "Bitset negation not handled by (most) set operations"
            );
    }

    Init_Blob(bset, VAL_BITSET(bset));

    *blob1 = bset;
    *blob2 = arg;

    return nullptr;
}


IMPLEMENT_GENERIC(intersect, bitset)
{
    INCLUDE_PARAMS_OF_INTERSECT;

    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_INTERSECT, LEVEL
    );
    if (e)
        return RAISE(e);

    Value* processed = rebValue(CANON(BITWISE_AND), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(union, bitset)
{
    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_UNION, LEVEL
    );
    if (e)
        return RAISE(e);

    Value* processed = rebValue(CANON(BITWISE_OR), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(difference, bitset)
{
    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_DIFFERENCE, LEVEL
    );
    if (e)
        return RAISE(e);

    Value* processed = rebValue(CANON(BITWISE_XOR), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(exclude, bitset)
{
    INCLUDE_PARAMS_OF_EXCLUDE;

    bool negated_result = (
        Is_Bitset(ARG_N(1)) and BITS_NOT(VAL_BITSET(ARG_N(1)))
    );

    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_EXCLUDE, LEVEL
    );
    if (e)
        return RAISE(e);

    const Symbol* operation =   // use UNION semantics if negated
        negated_result ? CANON(BITWISE_OR) : CANON(BITWISE_AND_NOT);

    Value* processed = rebValue(operation, blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, negated_result);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}
