//
//  file: %t-tuple.c
//  summary: "tuple datatype"
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


// !!! This contains some old ideas from R3-Alpha for what you might be able
// to MAKE a TUPLE! from.  But primarily, this is an evaluative form of
// TO TUPLE! on BLOCK!, with the checking that performs included.
//
IMPLEMENT_GENERIC(MAKE, Any_Sequence)
{
    INCLUDE_PARAMS_OF_MAKE;

    Heart heart = Datatype_Builtin_Heart(ARG(TYPE));
    assert(Any_Sequence_Type(heart));

    Element* arg = Element_ARG(DEF);

    if (Is_Block(arg))
        return rebValue(
            CANON(TO), Datatype_From_Type(heart), CANON(REDUCE), arg
        );

    if (Is_Text(arg)) {
        trap (
          Transcode_One(OUT, heart, arg)
        );
        return OUT;
    }

    REBLEN alen;

    if (Is_Rune(arg)) {
        Byte buf[MAX_TUPLE];
        Byte* vp = buf;

        const Strand* spelling = Cell_Strand(arg);
        const Byte* ap = Strand_Head(spelling);
        Size size = Strand_Size(spelling);  // UTF-8 len
        if (size & 1)
            panic (arg);  // must have even # of chars
        size /= 2;
        if (size > MAX_TUPLE)
            panic (arg);  // valid even for UTF-8
        for (alen = 0; alen < size; alen++) {
            Byte decoded;
            if (not (ap = opt Try_Scan_Hex2(&decoded, ap)))
                panic (arg);
            *vp++ = decoded;
        }
        require (
          Init_Tuple_Bytes(OUT, buf, size)
        );
        return OUT;
    }

    if (Is_Blob(arg)) {
        Size size;
        const Byte* at = Blob_Size_At(&size, arg);
        if (size > MAX_TUPLE)
            size = MAX_TUPLE;
        require (
          Init_Tuple_Bytes(OUT, at, size)
        );
        return OUT;
    }

    return fail (Error_Bad_Make(TYPE_TUPLE, arg));
}


IMPLEMENT_GENERIC(OLDGENERIC, Any_Sequence)
{
    const Symbol* verb = Level_Verb(LEVEL);
    Option(SymId) id = Symbol_Id(verb);

    Element* sequence = cast(Element*, ARG_N(1));
    Length len = Sequence_Len(sequence);

    switch (opt id) {
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
        panic (UNHANDLED);
    }

  legacy_tuple_math: { ///////////////////////////////////////////////////////

    // !!! This is broken code that the tests ran through, and are used in
    // some capacity for versioning in bootstrap.  It was kept around just to
    // continue booting.  The ideas need complete rethinking, as it only sort
    // of works on sequences that are some finite number of integers.

    Byte buf[MAX_TUPLE];

    if (len > MAX_TUPLE or not Try_Get_Sequence_Bytes(buf, sequence, len))
        panic ("Legacy TUPLE! math: only short all-integer sequences");

    Byte* vp = buf;

    if (id == SYM_BITWISE_NOT) {
        REBLEN temp = len;
        for (; temp > 0; --temp, vp++)
            *vp = cast(Byte, ~*vp);
        require (
          Init_Tuple_Bytes(OUT, buf, len)
        );
        return OUT;
    }

    Byte abuf[MAX_TUPLE];
    const Byte* ap;
    REBINT a;
    REBDEC dec;

    Stable* arg = ARG_N(2);

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

        REBLEN alen = Sequence_Len(arg);
        if (
            alen > MAX_TUPLE
            or not Try_Get_Sequence_Bytes(abuf, arg, alen)
        ){
            panic ("Legacy TUPLE! math: only short all-integer sequences");
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
        panic (Error_Math_Args(TYPE_TUPLE, verb));

    REBLEN temp = len;
    for (; temp > 0; --temp, ++vp) {
        REBINT v = *vp;
        if (ap)
            a = *ap++;

        switch (opt id) {
          case SYM_ADD: v += a; break;

          case SYM_SUBTRACT: v -= a; break;

          case SYM_DIVIDE:
            if (Is_Decimal(arg) || Is_Percent(arg)) {
                if (dec == 0.0)
                    panic (Error_Zero_Divide_Raw());

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
                    panic (Error_Zero_Divide_Raw());
                v /= a;
            }
            break;

          case SYM_REMAINDER:
            if (a == 0)
                panic (Error_Zero_Divide_Raw());
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
            panic (UNHANDLED);
        }

        if (v > 255)
            v = 255;
        else if (v < 0)
            v = 0;
        *vp = cast(Byte, v);
    }

    require (
      Init_Tuple_Bytes(OUT, buf, len)
    );
    return OUT;
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

    Element* seq = Element_ARG(VALUE);

    Heart to = Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_Sequence_Type(to))  // e.g. `to chain! 'a.b.c` [1]
        return GENERIC_CFUNC(AS, Any_Sequence)(LEVEL);  // immutable, same code

    if (Any_List_Type(to)) {  // !!! Should list have isomorphic binding?
        Source* a = Make_Source_Managed(1);
        Set_Flex_Len(a, 1);
        Copy_Cell(Array_Head(a), seq);
        Clear_Cell_Sigil(Array_Head(a));  // to block! @a.b.c -> [a.b.c]
        return Init_Any_List(OUT, to, a);
    }

    if (Any_Utf8_Type(to) and to != TYPE_WORD) {
        DECLARE_MOLDER (mo);
        Push_Mold(mo);
        Clear_Cell_Sigil(seq);  // to text! @a.b.c -> "a.b.c"
        Form_Element(mo, seq);
        const Strand* s = Pop_Molded_Strand(mo);
        if (not Any_String_Type(to))
            Freeze_Flex(s);
        return Init_Any_String(OUT, to, s);
    }

    panic (UNHANDLED);
}


//
//  Alias_Any_Sequence_As: C
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
Result(Element*) Alias_Any_Sequence_As(
    Sink(Element) out,
    const Element* seq,
    Heart as
){
    Length len = Sequence_Len(seq);

    if (Any_Sequence_Type(as)) {  // not all aliasings are legal [1]
        REBINT i;
        for (i = 0; i < len; ++i) {
            DECLARE_ELEMENT (temp);
            Copy_Sequence_At(temp, seq, i);
            if (not Any_Sequence(temp))
                continue;

            assert(not Is_Path(temp));  // impossible!
            if (Is_Chain(temp) and (as == TYPE_TUPLE or as == TYPE_CHAIN))
                return fail (
                    "Can't AS alias CHAIN!-containing sequence"
                    "as TUPLE! or CHAIN!"
                );

            if (Is_Tuple(temp) and as == TYPE_TUPLE)
                return fail (
                    "Can't AS alias TUPLE!-containing sequence as TUPLE!"
                );
        }

        Trust_Const(Copy_Cell(out, seq));
        KIND_BYTE(out) = as;
        possibly(Get_Cell_Flag(out, LEADING_SPACE));
        return out;
    }

    if (Any_List_Type(as)) {  // give immutable form, try to share memory
        if (not Sequence_Has_Pointer(seq)) {  // byte packed sequence
            Source* a = Make_Source_Managed(len);
            Set_Flex_Len(a, len);
            Offset i;
            for (i = 0; i < len; ++i)
                Copy_Sequence_At(Array_At(a, i), seq, i);
            return Init_Any_List(out, as, a);
        }

        const Base* payload1 = CELL_PAYLOAD_1(seq);
        if (Is_Base_A_Cell(payload1)) {  // Pairings hold two items [2]
            const Pairing* p = cast(Pairing*, payload1);
            Context *binding = List_Binding(seq);
            Source* a = Make_Source_Managed(2);
            Set_Flex_Len(a, 2);
            Copy_Cell_May_Bind(Array_At(a, 0), Pairing_First(p), binding);
            Copy_Cell_May_Bind(Array_At(a, 1), Pairing_Second(p), binding);
            Freeze_Source_Shallow(a);
            Init_Any_List(out, as, a);
        }
        else switch (Stub_Flavor(cast(Flex*, payload1))) {
          case FLAVOR_SYMBOL: {
            Source* a = Make_Source_Managed(2);
            Set_Flex_Len(a, 2);
            if (Get_Cell_Flag(seq, LEADING_SPACE)) {
                Init_Space(Array_At(a, 0));
                Copy_Cell(Array_At(a, 1), seq);
                KIND_BYTE(Array_At(a, 1)) = TYPE_WORD;
            }
            else {
                Copy_Cell(Array_At(a, 0), seq);
                KIND_BYTE(Array_At(a, 0)) = TYPE_WORD;
                Init_Space(Array_At(a, 1));
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
                if (Get_Cell_Flag(seq, LEADING_SPACE)) {
                    Init_Space(Array_At(two, 0));
                    tweak = Copy_Cell(Array_At(two, 1), seq);
                }
                else {
                    tweak = Copy_Cell(Array_At(two, 0), seq);
                    Init_Space(Array_At(two, 1));
                }
                KIND_BYTE(tweak) = MIRROR_BYTE(a);
                Clear_Cell_Flag(tweak, LEADING_SPACE);
                Init_Any_List(out, as, two);
            }
            else {
                assert(Is_Source_Frozen_Shallow(a));
                Copy_Cell(out, seq);
                KIND_BYTE(out) = TYPE_BLOCK;
                Clear_Cell_Flag(out, LEADING_SPACE);  // don't want stray flag
            }
            break; }

          default:
            assert(false);
        }
        return out;
    }

    return fail (Error_Invalid_Type(as));
}


IMPLEMENT_GENERIC(AS, Any_Sequence)
{
    INCLUDE_PARAMS_OF_AS;

    Element* seq = Element_ARG(VALUE);
    Heart as = Datatype_Builtin_Heart(ARG(TYPE));

    require (
      Alias_Any_Sequence_As(OUT, seq, as)
    );
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
    bool deep = did ARG(DEEP);

    if (not deep or Is_Cell_Wordlike(seq)) {  // wordlike is /A or :B etc
        if (ARG(PART))
            panic (PARAM(PART));
        return COPY(seq);
    }

    bool trivial_copy = true;

    Length len = Sequence_Len(seq);
    Offset n;
    for (n = 0; n < len; ++len) {  // first let's see if it's a trivial copy
        Element* spare = Copy_Sequence_At(SPARE, seq, n);
        Heart item_heart = Heart_Of_Builtin_Fundamental(spare);
        if (Handles_Builtin_Generic(COPY, item_heart)) {
            trivial_copy = false;
            break;
        }
    }

    if (not ARG(PART) and trivial_copy)  // something like a/1/foo
        return COPY(seq);

    Stable* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    Value* part = LOCAL(PART);
    possibly(Is_Light_Null(part));

    Lift_Cell(datatype);
    Quote_Cell(seq);
    Lift_Cell(part);
    return rebDelegate(  // slow, but not a high priority to write it fast [1]
        CANON(AS), datatype, CANON(COPY), CANON(_S_S), "[",
            CANON(AS), CANON(BLOCK_X), seq, ":part", part, ":deep ~okay~"
        "]"
    );
}


IMPLEMENT_GENERIC(TWEAK_P, Any_Sequence)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    const Element* seq = Element_ARG(LOCATION);
    const Stable* picker = ARG(PICKER);

    REBINT n;
    if (Is_Integer(picker) or Is_Decimal(picker)) { // #2312
        n = Int32(picker) - 1;
    }
    else
        panic (picker);

    Stable* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    if (n < 0 or n >= Sequence_Len(seq))
        return DUAL_SIGNAL_NULL_ABSENT;

    Copy_Sequence_At(OUT, seq, n);
    return DUAL_LIFTED(OUT);

} handle_poke: { /////////////////////////////////////////////////////////////

    panic ("Cannot modify a TUPLE!, PATH!, or CHAIN! (immutable)");

}}


// Sequences (TUPLE!, PATH!, etc.) are not mutable, so they don't support
// REVERSE, only REVERSE OF which creates a new sequence.
//
IMPLEMENT_GENERIC(REVERSE_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_REVERSE_OF;

    Element* seq = Element_ARG(VALUE);
    Value* part = LOCAL(PART);
    possibly(Is_Light_Null(part));

    Stable* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    return Delegate_Operation_With_Part(
        SYM_REVERSE, SYM_BLOCK_X,
        Lift_Cell(datatype), Quote_Cell(seq), Lift_Cell(part)
    );
}


// See notes on RANDOM-PICK on whether specializations like this are worth it.
//
// 1. When a sequence has a Symbol* in its Payload, that implies that it is
//    a sequence representing a SPACE and a WORD!.  A flag controls whether
//    that is a leading space or trailing space.  We don't care which--all
//    we do is have a 50-50 chance of making a space or a word.
//
IMPLEMENT_GENERIC(RANDOM_PICK, Any_Sequence)
{
    INCLUDE_PARAMS_OF_RANDOM_PICK;

    Element* seq = Element_ARG(COLLECTION);

    if (Is_Cell_Wordlike(seq)) {  // e.g. FOO: or :FOO [1]
        REBI64 one_or_two = Random_Range(2, did ARG(SECURE));
        if (one_or_two == 1)
            return Init_Space(OUT);
        Copy_Cell(OUT, seq);
        KIND_BYTE(OUT) = TYPE_WORD;
        return OUT;
    }

    if (Is_Cell_Pairlike(seq)) {  // e.g. A/B
        assert(Is_Cell_Listlike(seq));  // all pairlikes are also listlike
        REBI64 one_or_two = Random_Range(2, did ARG(SECURE));
        if (one_or_two == 1)
            return COPY(Cell_Pair_First(seq));
        return COPY(Cell_Pair_Second(seq));
    }

    if (Is_Cell_Listlike(seq)) {  // alias as BLOCK! and dispatch to list pick
        possibly(Is_Cell_Pairlike(seq));  // why we tested pairlike first
        KIND_BYTE(seq) = TYPE_BLOCK;
        return GENERIC_CFUNC(RANDOM_PICK, Any_List)(LEVEL);
    }

    assert(not Sequence_Has_Pointer(seq));  // packed byte sequence

    Byte used = seq->payload.at_least_8[IDX_SEQUENCE_USED];

    REBI64 rand = Random_Range(used, did ARG(SECURE));  // from 1 to used
    return Init_Integer(OUT, seq->payload.at_least_8[rand]);
}


IMPLEMENT_GENERIC(SHUFFLE_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_SHUFFLE_OF;

    if (ARG(SECURE) or ARG(PART))
        panic (Error_Bad_Refines_Raw());

    Element* seq = Element_ARG(VALUE);
    Value* part = LOCAL(PART);
    possibly(Is_Light_Null(part));

    Stable* datatype = Copy_Cell(SPARE, Datatype_Of(seq));

    return Delegate_Operation_With_Part(
        SYM_SHUFFLE, SYM_BLOCK_X,
        Lift_Cell(datatype), Quote_Cell(seq), Lift_Cell(part)
    );
}


IMPLEMENT_GENERIC(LENGTH_OF, Any_Sequence)
{
    INCLUDE_PARAMS_OF_LENGTH_OF;

    Element* seq = Element_ARG(VALUE);

    return Init_Integer(OUT, Sequence_Len(seq));
}


IMPLEMENT_GENERIC(MULTIPLY, Any_Sequence)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Stable* seq1 = ARG(VALUE1);  // dispatch is on first argument
    assert(Any_Sequence(seq1));

    Stable* arg2 = ARG(VALUE2);
    if (not Is_Integer(arg2))
        panic (PARAM(VALUE2));  // formerly supported decimal/percent

    return rebDelegate(
        "join type of", seq1, "map-each 'i", seq1, "[",
            arg2, "* match integer! i else [",
                "panic -[Can't multiply sequence unless all integers]-"
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

    Element* c = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = did ARG(FORM);

    UNUSED(form);

    Heart heart = Heart_Of_Builtin_Fundamental(c);

    char interstitial;
    if (heart == TYPE_TUPLE)
        interstitial = '.';
    else if (heart == TYPE_CHAIN)
        interstitial = ':';
    else {
        assert(heart == TYPE_PATH);
        interstitial = '/';
    }

    DECLARE_ELEMENT (element);
    Length len = Sequence_Len(c);
    Offset i;
    for (i = 0; i < len; ++i) {
        Copy_Sequence_At(element, c, i);  // !!! cast

        if (i == 0) {
            // don't print `.` or `/` before first element
        } else {
            Append_Codepoint(mo->strand, interstitial);
        }

        if (Is_Space(element)) {  // space molds invisibly
            assert(i == 0 or i == len - 1);  // head or tail only
        }
        else {
            if (Is_Word(element)) {  // double-check word legality in debug
                const Symbol* s = Word_Symbol(element);
                if (Get_Flavor_Flag(SYMBOL, s, ILLEGAL_IN_ANY_SEQUENCE))
                    assert(
                        heart == TYPE_CHAIN
                        and Sequence_Len(c) == 2
                    );
                if (heart == TYPE_TUPLE)
                    assert(Not_Flavor_Flag(SYMBOL, s, ILLEGAL_IN_TUPLE));
                UNUSED(s);
            }

            Mold_Element(mo, element);  // ignore CELL_FLAG_NEWLINE_BEFORE [1]
        }
    }

    return TRASH;
}
