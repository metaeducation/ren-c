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
//  Makehook_Sequence: C
//
// !!! This contains some old ideas from R3-Alpha for what you might be able
// to MAKE a TUPLE! from.  But primarily, this is an evaluative form of
// TO TUPLE! on BLOCK!, with the checking that performs included.
//
Bounce Makehook_Sequence(Level* level_, Kind kind, Element* arg)
{
    if (Is_Block(arg))
        return rebValue(
            Canon(TO), Datatype_From_Kind(kind), Canon(REDUCE), arg
        );

    REBLEN alen;

    if (Is_Issue(arg)) {
        Byte buf[MAX_TUPLE];
        Byte* vp = buf;

        const String* spelling = Cell_String(arg);
        const Byte* ap = String_Head(spelling);
        Size size = String_Size(spelling);  // UTF-8 len
        if (size & 1)
            return FAIL(arg);  // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            return FAIL(arg);  // valid even for UTF-8
        for (alen = 0; alen < size; alen++) {
            Byte decoded;
            if (not (ap = maybe Try_Scan_Hex2(&decoded, ap)))
                return FAIL(arg);
            *vp++ = decoded;
        }
        Init_Tuple_Bytes(OUT, buf, size);
        return OUT;
    }

    if (Is_Blob(arg)) {
        Size size;
        const Byte* at = Cell_Blob_Size_At(&size, arg);
        if (size > MAX_TUPLE)
            size = MAX_TUPLE;
        Init_Tuple_Bytes(OUT, at, size);
        return OUT;
    }

    return RAISE(Error_Bad_Make(REB_TUPLE, arg));
}


//
//  DECLARE_GENERICS: C
//
// !!! This is shared code between TUPLE! and PATH!.  The math operations
// predate the unification, and are here to document what expected operations
// were...though they should use the method of PAIR! to generate frames for
// each operation and run them against each other.
//
DECLARE_GENERICS(Sequence)
{
    Option(SymId) id = Symbol_Id(verb);

    Element* sequence = cast(Element*, (id == SYM_TO) ? ARG_N(2) : ARG_N(1));
    Length len = Cell_Sequence_Len(sequence);
    Heart heart = Cell_Heart_Ensure_Noquote(sequence);

    switch (id) {
      case SYM_REFLECT: {
        INCLUDE_PARAMS_OF_REFLECT;
        UNUSED(ARG(value));

        switch (Cell_Word_Id(ARG(property))) {
          case SYM_LENGTH:
            return Init_Integer(OUT, len);

          case SYM_INDEX:
            return RAISE(Error_Type_Has_No_Index_Raw(Type_Of(sequence)));

          default:
            return UNHANDLED;  // !!! binding? etc.
        }
        break; }

        // ANY-SEQUENCE? is immutable, so a shallow copy should be a no-op,
        // but it should be cheap for any similarly marked array.  Also, a
        // :DEEP copy of a path may copy groups that are mutable.
        //
      case SYM_COPY: {
        if (not Listlike_Cell(sequence))
            return Copy_Cell(level_->out, sequence);

        HEART_BYTE(sequence) = REB_BLOCK;

        Atom* r = Atom_From_Bounce(T_List(level_, verb));
        assert(Cell_Heart(r) == REB_BLOCK);

        if (r != OUT)
            Copy_Cell(OUT, r);

        Freeze_Source_Shallow(Cell_Array_Known_Mutable(OUT));
        HEART_BYTE(OUT) = heart;
        return OUT; }

  //=//// TO CONVERSIONS //////////////////////////////////////////////////=//

  // 1. We can only convert up the hierarchy.  e.g. a path like a:b/c:d can't
  //    be converted "TO" a chain as a:b:c:d ... while such a chain could be
  //    constructed, it can't reuse the allocation.
  //
  //    !!! Should this restriction be what AS does, while TO will actually
  //    "flatten"?  How useful is the flattening operation, really?

      case SYM_TO: {
        INCLUDE_PARAMS_OF_TO;
        UNUSED(ARG(element));  // sequence
        Heart to = VAL_TYPE_HEART(ARG(type));

        if (Any_Sequence_Kind(to)) {  // e.g. `to the-chain! 'a.b.c` [1]
            if (
                (Any_Path_Kind(to) and Any_Path_Kind(heart))
                or (Any_Chain_Kind(to) and Any_Chain_Kind(heart))
                or (Any_Tuple_Kind(to) and Any_Tuple_Kind(heart))
            ){
                HEART_BYTE(sequence) = to;
                return COPY(sequence);
            }

            Offset i;
            for (i = 0; i < len; ++i) {
                Copy_Sequence_At(
                    PUSH(), sequence, i
                );
            }
            Option(Error*) error = Trap_Pop_Sequence(OUT, to, STACK_BASE);
            if (error)
                return RAISE(unwrap error);

            return OUT;
        }

        if (Any_List_Kind(to)) {  // !!! Should list have isomorphic binding?
            Source* a = Make_Source_Managed(len);
            Set_Flex_Len(a, len);
            Offset i;
            for (i = 0; i < len; ++i) {
                Derelativize_Sequence_At(
                    Array_At(a, i),
                    sequence,
                    Cell_Sequence_Binding(sequence),
                    i
                );
            }
            return Init_Any_List(OUT, to, a);
        }

        if (Any_Utf8_Kind(to)) {
            DECLARE_MOLDER (mo);
            Push_Mold(mo);
            Offset i;
            for (i = 0; i < len; ++i) {
                DECLARE_ELEMENT (temp);
                Copy_Sequence_At(temp, sequence, i);
                Mold_Element(mo, temp);
                if (i != len - 1)
                    Append_Codepoint(mo->string, ' ');
            }
            const String* s = Pop_Molded_String(mo);
            if (not Any_String_Kind(to))
                Freeze_Flex(s);
            return Init_Any_String(OUT, to, s);
        }

        return FAIL(Error_Bad_Cast_Raw(sequence, ARG(type))); }

      case SYM_PICK: {
        INCLUDE_PARAMS_OF_PICK;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);

        REBINT n;
        if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
            n = Int32(picker) - 1;
        }
        else
            return FAIL(picker);

        if (n < 0 or n >= Cell_Sequence_Len(sequence))
            return RAISE(Error_Bad_Pick_Raw(picker));

        Copy_Sequence_At(OUT, sequence, n);
        return OUT; }

    // !!! Should REVERSE of a sequence be supported, when sequences are
    // fundamentally immutable?  Probably not, but this replaces code that was
    // outdated and completely buggy.

      case SYM_REVERSE: {
        INCLUDE_PARAMS_OF_REVERSE;
        UNUSED(PARAM(series));

        Length part = len;

        if (REF(part)) {
            Length temp = Get_Num_From_Arg(ARG(part));
            part = MIN(temp, len);
        }

        return rebDelegate(
            "let t: type of", rebQ(sequence),
            "as t reverse:part to block!", rebQ(sequence), rebI(part)
        ); }

    // !!! RANDOM is SHUFFLE by default, so same question about mutability
    // and if SHUFFLE:COPY should be a thing.  RANDOM:ONLY picks an item out
    // of a series at random (should be default for random?)
    //
    // !!! Historical Redbol treated RANDOM of a TUPLE! as using each of the
    // tuple values as a maximum of the chosen number, which seems kind of
    // useless unless you say `random 255.255.255.255` to try and get a
    // random IPv4 address.  :-/  All of this needs rethinking.

      case SYM_RANDOM: {
        INCLUDE_PARAMS_OF_RANDOM;
        UNUSED(ARG(value));

        if (REF(seed) or REF(secure))
            return (Error_Bad_Refines_Raw());

        if (REF(only))
            return rebDelegate("random:only as block!", rebQ(sequence));

        return rebDelegate(
            "let t: type of", rebQ(sequence),
            "as t random to block!", rebQ(sequence)
        ); }

      case SYM_ADD:
      case SYM_SUBTRACT:
      case SYM_MULTIPLY:
      case SYM_DIVIDE:
      case SYM_REMAINDER:
      case SYM_BITWISE_AND:
      case SYM_BITWISE_OR:
      case SYM_BITWISE_XOR:
      case SYM_BITWISE_AND_NOT:
        goto legacy_tuple_math;

      case SYM_BITWISE_NOT:  // unary math operation
        goto legacy_tuple_math;

      default:
        return UNHANDLED;
    }

  legacy_tuple_math: { ///////////////////////////////////////////////////////

    // !!! This is broken code that the tests ran through, and are used in
    // some capacity for versioning in bootstrap.  It was kept around just to
    // continue booting.  The ideas need complete rethinking, as it only sort
    // of works on sequences that are some finite number of integers.

    Byte buf[MAX_TUPLE];

    if (len > MAX_TUPLE or not Try_Get_Sequence_Bytes(buf, sequence, len))
        return FAIL("Legacy TUPLE! math: only short all-integer sequences");

    Byte* vp = buf;

    if (id == SYM_BITWISE_NOT) {
        REBLEN temp = len;
        for (; temp > 0; --temp, vp++)
            *vp = cast(Byte, ~*vp);
        return Init_Tuple_Bytes(OUT, buf, len);
    }

    Byte abuf[MAX_TUPLE];
    const Byte* ap;
    REBINT a;
    REBDEC dec;

    Value* arg = ARG_N(2);

    if (Is_Integer(arg)) {
        dec = -207.6382;  // unused but avoid maybe uninitialized warning
        a = VAL_INT32(arg);
        ap = nullptr;
    }
    else if (Is_Decimal(arg) || Is_Percent(arg)) {
        dec = VAL_DECIMAL(arg);
        a = cast(REBINT, dec);
        ap = nullptr;
    }
    else if (Is_Tuple(arg)) {
        dec = -251.8517;  // unused but avoid maybe uninitialized warning
        a = 646699;  // unused but avoid maybe uninitialized warning

        REBLEN alen = Cell_Sequence_Len(arg);
        if (
            alen > MAX_TUPLE
            or not Try_Get_Sequence_Bytes(abuf, arg, alen)
        ){
            return FAIL("Legacy TUPLE! math: only short all-integer sequences");
        }

        // Historical behavior: 1.1.1 + 2.2.2.2 => 3.3.3.2
        // Zero-extend buffers so lengths match, output length will be longest
        //
        for (; len < alen; ++len)
            buf[len] = 0;
        for (; alen < len; ++alen)
            abuf[alen] = 0;

        ap = abuf;
    }
    else
        return FAIL(Error_Math_Args(REB_TUPLE, verb));

    REBLEN temp = len;
    for (; temp > 0; --temp, ++vp) {
        REBINT v = *vp;
        if (ap)
            a = *ap++;

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
                    return FAIL(Error_Zero_Divide_Raw());

                // !!! After moving all the ROUND service routines to
                // talk directly to ROUND frames, cases like this that
                // don't have round frames need one.  Can't run:
                //
                //    v = Round_Dec(v / dec, 0, 1.0);
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
                    return FAIL(Error_Zero_Divide_Raw());
                v /= a;
            }
            break;

          case SYM_REMAINDER:
            if (a == 0)
                return FAIL(Error_Zero_Divide_Raw());
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
            return UNHANDLED;
        }

        if (v > 255)
            v = 255;
        else if (v < 0)
            v = 0;
        *vp = cast(Byte, v);
    }

    return Init_Tuple_Bytes(OUT, buf, len);
}}


//
//  MF_Sequence: C
//
// 1. We ignore CELL_FLAG_NEWLINE_BEFORE here for the sequence elements
//    themselves.  But any embedded BLOCK! or GROUP! which do have newlines in
//    them can make newlines, e.g.:
//
//         a/[
//            b c d
//         ]/e
//
void MF_Sequence(Molder* mo, const Cell* c, bool form)
{
    UNUSED(form);

    Heart heart = Cell_Heart(c);

    char interstitial;
    if (Any_Tuple_Kind(heart))
        interstitial = '.';
    else if (Any_Chain_Kind(heart))
        interstitial = ':';
    else {
        assert(Any_Path_Kind(heart));
        interstitial = '/';
    }

    if (Any_Meta_Kind(heart))
        Append_Codepoint(mo->string, '^');
    else if (Any_The_Kind(heart))
        Append_Codepoint(mo->string, '@');
    else if (Any_Type_Kind(heart))
        Append_Codepoint(mo->string, '&');
    else if (Any_Var_Kind(heart))
        Append_Codepoint(mo->string, '$');

    DECLARE_ELEMENT (element);
    Length len = Cell_Sequence_Len(c);
    Offset i;
    for (i = 0; i < len; ++i) {
        Copy_Sequence_At(element, c, i);  // !!! cast

        if (i == 0) {
            // don't print `.` or `/` before first element
        } else {
            Append_Codepoint(mo->string, interstitial);
        }

        if (Is_Blank(element)) {  // blank molds invisibly
            assert(i == 0 or i == len - 1);  // head or tail only
        }
        else {
            if (Is_Word(element)) {  // double-check word legality in debug
                const Symbol* s = Cell_Word_Symbol(element);
                if (Get_Flavor_Flag(SYMBOL, s, ILLEGAL_IN_ANY_SEQUENCE))
                    assert(
                        Any_Chain_Kind(heart)
                        and Cell_Sequence_Len(c) == 2
                    );
                if (Any_Tuple_Kind(heart))
                    assert(Not_Flavor_Flag(SYMBOL, s, ILLEGAL_IN_ANY_TUPLE));
                UNUSED(s);
            }

            Mold_Element(mo, element);  // ignore CELL_FLAG_NEWLINE_BEFORE [1]
        }
    }
}
