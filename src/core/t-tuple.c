//
//  File: %t-tuple.c
//  Summary: "tuple datatype"
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
//  MAKE_Sequence: C
//
// !!! There was no original TO TUPLE! code besides calling this MAKE, so
// PATH!'s TO ANY-PATH? was used for TO ANY-TUPLE?.  But this contains some
// unique behavior which might be interesting for numeric MAKEs.
//
Bounce MAKE_Sequence(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    if (kind == REB_TEXT or Any_Path_Kind(kind))  // delegate for now
        return MAKE_Path(level_, kind, parent, arg);

    assert(kind == REB_TUPLE);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap parent));

    if (Is_Tuple(arg))
        return Copy_Cell(OUT, arg);

    // !!! Net lookup parses IP addresses out of `tcp://93.184.216.34` or
    // similar URL!s.  In Rebol3 these captures come back the same type
    // as the input instead of as STRING!, which was a latent bug in the
    // network code of the 12-Dec-2012 release:
    //
    // https://github.com/rebol/rebol/blob/master/src/mezz/sys-ports.r#L110
    //
    // All attempts to convert a URL!-flavored IP address failed.  Taking
    // URL! here fixes it, though there are still open questions.
    //
    if (Is_Url(arg)) {
        Size len;
        const Byte* cp
            = Analyze_String_For_Scan(&len, arg, MAX_SCAN_TUPLE);

        if (len == 0)
            fail (arg);

        const Byte* ep;
        REBLEN size = 1;
        REBINT n;
        for (n = cast(REBINT, len), ep = cp; n > 0; n--, ep++) { // count '.'
            if (*ep == '.')
                ++size;
        }

        if (size > MAX_TUPLE)
            fail (arg);

        if (size < 3)
            size = 3;

        Byte buf[MAX_TUPLE];

        Byte* tp = buf;
        for (ep = cp; len > cast(REBLEN, ep - cp); ++ep) {
            ep = Grab_Int(ep, &n);
            if (n < 0 || n > 255)
                return RAISE(arg);

            *tp++ = cast(Byte, n);
            if (*ep != '.')
                break;
        }

        if (len > cast(REBLEN, ep - cp))
            return RAISE(arg);

        return Init_Tuple_Bytes(OUT, buf, size);
    }

    if (Any_List(arg)) {
        REBLEN len = 0;
        REBINT n;

        const Element* tail;
        const Element* item = Cell_List_At(&tail, arg);

        Byte buf[MAX_TUPLE];
        Byte* vp = buf;

        for (; item != tail; ++item, ++vp, ++len) {
            if (len >= MAX_TUPLE)
                goto bad_make;
            if (Is_Integer(item)) {
                n = Int32(item);
            }
            else if (IS_CHAR(item)) {
                n = Cell_Codepoint(item);
            }
            else
                goto bad_make;

            if (n > 255 || n < 0)
                goto bad_make;
            *vp = n;
        }

        return Init_Tuple_Bytes(OUT, buf, len);
    }

    REBLEN alen;

    if (Is_Issue(arg)) {
        Byte buf[MAX_TUPLE];
        Byte* vp = buf;

        const String* spelling = Cell_String(arg);
        const Byte* ap = String_Head(spelling);
        size_t size = String_Size(spelling); // UTF-8 len
        if (size & 1)
            fail (arg); // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            fail (arg); // valid even for UTF-8
        for (alen = 0; alen < size; alen++) {
            Byte decoded;
            if ((ap = Scan_Hex2(&decoded, ap)) == NULL)
                fail (arg);
            *vp++ = decoded;
        }
        Init_Tuple_Bytes(OUT, buf, size);
    }
    else if (Is_Binary(arg)) {
        Size size;
        const Byte* at = Cell_Binary_Size_At(&size, arg);
        if (size > MAX_TUPLE)
            size = MAX_TUPLE;
        Init_Tuple_Bytes(OUT, at, size);
    }
    else
        return RAISE(arg);

    return OUT;

  bad_make:

    return RAISE(Error_Bad_Make(REB_TUPLE, arg));
}


//
//  REBTYPE: C
//
// !!! This is shared code between TUPLE! and PATH!.  The math operations
// predate the unification, and are here to document what expected operations
// were...though they should use the method of PAIR! to generate frames for
// each operation and run them against each other.
//
REBTYPE(Sequence)
{
    Value* sequence = D_ARG(1);

    // !!! We get bytes for the sequence even if it's not a legitimate byte
    // tuple (or path), for compatibility in the below code for when it is.
    // This is a work in progress, just to try to get to booting.
    //
    Byte buf[MAX_TUPLE];
    Length len = Cell_Sequence_Len(sequence);
    if (len > MAX_TUPLE)
        len = MAX_TUPLE;
    bool all_byte_sized_ints = Did_Get_Sequence_Bytes(buf, sequence, len);
    UNUSED(all_byte_sized_ints);
    Byte* vp = buf;

    Option(SymId) id = Symbol_Id(verb);

    // !!! This used to depend on "IS_BINARY_ACT", a concept that does not
    // exist any longer with symbol-based action dispatch.  Patch with more
    // elegant mechanism.
    //
    if (
        id == SYM_ADD
        or id == SYM_SUBTRACT
        or id == SYM_MULTIPLY
        or id == SYM_DIVIDE
        or id == SYM_REMAINDER
        or id == SYM_BITWISE_AND
        or id == SYM_BITWISE_OR
        or id == SYM_BITWISE_XOR
        or id == SYM_BITWISE_AND_NOT
    ){
        assert(vp);

        Byte abuf[MAX_TUPLE];
        const Byte* ap;
        REBLEN alen;
        REBINT a;
        REBDEC dec;

        Value* arg = D_ARG(2);

        if (Is_Integer(arg)) {
            dec = -207.6382; // unused but avoid maybe uninitialized warning
            a = VAL_INT32(arg);
            ap = nullptr;
        }
        else if (Is_Decimal(arg) || Is_Percent(arg)) {
            dec = VAL_DECIMAL(arg);
            a = cast(REBINT, dec);
            ap = nullptr;
        }
        else if (Is_Tuple(arg)) {
            dec = -251.8517; // unused but avoid maybe uninitialized warning
            alen = Cell_Sequence_Len(arg);
            Get_Tuple_Bytes(abuf, arg, alen);
            ap = abuf;
            if (len < alen)
                len = alen;
            a = 646699; // unused but avoid maybe uninitialized warning
        }
        else
            fail (Error_Math_Args(REB_TUPLE, verb));

        REBLEN temp = len;
        for (; temp > 0; --temp, ++vp) {
            REBINT v = *vp;
            if (ap)
                a = (REBINT) *ap++;

            switch (id) {
            case SYM_ADD: v += a; break;

            case SYM_SUBTRACT: v -= a; break;

            case SYM_MULTIPLY:
                if (Is_Decimal(arg) || Is_Percent(arg))
                    v = cast(REBINT, v * dec);
                else
                    v *= a;
                break;

            case SYM_DIVIDE:
                if (Is_Decimal(arg) || Is_Percent(arg)) {
                    if (dec == 0.0)
                        fail (Error_Zero_Divide_Raw());

                    // !!! After moving all the ROUND service routines to
                    // talk directly to ROUND frames, cases like this that
                    // don't have round frames need one.  Can't run:
                    //
                    //    v = cast(REBINT, Round_Dec(v / dec, 0, 1.0));
                    //
                    // The easiest way to do it is to call ROUND.  Methods for
                    // this are being improved all the time, so the slowness
                    // of scanning and binding is not too important.  (The
                    // TUPLE! code is all going to be replaced... so just
                    // consider this an API test.)
                    //
                    v = rebUnboxInteger(
                        "to integer! round divide", rebI(v), arg
                    );
                }
                else {
                    if (a == 0)
                        fail (Error_Zero_Divide_Raw());
                    v /= a;
                }
                break;

            case SYM_REMAINDER:
                if (a == 0)
                    fail (Error_Zero_Divide_Raw());
                v %= a;
                break;

            case SYM_BITWISE_AND:
                v &= a;
                break;

            case SYM_BITWISE_OR:
                v |= a;
                break;

            case SYM_BITWISE_XOR:
                v ^= a;
                break;

            case SYM_BITWISE_AND_NOT:
                v &= ~a;
                break;

            default:
                fail (UNHANDLED);
            }

            if (v > 255)
                v = 255;
            else if (v < 0)
                v = 0;
            *vp = cast(Byte, v);
        }
        return Init_Tuple_Bytes(OUT, buf, len);
    }

    // !!!! merge with SWITCH below !!!
    if (id == SYM_BITWISE_NOT) {
        REBLEN temp = len;
        for (; temp > 0; --temp, vp++)
            *vp = cast(Byte, ~*vp);
        return Init_Tuple_Bytes(OUT, buf, len);
    }
    if (id == SYM_RANDOM) {
        INCLUDE_PARAMS_OF_RANDOM;

        UNUSED(PARAM(value));

        if (REF(only))
            fail (Error_Bad_Refines_Raw());

        if (REF(seed))
            fail (Error_Bad_Refines_Raw());
        for (; len > 0; len--, vp++) {
            if (*vp)
                *vp = cast(Byte, Random_Int(REF(secure)) % (1 + *vp));
        }
        return Init_Tuple_Bytes(OUT, buf, len);
    }

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        switch (Cell_Word_Id(ARG(property))) {
          case SYM_LENGTH:
            return Init_Integer(OUT, Cell_Sequence_Len(sequence));

          case SYM_INDEX:
            return RAISE(Error_Type_Has_No_Index_Raw(Type_Of(sequence)));

          default:
            break;
        }
        break; }

        // ANY-SEQUENCE? is immutable, so a shallow copy should be a no-op,
        // but it should be cheap for any similarly marked array.  Also, a
        // /DEEP copy of a path may copy groups that are mutable.
        //
      case SYM_COPY: {
        if (not Any_Listlike(sequence))
            return Copy_Cell(level_->out, sequence);

        Heart heart = Cell_Heart_Ensure_Noquote(sequence);
        HEART_BYTE(sequence) = REB_BLOCK;

        Atom* r = Atom_From_Bounce(T_List(level_, verb));
        assert(Cell_Heart(r) == REB_BLOCK);

        if (r != OUT)
            Copy_Cell(OUT, r);

        Freeze_Array_Shallow(Cell_Array_Known_Mutable(OUT));
        HEART_BYTE(OUT) = heart;
        return OUT; }

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);

        REBINT n;
        if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
            n = Int32(picker) - 1;
        }
        else
            fail (picker);

        if (n < 0 or n >= cast(REBINT, Cell_Sequence_Len(sequence)))
            return RAISE(Error_Bad_Pick_Raw(picker));

        Copy_Sequence_At(OUT, sequence, n);
        return OUT; }

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;

        UNUSED(PARAM(series));

        REBLEN temp = len;

        if (REF(part)) {
            REBLEN part = Get_Num_From_Arg(ARG(part));
            temp = MIN(part, Cell_Sequence_Len(sequence));
        }
        if (len > 0) {
            REBLEN i;
            for (i = 0; i < temp/2; i++) {
                REBINT a = vp[temp - i - 1];
                vp[temp - i - 1] = vp[i];
                vp[i] = a;
            }
        }
        return Init_Tuple_Bytes(OUT, buf, len); }

      default:
        break;
    }

    fail (UNHANDLED);
}


//
//  MF_Sequence: C
//
void MF_Sequence(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form);

    Heart heart = Cell_Heart(v);
    char interstitial = Any_Tuple_Kind(heart) ? '.' : '/';

    if (heart == REB_GET_TUPLE)
        Append_Codepoint(mo->string, ':');
    else if (heart == REB_META_PATH or heart == REB_META_TUPLE)
        Append_Codepoint(mo->string, '^');
    else if (heart == REB_THE_PATH or heart == REB_THE_TUPLE)
        Append_Codepoint(mo->string, '@');
    else if (heart == REB_TYPE_PATH or heart == REB_TYPE_TUPLE)
        Append_Codepoint(mo->string, '&');
    else if (heart == REB_VAR_PATH or heart == REB_VAR_TUPLE)
        Append_Codepoint(mo->string, '$');

    bool first = true;

    DECLARE_ELEMENT (element);
    Length len = Cell_Sequence_Len(v);
    REBLEN i;
    for (i = 0; i < len; ++i) {
        Copy_Sequence_At(element, c_cast(Cell*, v), i);  // !!! cast
        Heart element_heart = Cell_Heart_Ensure_Noquote(element);

        if (first)
            first = false;  // don't print `.` or `/` before first element
        else
            Append_Codepoint(mo->string, interstitial);

        if (element_heart == REB_BLANK) {
            // no blank molding; implicit
        }
        else if (element_heart == REB_WORD) {
            const Symbol* sym = Cell_Word_Symbol(element);
            assert(Not_Subclass_Flag(SYMBOL, sym, ILLEGAL_IN_ANY_SEQUENCE));
            if (Any_Tuple_Kind(heart))
                assert(Not_Subclass_Flag(SYMBOL, sym, ILLEGAL_IN_ANY_TUPLE));
            UNUSED(sym);

            Mold_Element(mo, element);
        }
        else {
            Mold_Element(mo, element);

            // Note: Ignore VALUE_FLAG_NEWLINE_BEFORE here for ANY-PATH,
            // but any embedded BLOCK! or GROUP! which do have newlines in
            // them can make newlines, e.g.:
            //
            //     a/[
            //        b c d
            //     ]/e
        }

    }

    if (heart == REB_SET_TUPLE)
        Append_Codepoint(mo->string, ':');
}
