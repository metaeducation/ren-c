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
REBINT CT_Bitset(const REBCEL *a, const REBCEL *b, REBINT mode)
{
    if (mode >= 0) {  // !!! keep defer to binary comparisons from R3-Alphae
        DECLARE_LOCAL (atemp);
        DECLARE_LOCAL (btemp);
        Init_Binary(atemp, VAL_BITSET(a));
        Init_Binary(btemp, VAL_BITSET(b));
        return (
            BITS_NOT(VAL_BITSET(a)) == BITS_NOT(VAL_BITSET(b))
            && Compare_Binary_Vals(atemp, btemp) == 0
        );
    }
    return -1;
}


//
//  Make_Bitset: C
//
REBBIN *Make_Bitset(REBCNT num_bits)
{
    REBCNT num_bytes = (num_bits + 7) / 8;
    REBBIN *bin = Make_Binary(num_bytes);
    Clear_Series(bin);
    TERM_BIN_LEN(bin, num_bytes);
    INIT_BITS_NOT(bin, false);
    return bin;
}


//
//  MF_Bitset: C
//
void MF_Bitset(REB_MOLD *mo, const REBCEL *v, bool form)
{
    UNUSED(form); // all bitsets are "molded" at this time

    Pre_Mold(mo, v); // #[bitset! or make bitset!

    REBBIN *s = VAL_BITSET(v);

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
REB_R MAKE_Bitset(
    REBVAL *out,
    enum Reb_Kind kind,
    const REBVAL *opt_parent,
    const REBVAL *arg
){
    assert(kind == REB_BITSET);
    if (opt_parent)
        fail (Error_Bad_Make_Parent(kind, opt_parent));

    REBINT len = Find_Max_Bit(arg);

    // Determine size of bitset. Returns -1 for errors.
    //
    // !!! R3-alpha construction syntax said 0xFFFFFF while the A_MAKE
    // path used 0x0FFFFFFF.  Assume A_MAKE was more likely right.
    //
    if (len < 0 || len > 0x0FFFFFFF)
        fail (arg);

    REBBIN *bin = Make_Bitset(len);
    Init_Bitset(out, Manage_Series(bin));

    if (IS_INTEGER(arg))
        return out; // allocated at a size, no contents.

    if (IS_BINARY(arg)) {
        memcpy(BIN_HEAD(bin), VAL_BIN_AT(arg), len/8 + 1);
        return out;
    }

    Set_Bits(bin, arg, true);
    return out;
}


//
//  TO_Bitset: C
//
REB_R TO_Bitset(REBVAL *out, enum Reb_Kind kind, const REBVAL *arg)
{
    return MAKE_Bitset(out, kind, nullptr, arg);
}


//
//  Find_Max_Bit: C
//
// Return integer number for the maximum bit number defined by
// the value. Used to determine how much space to allocate.
//
REBINT Find_Max_Bit(const RELVAL *val)
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
        REBCHR(const*) up = VAL_STRING_AT(val);
        for (; n < cast(REBINT, VAL_LEN_HEAD(val)); n++) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            if (cast(REBINT, c) > maxi)
                maxi = cast(REBINT, c);
        }
        maxi++;
        break; }

    case REB_BINARY:
        maxi = VAL_LEN_AT(val) * 8 - 1;
        if (maxi < 0) maxi = 0;
        break;

    case REB_BLOCK:
        for (val = VAL_ARRAY_AT(val); NOT_END(val); val++) {
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
bool Check_Bit(REBSER *bset, REBCNT c, bool uncased)
{
    REBCNT i, n = c;
    REBCNT tail = SER_LEN(bset);
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
        flag = did (BIN_HEAD(bset)[i] & (1 << (7 - (n & 7))));

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
void Set_Bit(REBSER *bset, REBCNT n, bool set)
{
    REBCNT i = n >> 3;
    REBCNT tail = SER_LEN(bset);
    REBYTE bit;

    // Expand if not enough room:
    if (i >= tail) {
        if (!set) return; // no need to expand
        Expand_Series(bset, tail, (i - tail) + 1);
        CLEAR(BIN_AT(bset, tail), (i - tail) + 1);
    }

    bit = 1 << (7 - ((n) & 7));
    if (set)
        BIN_HEAD(bset)[i] |= bit;
    else
        BIN_HEAD(bset)[i] &= ~bit;
}


//
//  Set_Bits: C
//
// Set/clear bits indicated by strings and chars and ranges.
//
bool Set_Bits(REBSER *bset, const REBVAL *val, bool set)
{
    if (IS_CHAR(val)) {
        Set_Bit(bset, VAL_CHAR(val), set);
        return true;
    }

    if (IS_INTEGER(val)) {
        REBCNT n = Int32s(val, 0);
        if (n > MAX_BITSET)
            return false;
        Set_Bit(bset, n, set);
        return true;
    }

    if (IS_BINARY(val)) {
        REBCNT i = VAL_INDEX(val);

        REBYTE *bp = VAL_BIN_HEAD(val);
        for (; i != VAL_LEN_HEAD(val); i++)
            Set_Bit(bset, bp[i], set);

        return true;
    }

    if (ANY_STRING(val)) {
        REBCNT i = VAL_INDEX(val);
        REBCHR(const*) up = VAL_STRING_AT(val);
        for (; i < VAL_LEN_HEAD(val); ++i) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            Set_Bit(bset, c, set);
        }

        return true;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    RELVAL *item = VAL_ARRAY_AT(val);

    if (
        NOT_END(item)
        && IS_WORD(item)
        && VAL_WORD_SYM(item) == SYM_NOT
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
                && IS_WORD(item + 1)
                && VAL_WORD_SYM(item + 1) == SYM_HYPHEN
            ){
                item += 2;
                if (IS_CHAR(item)) {
                    REBCNT n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    do {
                        Set_Bit(bset, c, set);
                    } while (c++ < n); // post-increment: test before overflow
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                Set_Bit(bset, c, set);
            break; }

        case REB_INTEGER: {
            REBCNT n = Int32s(KNOWN(item), 0);
            if (n > MAX_BITSET)
                return false;
            if (
                NOT_END(item + 1)
                && IS_WORD(item + 1)
                && VAL_WORD_SYM(item + 1) == SYM_HYPHEN
            ){
                REBUNI c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        Set_Bit(bset, c, set);
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
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
            if (not IS_WORD(item) or VAL_WORD_SYM(item) != SYM_BITS)
                return false;
            item++;
            if (not IS_BINARY(item))
                return false;
            REBCNT n = VAL_LEN_AT(item);
            REBUNI c = SER_LEN(bset);
            if (n >= c) {
                Expand_Series(bset, c, (n - c));
                CLEAR(BIN_AT(bset, c), (n - c));
            }
            memcpy(BIN_HEAD(bset), VAL_BIN_AT(item), n);
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
bool Check_Bits(REBSER *bset, const REBVAL *val, bool uncased)
{
    if (IS_CHAR(val))
        return Check_Bit(bset, VAL_CHAR(val), uncased);

    if (IS_INTEGER(val))
        return Check_Bit(bset, Int32s(val, 0), uncased);

    if (IS_BINARY(val)) {
        REBCNT i = VAL_INDEX(val);
        REBYTE *bp = VAL_BIN_HEAD(val);
        for (; i != VAL_LEN_HEAD(val); ++i)
            if (Check_Bit(bset, bp[i], uncased))
                return true;
        return false;
    }

    if (ANY_STRING(val)) {
        REBCNT i = VAL_INDEX(val);
        REBCHR(const*) up = VAL_STRING_AT(val);
        for (; i != VAL_LEN_HEAD(val); ++i) {
            REBUNI c;
            up = NEXT_CHR(&c, up);
            if (Check_Bit(bset, c, uncased))
                return true;
        }

        return false;
    }

    if (!ANY_ARRAY(val))
        fail (Error_Invalid_Type(VAL_TYPE(val)));

    // Loop through block of bit specs

    RELVAL *item;
    for (item = VAL_ARRAY_AT(val); NOT_END(item); item++) {

        switch (VAL_TYPE(item)) {

        case REB_CHAR: {
            REBUNI c = VAL_CHAR(item);
            if (IS_WORD(item + 1) && VAL_WORD_SYM(item + 1) == SYM_HYPHEN) {
                item += 2;
                if (IS_CHAR(item)) {
                    REBCNT n = VAL_CHAR(item);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
            }
            else
                if (Check_Bit(bset, c, uncased))
                    return true;
            break; }

        case REB_INTEGER: {
            REBCNT n = Int32s(KNOWN(item), 0);
            if (n > 0xffff)
                return false;
            if (IS_WORD(item + 1) && VAL_WORD_SYM(item + 1) == SYM_HYPHEN) {
                REBUNI c = n;
                item += 2;
                if (IS_INTEGER(item)) {
                    n = Int32s(KNOWN(item), 0);
                    if (n < c)
                        fail (Error_Past_End_Raw());
                    for (; c <= n; c++)
                        if (Check_Bit(bset, c, uncased))
                            return true;
                }
                else
                    fail (Error_Bad_Value_Core(item, VAL_SPECIFIER(val)));
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
    const REBVAL *picker,
    const REBVAL *opt_setval
){
    REBSER *ser = VAL_SERIES(pvs->out);

    if (opt_setval == NULL) {
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
void Trim_Tail_Zeros(REBSER *ser)
{
    REBCNT len = SER_LEN(ser);
    REBYTE *bp = BIN_HEAD(ser);

    while (len > 0 && bp[len] == 0)
        len--;

    if (bp[len] != 0)
        len++;

    SET_SERIES_LEN(ser, len);
}


//
//  REBTYPE: C
//
REBTYPE(Bitset)
{
    REBVAL *v = D_ARG(1);
    REBVAL *arg = D_ARGC > 1 ? D_ARG(2) : NULL;

    switch (VAL_WORD_SYM(verb)) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value)); // covered by `v`

        REBSYM property = VAL_WORD_SYM(ARG(property));
        switch (property) {
          case SYM_LENGTH:
            return Init_Integer(v, BIN_LEN(VAL_BITSET(v)) * 8);

          case SYM_TAIL_Q:
            // Necessary to make EMPTY? work:
            return Init_Logic(D_OUT, BIN_LEN(VAL_BITSET(v)) == 0);

          default:
            break;
        }

        break; }

    // Add AND, OR, XOR

      case SYM_FIND: {
        INCLUDE_PARAMS_OF_FIND;

        UNUSED(REF(reverse));  // Deprecated https://forum.rebol.info/t/1126
        UNUSED(REF(last));  // ...a HIJACK in %mezz-legacy errors if used

        UNUSED(PAR(series));
        UNUSED(PAR(pattern));

        if (REF(part) or REF(only) or REF(skip) or REF(tail) or REF(match))
            fail (Error_Bad_Refines_Raw());

        if (not Check_Bits(VAL_BITSET(v), arg, REF(case)))
            return nullptr;
        return Init_True(D_OUT); }

      case SYM_COMPLEMENT:
      case SYM_NEGATE: {
        REBBIN *copy = Copy_Sequence_Core(VAL_BITSET(v), NODE_FLAG_MANAGED);
        INIT_BITS_NOT(copy, not BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(D_OUT, copy); }

      case SYM_APPEND:  // Accepts: #"a" "abc" [1 - 10] [#"a" - #"z"] etc.
      case SYM_INSERT: {
        if (IS_NULLED_OR_BLANK(arg))
            RETURN (v);  // don't fail on read only if it would be a no-op

        FAIL_IF_READ_ONLY(v);

        bool diff;
        if (BITS_NOT(VAL_BITSET(v)))
            diff = false;
        else
            diff = true;

        if (not Set_Bits(VAL_BITSET(v), arg, diff))
            fail (arg);
        RETURN (v); }

      case SYM_REMOVE: {
        INCLUDE_PARAMS_OF_REMOVE;
        UNUSED(PAR(series));  // covered by `v`

        if (not REF(part))
            fail (Error_Missing_Arg_Raw());

        if (not Set_Bits(VAL_BITSET(v), ARG(part), false))
            fail (PAR(part));

        RETURN (v); }

      case SYM_COPY: {
        INCLUDE_PARAMS_OF_COPY;
        UNUSED(PAR(value));

        if (REF(part) or REF(deep) or REF(types))
            fail (Error_Bad_Refines_Raw());

        REBBIN *copy = Copy_Sequence_Core(VAL_BITSET(v), NODE_FLAG_MANAGED);
        INIT_BITS_NOT(copy, BITS_NOT(VAL_BITSET(v)));
        return Init_Bitset(D_OUT, copy); }

      case SYM_CLEAR:
        FAIL_IF_READ_ONLY(v);
        Clear_Series(VAL_BITSET(v));
        RETURN (v);

      case SYM_INTERSECT:
      case SYM_UNION:
      case SYM_DIFFERENCE: {
        if (IS_BITSET(arg)) {
            if (BITS_NOT(VAL_BITSET(arg)))  // !!! see #2365
                fail ("Bitset negation not handled by set operations");
            Init_Binary(arg, VAL_BITSET(arg));
        }
        else if (not IS_BINARY(arg))
            fail (Error_Math_Args(VAL_TYPE(arg), verb));

        if (BITS_NOT(VAL_BITSET(v)))  // !!! see #2365
            fail ("Bitset negation not handled by set operations");

        Init_Binary(v, VAL_BITSET(v));

        REBBIN *bits = Xandor_Binary(verb, v, arg);
        INIT_BITS_NOT(bits, false);
        Trim_Tail_Zeros(bits);
        return Init_Bitset(D_OUT, Manage_Series(bits)); }

      default:
        break;
    }

    return R_UNHANDLED;
}
