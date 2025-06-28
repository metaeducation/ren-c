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
REBINT CT_Bitset(const Element* a, const Element* b, bool strict)
{
    DECLARE_ELEMENT (atemp);
    DECLARE_ELEMENT (btemp);
    Init_Blob(atemp, VAL_BITSET(a));
    Init_Blob(btemp, VAL_BITSET(b));

    if (BITS_NOT(VAL_BITSET(a)) != BITS_NOT(VAL_BITSET(b)))
        return 1;

    return CT_Blob(atemp, btemp, strict);
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Bitset)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Bitset(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(LESSER_Q, Is_Bitset)
{
    INCLUDE_PARAMS_OF_LESSER_Q;

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Bitset(v1, v2, true) == -1);
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


IMPLEMENT_GENERIC(MOLDIFY, Is_Bitset)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form); // all bitsets are "molded" at this time

    Begin_Non_Lexical_Mold(mo, v); // &[bitset!

    const Binary* bset = VAL_BITSET(v);

    if (BITS_NOT(bset))
        Append_Ascii(mo->string, "[not bits ");

    Init_Blob(v, bset);
    Init_Nulled(ARG(FORM));  // form = false
    Bounce bounce = GENERIC_CFUNC(MOLDIFY, Is_Blob)(LEVEL);
    assert(bounce == TRIPWIRE);  // !!! generically it could BOUNCE_CONTINUE...
    UNUSED(bounce);

    if (BITS_NOT(bset))
        Append_Codepoint(mo->string, ']');

    End_Non_Lexical_Mold(mo);

    return TRIPWIRE;
}


IMPLEMENT_GENERIC(MAKE, Is_Bitset)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Cell_Datatype_Type(ARG(TYPE)) == TYPE_BITSET);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    REBINT len = Find_Max_Bit(arg);
    if (len == NOT_FOUND)
        return FAIL(arg);

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

    switch (Type_Of(val)) {

    case TYPE_INTEGER:
        maxi = Int32s(val, 0);
        break;

    case TYPE_TEXT:
    case TYPE_FILE:
    case TYPE_EMAIL:
    case TYPE_URL:
    case TYPE_RUNE:
    case TYPE_TAG: {
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

    case TYPE_BLOB:
        if (Cell_Series_Len_At(val) != 0)
            maxi = Cell_Series_Len_At(val) * 8 - 1;
        break;

    case TYPE_BLOCK: {
        const Element* tail;
        const Element* item = Cell_List_At(&tail, val);
        for (; item != tail; ++item) {
            REBINT n = Find_Max_Bit(item);
            if (n != NOT_FOUND and n > maxi)
                maxi = n;
        }
        //maxi++;
        break; }

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
        if (n >= NUM_UNICODE_CASES)
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
bool Set_Bits(Binary* bset, const Element* val, bool set)
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

    if (Is_Rune(val) or Any_String(val)) {
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
        panic (Error_Invalid_Type_Raw(Datatype_Of(val)));

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
        if (Is_Rune_And_Is_Char(item)) {  // may be #{00} for NUL
            Codepoint c = Rune_Known_Single_Codepoint(item);
            if (
                item + 1 != tail
                && Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
            ){
                item += 2;
                if (Is_Rune_And_Is_Char(item)) {
                    Codepoint c2 = Rune_Known_Single_Codepoint(item);
                    if (c2 < c)
                        panic (Error_Index_Out_Of_Range_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < c2);  // post-increment test BEFORE overflow
                }
                else
                    panic (Error_Bad_Value(item));
            }
            else
                Set_Bit(bset, c, set);
        }
        else switch (Type_Of(item)) {
        case TYPE_RUNE: {
            if (not Is_Rune_And_Is_Char(item)) {  // no special handling for hyphen
                Set_Bits(bset, item, set);
                break;
            }
            break; }

        case TYPE_INTEGER: {
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
                        panic (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    panic (Error_Bad_Value(item));
            }
            else
                Set_Bit(bset, n, set);
            break; }

        case TYPE_BLOB:
        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG:
            Set_Bits(bset, item, set);
            break;

        case TYPE_WORD: {
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
    if (Is_Rune_And_Is_Char(val))
        return Check_Bit(bset, Rune_Known_Single_Codepoint(val), uncased);

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
        panic (Error_Invalid_Type_Raw(Datatype_Of(val)));

    // Loop through block of bit specs

    const Element* tail;
    const Element* item = Cell_List_At(&tail, val);
    for (; item != tail; item++) {

        switch (Type_Of(item)) {

        case TYPE_RUNE: {
            if (not Is_Rune_And_Is_Char(item)) {
                if (Check_Bits(bset, item, uncased))
                    return true;
            }
            Codepoint c = Rune_Known_Single_Codepoint(item);
            if (
                Is_Word(item + 1)
                && Cell_Word_Symbol(item + 1) == CANON(HYPHEN_1)
            ){
                item += 2;
                if (Is_Rune_And_Is_Char(item)) {
                    Codepoint c2 = Rune_Known_Single_Codepoint(item);
                    if (c2 < c)
                        panic (Error_Index_Out_Of_Range_Raw());
                    for (; c <= c2; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    panic (Error_Bad_Value(item));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case TYPE_INTEGER: {
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
                        panic (Error_Index_Out_Of_Range_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    panic (Error_Bad_Value(item));
            }
            else
                if (Check_Bit(bset, n, uncased))
                    return true;
            break; }

        case TYPE_BLOB:
        case TYPE_TEXT:
        case TYPE_FILE:
        case TYPE_EMAIL:
        case TYPE_URL:
        case TYPE_TAG:
//      case TYPE_RUNE:
            if (Check_Bits(bset, item, uncased))
                return true;
            break;

        default:
            panic (Error_Invalid_Type_Raw(Datatype_Of(item)));
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


IMPLEMENT_GENERIC(OLDGENERIC, Is_Bitset)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* v = cast(Element*, ARG_N(1));
    assert(Is_Bitset(v));

    switch (id) {

    // Add AND, OR, XOR

      case SYM_SELECT: {
        INCLUDE_PARAMS_OF_SELECT;
        if (Is_Antiform(ARG(VALUE)))
            return PANIC(ARG(VALUE));

        UNUSED(PARAM(SERIES));  // covered by `v`

        if (Bool_ARG(PART) or Bool_ARG(SKIP) or Bool_ARG(MATCH))
            return PANIC(Error_Bad_Refines_Raw());

        if (not Check_Bits(VAL_BITSET(v), ARG(VALUE), Bool_ARG(CASE)))
            return nullptr;
        return LOGIC(true); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        INCLUDE_PARAMS_OF_APPEND;
        USED(PARAM(SERIES));  // covered by `v`

        if (Is_Undone_Opt_Nulled(ARG(VALUE)))
            return COPY(v);  // don't panic on read only if it would be a no-op
        if (Is_Antiform(ARG(VALUE)))
            return PANIC(PARAM(VALUE));
        const Element* arg = Element_ARG(VALUE);

        if (Bool_ARG(PART) or Bool_ARG(DUP) or Bool_ARG(LINE))
            return PANIC(Error_Bad_Refines_Raw());

        Binary* bset = VAL_BITSET_Ensure_Mutable(v);

        bool diff;
        if (BITS_NOT(VAL_BITSET(v)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(bset, arg, diff))
            return PANIC(arg);
        return COPY(v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PARAM(SERIES));  // covered by `v`

        Binary* bset = VAL_BITSET_Ensure_Mutable(v);

        if (not Bool_ARG(PART))
            return PANIC(Error_Missing_Arg_Raw());

        if (not Set_Bits(bset, Element_ARG(PART), false))
            return PANIC(PARAM(PART));

        return COPY(v); }

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


IMPLEMENT_GENERIC(TWEAK_P, Is_Bitset)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* bset = Element_ARG(LOCATION);

    if (Is_Antiform(ARG(PICKER)))
        return PANIC(PARAM(PICKER));

    const Element* picker = Element_ARG(PICKER);

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        return PANIC(Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    bool bit = Check_Bits(VAL_BITSET(bset), picker, false);

    return DUAL_LIFTED(Init_Logic(OUT, bit));

} handle_poke: { /////////////////////////////////////////////////////////////

    Value* poke = Unliftify_Known_Stable(dual);  // ~null~/~okay~ antiforms

    if (not Is_Logic(poke))
        return PANIC(Error_Bad_Value_Raw(poke));

    bool cond = Cell_Logic(poke);

    Binary* bits = cast(Binary*, VAL_BITSET_Ensure_Mutable(bset));
    if (not Set_Bits(
        bits,
        picker,
        BITS_NOT(bits) ? not cond : cond
    )){
        return PANIC(PARAM(PICKER));
    }
    return NO_WRITEBACK_NEEDED;
}}


IMPLEMENT_GENERIC(COPY, Is_Bitset)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* bset = Element_ARG(VALUE);
    Binary* bits = VAL_BITSET(bset);

    if (Bool_ARG(PART) or Bool_ARG(DEEP))
        return PANIC(Error_Bad_Refines_Raw());

    Binary* copy = cast(Binary*, Copy_Flex_Core(BASE_FLAG_MANAGED, bits));
    INIT_BITS_NOT(copy, BITS_NOT(bits));

    return Init_Bitset(OUT, copy);
}


IMPLEMENT_GENERIC(LENGTH_OF, Is_Bitset)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* bset = Element_ARG(ELEMENT);

    return Init_Integer(OUT, Binary_Len(VAL_BITSET(bset)) * 8);
}


// This is necessary to make EMPTY? work:
//
IMPLEMENT_GENERIC(TAIL_Q, Is_Bitset)
{
    INCLUDE_PARAMS_OF_TAIL_Q;

    Element* bset = Element_ARG(ELEMENT);
    return LOGIC(Binary_Len(VAL_BITSET(bset)) == 0);
}


IMPLEMENT_GENERIC(COMPLEMENT, Is_Bitset)
{
    INCLUDE_PARAMS_OF_COMPLEMENT;

    Element* bset = Element_ARG(VALUE);

    Binary* copy = cast(
        Binary*,
        Copy_Flex_Core(BASE_FLAG_MANAGED, VAL_BITSET(bset))
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

    Element* bset = Element_ARG(VALUE1);
    Element* arg = Element_ARG(VALUE2);

    if (Bool_ARG(SKIP))
        return Error_Bad_Refines_Raw();

    UNUSED(ARG(CASE));

    if (Is_Bitset(arg)) {
        if (BITS_NOT(VAL_BITSET(arg))) {  // !!! see #2365
            return Error_User(
                "Bitset negation not handled by set operations"
            );
        }
        Init_Blob(arg, VAL_BITSET(arg));
    }
    else if (not Is_Blob(arg))
        return Error_Not_Related_Raw(Canon_Symbol(id), Datatype_Of(arg));

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

    return SUCCESS;
}


IMPLEMENT_GENERIC(INTERSECT, Is_Bitset)
{
    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_INTERSECT, LEVEL
    );
    if (e)
        return FAIL(unwrap e);

    Value* processed = rebValue(CANON(BITWISE_AND), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(UNION, Is_Bitset)
{
    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_UNION, LEVEL
    );
    if (e)
        return FAIL(unwrap e);

    Value* processed = rebValue(CANON(BITWISE_OR), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(DIFFERENCE, Is_Bitset)
{
    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_DIFFERENCE, LEVEL
    );
    if (e)
        return FAIL(unwrap e);

    Value* processed = rebValue(CANON(BITWISE_XOR), blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, false);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}


IMPLEMENT_GENERIC(EXCLUDE, Is_Bitset)
{
    bool negated_result = (
        Is_Bitset(ARG_N(1)) and BITS_NOT(VAL_BITSET(ARG_N(1)))
    );

    Element* blob1;
    Element* blob2;
    Option(Error*) e = Blobify_Args_For_Bitset_Arity_2_Set_Operation(
        &blob1, &blob2, SYM_EXCLUDE, LEVEL
    );
    if (e)
        return FAIL(unwrap e);

    const Symbol* operation =   // use UNION semantics if negated
        negated_result ? CANON(BITWISE_OR) : CANON(BITWISE_AND_NOT);

    Value* processed = rebValue(operation, blob1, blob2);

    Binary* bits_out = Cell_Binary_Known_Mutable(processed);
    rebRelease(processed);

    INIT_BITS_NOT(bits_out, negated_result);
    Trim_Tail_Zeros(bits_out);
    return Init_Bitset(OUT, bits_out);
}
