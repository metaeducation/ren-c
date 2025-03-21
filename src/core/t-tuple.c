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


// !!! This contains some old ideas from R3-Alpha for what you might be able
// to MAKE a TUPLE! from.  But primarily, this is an evaluative form of
// TO TUPLE! on BLOCK!, with the checking that performs included.
//
IMPLEMENT_GENERIC(MAKE, Any_Sequence)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Cell_Datatype_Heart(ARG(TYPE));
    assert(Any_Sequence_Type(heart));

    Element* arg = Element_ARG(DEF);

    if (Is_Block(arg))
        return rebValue(
            CANON(TO), Datatype_From_Type(heart), CANON(REDUCE), arg
        );

    if (Is_Text(arg)) {
        Option(Error*) error = Trap_Transcode_One(OUT, heart, arg);
        if (error)
            return RAISE(unwrap error);
        return OUT;
    }

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

    return RAISE(Error_Bad_Make(TYPE_TUPLE, arg));
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Sequence)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* sequence = cast(Element*, ARG_N(1));
    Length len = Cell_Sequence_Len(sequence);

    switch (id) {
      case SYM_ADD:
      case SYM_SUBTRACT:
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
        return FAIL(Error_Math_Args(TYPE_TUPLE, verb));

    REBLEN temp = len;
    for (; temp > 0; --temp, ++vp) {
        REBINT v = *vp;
        if (ap)
            a = *ap++;

        switch (id) {
          case SYM_ADD: v += a; break;

          case SYM_SUBTRACT: v -= a; break;

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


// 1. We can only convert up the hierarchy.  e.g. a path like a:b/c:d can't
//    be converted "TO" a chain as a:b:c:d ... while such a chain could be
//    constructed, it can't reuse the allocation.
//
//    !!! Should this restriction be what AS does, while TO will actually
//    "flatten"?  How useful is the flattening operation, really?
//
IMPLEMENT_GENERIC(TO, Any_Sequence)
{
    INCLUDE_PARAMS_OF_TO;

    Element* seq = Element_ARG(ELEMENT);

    Heart to = Cell_Datatype_Heart(ARG(TYPE));

    if (Any_Sequence_Type(to))  // e.g. `to the-chain! 'a.b.c` [1]
        return GENERIC_CFUNC(AS, Any_Sequence)(LEVEL);  // immutable, same code

    if (Any_List_Type(to)) {  // !!! Should list have isomorphic binding?
        Source* a = Make_Source_Managed(1);
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), seq);
        Plainify(Array_Head(a));  // to block! @a.b.c -> [a.b.c]
        return Init_Any_List(OUT, to, a);
    }

    if (Any_Utf8_Type(to) and not Any_Word_Type(to)) {
        DECLARE_MOLDER (mo);
        Push_Mold(mo);
        Plainify(seq);  // to text! @a.b.c -> "a.b.c"
        Form_Element(mo, seq);
        const String* s = Pop_Molded_String(mo);
        if (not Any_String_Type(to))
            Freeze_Flex(s);
        return Init_Any_String(OUT, to, s);
    }

    return UNHANDLED;
}


//
//  Trap_Alias_Any_Sequence_As: C
//
// 1. If you have a PATH! like "a.b/c.d" and you change the heart byte
//    to a TUPLE!, you'd get "a.b.c.d" which would be an invalidly
//    constructed tuple of length 2, with two tuples in it.  The TO
//    conversion code constructs new tuples, but AS is supposed to be
//    for efficiency.  The code should be merged into a version that is
//    efficient when it can be: TO and AS should maybe be the same.
//
// 2. Pairings are usually the same size as stubs...but not always.  If the
//    UNUSUAL_CELL_SIZE flag is set, pairings will be in their own pool.
//    Were there a strong incentive to have separate code for that case,
//    we could reuse the node...but the case is not that strong.  It may be
//    that AS should not be willing to alias sequences since compressed
//    cases will force new allocations (e.g. aliasing a refinement has to
//    make a new array, since the symbol absolutely can't be mutated into
//    an array node).  Review.
//
Option(Error*) Trap_Alias_Any_Sequence_As(
    Sink(Element) out,
    const Element* seq,
    Heart as
){
    Length len = Cell_Sequence_Len(seq);

    if (Any_Sequence_Type(as)) {  // not all aliasings are legal [1]
        REBINT i;
        for (i = 0; i < len; ++i) {
            DECLARE_ELEMENT (temp);
            Copy_Sequence_At(temp, seq, i);
            if (not Any_Sequence(temp))
                continue;

            assert(not Any_Path(temp));  // impossible!
            if (Any_Chain(temp) and (as == TYPE_TUPLE or as == TYPE_CHAIN))
                return Error_User(
                    "Can't AS alias CHAIN!-containing sequence"
                    "as TUPLE! or CHAIN!"
                );

            if (Any_Tuple(temp) and as == TYPE_TUPLE)
                return Error_User(
                    "Can't AS alias TUPLE!-containing sequence as TUPLE!"
                );
        }

        Trust_Const(Copy_Cell(out, seq));
        HEART_BYTE(out) = as;
        possibly(Get_Cell_Flag(out, LEADING_BLANK));
        return nullptr;
    }

    if (Any_List_Type(as)) {  // give immutable form, try to share memory
        if (not Sequence_Has_Node(seq)) {  // byte packed sequence
            Source* a = Make_Source_Managed(len);
            Set_Flex_Len(a, len);
            Offset i;
            for (i = 0; i < len; ++i)
                Copy_Sequence_At(Array_At(a, i), seq, i);
            Init_Any_List(out, as, a);
            return nullptr;
        }

        const Node* node1 = CELL_NODE1(seq);
        if (Is_Node_A_Cell(node1)) {  // reusing node complicated [2]
            const Pairing* p = c_cast(Pairing*, node1);
            Context *binding = Cell_List_Binding(seq);
            Source* a = Make_Source_Managed(2);
            Set_Flex_Len(a, 2);
            Derelativize(Array_At(a, 0), Pairing_First(p), binding);
            Derelativize(Array_At(a, 1), Pairing_Second(p), binding);
            Freeze_Source_Shallow(a);
            Init_Any_List(out, as, a);
        }
        else switch (Stub_Flavor(c_cast(Flex*, node1))) {
          case FLAVOR_SYMBOL: {
            Source* a = Make_Source_Managed(2);
            Set_Flex_Len(a, 2);
            if (Get_Cell_Flag(seq, LEADING_BLANK)) {
                Init_Blank(Array_At(a, 0));
                Copy_Cell(Array_At(a, 1), seq);
                HEART_BYTE(Array_At(a, 1)) = TYPE_WORD;
            }
            else {
                Copy_Cell(Array_At(a, 0), seq);
                HEART_BYTE(Array_At(a, 0)) = TYPE_WORD;
                Init_Blank(Array_At(a, 1));
            }
            Freeze_Source_Shallow(a);
            Init_Any_List(out, as, a);
            break; }

          case FLAVOR_SOURCE: {
            const Source* a = Cell_Array(seq);
            if (MIRROR_BYTE(a)) {  // .[a] or (xxx): compression
                Source* two = Make_Source_Managed(2);
                Set_Flex_Len(two, 2);
                Cell* tweak;
                if (Get_Cell_Flag(seq, LEADING_BLANK)) {
                    Init_Blank(Array_At(two, 0));
                    tweak = Copy_Cell(Array_At(two, 1), seq);
                }
                else {
                    tweak = Copy_Cell(Array_At(two, 0), seq);
                    Init_Blank(Array_At(two, 1));
                }
                HEART_BYTE(tweak) = MIRROR_BYTE(a);
                Clear_Cell_Flag(tweak, LEADING_BLANK);
                Init_Any_List(out, as, two);
            }
            else {
                assert(Is_Source_Frozen_Shallow(a));
                Copy_Cell(out, seq);
                HEART_BYTE(out) = TYPE_BLOCK;
                Clear_Cell_Flag(out, LEADING_BLANK);  // don't want stray flag
            }
            break; }

          default:
            assert(false);
        }
        return nullptr;
    }

    return Error_Invalid_Type(as);;
}


IMPLEMENT_GENERIC(AS, Any_Sequence)
{
    INCLUDE_PARAMS_OF_AS;

    Option(Error*) e = Trap_Alias_Any_Sequence_As(
        OUT,
        Element_ARG(ELEMENT),
        Cell_Datatype_Heart(ARG(TYPE))
    );
    if (e)
        return FAIL(unwrap e);

    return OUT;
}


// ANY-SEQUENCE? is immutable, so a shallow copy should be a no-op.  However
// if it contains series values then COPY:DEEP may be meaningful.
//
// 1. We could do some clever optimizations here probably, in that we could
//    move the sequence out of the way and then be able to reuse the frame
//    just to invoke the COPY generic dispatchers of the elements inside.
//    But this is a low priority.  Most cases will probably be fast as it
//    is rare to be interested in copying a sequence at all.
//
IMPLEMENT_GENERIC(COPY, Any_Sequence)
{
    INCLUDE_PARAMS_OF_COPY;

    Element* seq = Element_ARG(VALUE);
    bool deep = Bool_ARG(DEEP);
    Value* part = ARG(PART);

    if (not deep or Wordlike_Cell(seq)) {  // wordlike is /A or :B etc
        if (part)
            return FAIL(part);
        return COPY(seq);
    }

    bool trivial_copy = true;

    Length len = Cell_Sequence_Len(seq);
    Offset n;
    for (n = 0; n < len; ++len) {  // first let's see if it's a trivial copy
        Copy_Sequence_At(SPARE, seq, n);
        Heart item_heart = Cell_Heart(SPARE);
        if (Handles_Generic(COPY, item_heart)) {
            trivial_copy = false;
            break;
        }
    }

    if (trivial_copy)  // something like a/1/foo
        return COPY(seq);

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    Meta_Quotify(datatype);
    Quotify(seq);
    Meta_Quotify(part);
    return rebDelegate(  // slow, but not a high priority to write it fast [1]
        CANON(AS), datatype, CANON(COPY), CANON(_S_S), "[",
            CANON(AS), CANON(BLOCK_X), seq, ":part", part, ":deep ~okay~"
        "]"
    );
}


IMPLEMENT_GENERIC(PICK, Any_Sequence)
{
    INCLUDE_PARAMS_OF_PICK;

    const Element* seq = Element_ARG(LOCATION);
    const Element* picker = Element_ARG(PICKER);

    REBINT n;
    if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
        n = Int32(picker) - 1;
    }
    else
        return FAIL(picker);

    if (n < 0 or n >= Cell_Sequence_Len(seq))
        return RAISE(Error_Bad_Pick_Raw(picker));

    Copy_Sequence_At(OUT, seq, n);
    return OUT;
}


// Sequences (TUPLE!, PATH!, etc.) are not mutable, so they don't support
// REVERSE, only REVERSE OF which creates a new sequence.
//
IMPLEMENT_GENERIC(REVERSE_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_REVERSE_OF;

    Element* seq = Element_ARG(ELEMENT);
    Value* part = ARG(PART);

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    return Delegate_Operation_With_Part(
        SYM_REVERSE, SYM_BLOCK_X,
        Meta_Quotify(datatype), Quotify(seq), Meta_Quotify(part)
    );
}


// See notes on RANDOM-PICK on whether specializations like this are worth it.
//
// 1. When a sequence has a Symbol* in its Payload, that implies that it is
//    a sequence representing a BLANK! and a WORD!.  A flag controls whether
//    that is a leading blank or trailing blank.  We don't care which--all
//    we do is have a 50-50 chance of making a blank or a word.
//
IMPLEMENT_GENERIC(RANDOM_PICK, Any_Sequence)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* seq = Element_ARG(COLLECTION);

    if (Wordlike_Cell(seq)) {  // e.g. FOO: or :FOO [1]
        REBI64 one_or_two = Random_Range(2, Bool_ARG(SECURE));
        if (one_or_two == 1)
            return Init_Blank(OUT);
        Copy_Cell(OUT, seq);
        HEART_BYTE(OUT) = TYPE_WORD;
        return OUT;
    }

    if (Pairlike_Cell(seq)) {  // e.g. A/B
        assert(Listlike_Cell(seq));  // all pairlikes are also listlike
        REBI64 one_or_two = Random_Range(2, Bool_ARG(SECURE));
        if (one_or_two == 1)
            return COPY(Cell_Pair_First(seq));
        return COPY(Cell_Pair_Second(seq));
    }

    if (Listlike_Cell(seq)) {  // alias as BLOCK! and dispatch to list pick
        possibly(Pairlike_Cell(seq));  // why we tested pairlike first
        HEART_BYTE(seq) = TYPE_BLOCK;
        return GENERIC_CFUNC(RANDOM_PICK, Any_List)(LEVEL);
    }

    assert(not Sequence_Has_Node(seq));  // packed byte sequence

    Byte used = seq->payload.at_least_8[IDX_SEQUENCE_USED];

    REBI64 rand = Random_Range(used, Bool_ARG(SECURE));  // from 1 to used
    return Init_Integer(OUT, seq->payload.at_least_8[rand]);
}


IMPLEMENT_GENERIC(SHUFFLE_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_SHUFFLE_OF;

    Element* seq = Element_ARG(ELEMENT);
    Value* part = ARG(PART);

    if (Bool_ARG(SECURE) or Bool_ARG(PART))
        return (Error_Bad_Refines_Raw());

    Value* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    return Delegate_Operation_With_Part(
        SYM_SHUFFLE, SYM_BLOCK_X,
        Meta_Quotify(datatype), Quotify(seq), Meta_Quotify(part)
    );
}


IMPLEMENT_GENERIC(LENGTH_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* seq = Element_ARG(ELEMENT);

    return Init_Integer(OUT, Cell_Sequence_Len(seq));
}


IMPLEMENT_GENERIC(MULTIPLY, Any_Sequence)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Value* seq1 = ARG(VALUE1);  // dispatch is on first argument
    assert(Any_Sequence(seq1));

    Value* arg2 = ARG(VALUE2);
    if (not Is_Integer(arg2))
        return FAIL(PARAM(VALUE2));  // formerly supported decimal/percent

    return rebDelegate(
        "join type of", seq1, "map-each 'i", seq1, "[",
            arg2, "* match integer! i else [",
                "fail -{Can't multiply sequence unless all integers}-"
            "]",
        "]"
    );
}


// 1. We ignore CELL_FLAG_NEWLINE_BEFORE here for the sequence elements
//    themselves.  But any embedded BLOCK! or GROUP! which do have newlines in
//    them can make newlines, e.g.:
//
//         a/[
//            b c d
//         ]/e
//
IMPLEMENT_GENERIC(MOLDIFY, Any_Sequence)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* c = Element_ARG(ELEMENT);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    UNUSED(form);

    Heart heart = Cell_Heart(c);

    char interstitial;
    if (Any_Tuple_Type(heart))
        interstitial = '.';
    else if (Any_Chain_Type(heart))
        interstitial = ':';
    else {
        assert(Any_Path_Type(heart));
        interstitial = '/';
    }

    if (Any_Meta_Type(heart))
        Append_Codepoint(mo->string, '^');
    else if (Any_The_Type(heart))
        Append_Codepoint(mo->string, '@');
    else if (Any_Type_Type(heart))
        Append_Codepoint(mo->string, '&');
    else if (Any_Var_Type(heart))
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
                        Any_Chain_Type(heart)
                        and Cell_Sequence_Len(c) == 2
                    );
                if (Any_Tuple_Type(heart))
                    assert(Not_Flavor_Flag(SYMBOL, s, ILLEGAL_IN_ANY_TUPLE));
                UNUSED(s);
            }

            Mold_Element(mo, element);  // ignore CELL_FLAG_NEWLINE_BEFORE [1]
        }
    }

    return NOTHING;
}
