//
//  file: %t-pair.c
//  summary: "pair datatype"
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
// See %sys-pair.h for explanation.
//


#include "sys-core.h"


//
//  as-pair: native [
//
//  "Combine X and Y values into a pair"
//
//      return: [pair!]
//      x [integer!]
//      y [integer!]
//  ]
//
DECLARE_NATIVE(AS_PAIR)
{
    INCLUDE_PARAMS_OF_AS_PAIR;

    return Init_Pair(OUT, VAL_INT64(ARG(X)), VAL_INT64(ARG(Y)));
}


//
//  CT_Pair: C
//
REBINT CT_Pair(const Element* a, const Element* b, bool strict)
{
    UNUSED(strict);  // !!! Should this be heeded for the decimal?

    REBI64 diff;

    if ((diff = Cell_Pair_Y(a) - Cell_Pair_Y(b)) == 0)
        diff = Cell_Pair_X(a) - Cell_Pair_X(b);
    return (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
}


IMPLEMENT_GENERIC(EQUAL_Q, Is_Pair)
{
    INCLUDE_PARAMS_OF_EQUAL_Q;
    bool strict = not Bool_ARG(RELAX);

    Element* v1 = Element_ARG(VALUE1);
    Element* v2 = Element_ARG(VALUE2);

    return LOGIC(CT_Pair(v1, v2, strict) == 0);
}


IMPLEMENT_GENERIC(ZEROIFY, Is_Pair)
{
    INCLUDE_PARAMS_OF_ZEROIFY;
    UNUSED(ARG(EXAMPLE));  // always gives 0x0

    return Init_Pair(OUT, 0, 0);
}


IMPLEMENT_GENERIC(MAKE, Is_Pair)
{
    INCLUDE_PARAMS_OF_MAKE;

    assert(Datatype_Builtin_Heart(ARG(TYPE)) == TYPE_PAIR);
    UNUSED(ARG(TYPE));

    Element* arg = Element_ARG(DEF);

    if (Is_Text(arg)) {  // "-1234567890x-1234567890"
        trap (
          Transcode_One(OUT, TYPE_PAIR, arg)
        );
        return OUT;
    }

    if (Is_Integer(arg))
        return Init_Pair(OUT, VAL_INT64(arg), VAL_INT64(arg));

    if (Is_Block(arg))
        return rebValue(CANON(TO), CANON(PAIR_X), CANON(REDUCE), arg);

    return fail (Error_Bad_Make(TYPE_PAIR, arg));
}


//
//  Min_Max_Pair: C
//
void Min_Max_Pair(
    Sink(Value) out,
    const Value* a,
    const Value* b,
    bool maxed
){
    REBI64 x;
    if (Cell_Pair_X(a) > Cell_Pair_X(b))
        x = maxed ? Cell_Pair_X(a) : Cell_Pair_X(b);
    else
        x = maxed ? Cell_Pair_X(b) : Cell_Pair_X(a);

    REBI64 y;
    if (Cell_Pair_Y(a) > Cell_Pair_Y(b))
        y = maxed ? Cell_Pair_Y(a) : Cell_Pair_Y(b);
    else
        y = maxed ? Cell_Pair_Y(b) : Cell_Pair_Y(a);

    Init_Pair(out, x, y);
}


IMPLEMENT_GENERIC(MOLDIFY, Is_Pair)
{
    INCLUDE_PARAMS_OF_MOLDIFY;

    Element* v = Element_ARG(VALUE);
    Molder* mo = Cell_Handle_Pointer(Molder, ARG(MOLDER));
    bool form = Bool_ARG(FORM);

    Mold_Or_Form_Element(mo, Cell_Pair_First(v), form);

    Append_Codepoint(mo->strand, 'x');

    Mold_Or_Form_Element(mo, Cell_Pair_Second(v), form);

    return TRASH;
}


REBINT Index_From_Picker_For_Pair(
    const Element* pair,
    const Value* picker
){
    UNUSED(pair); // Might the picker be pair-sensitive?

    REBINT n;
    if (Is_Word(picker)) {
        if (Word_Id(picker) == SYM_X)
            n = 1;
        else if (Word_Id(picker) == SYM_Y)
            n = 2;
        else
            panic (picker);
    }
    else if (Is_Integer(picker)) {
        n = Int32(picker);
        if (n != 1 and n != 2)
            panic (picker);
    }
    else
        panic (picker);

    return n;
}


// !!! R3-Alpha turned all the PAIR! operations from integer to decimal, but
// they had floating point precision (otherwise you couldn't fit a full cell
// for two values into a single cell).  This meant they were neither INTEGER!
// nor DECIMAL!.  Ren-C stepped away from this idea of introducing a new
// numeric type and instead created a more compact "Pairing" that could fit
// in a single STUB_POOL Unit and hold two arbitrary values.
//
// With the exception of operations that are specifically pair-aware (e.g.
// REVERSE swapping X and Y), this chains to retrigger the action onto the
// pair elements and then return a pair made of that.
//
IMPLEMENT_GENERIC(OLDGENERIC, Is_Pair)
{
    Option(SymId) id = Symbol_Id(Level_Verb(LEVEL));

    Element* v = cast(Element*, ARG_N(1));
    Value* x1 = Cell_Pair_First(v);
    Value* y1 = Cell_Pair_Second(v);

    Value* x2 = nullptr;
    Value* y2 = nullptr;

    switch (opt id) {
      case SYM_ADD:
      case SYM_SUBTRACT:
      case SYM_DIVIDE:
        if (Is_Pair(ARG_N(2))) {
            x2 = Cell_Pair_First(ARG_N(2));
            y2 = Cell_Pair_Second(ARG_N(2));
        }
        break;  // delegate to pairwise operation

      default:
        break;
    }

    // !!! The only way we can generically guarantee the ability to retrigger
    // an action multiple times without it ruining its arguments is to copy
    // the FRAME!.  Technically we don't need two copies, we could reuse
    // this frame...but then the retriggering would have to be done with a
    // mechanical trick vs. the standard DO, because the frame thinks it is
    // already running...and the check for that would be subverted.

    Value* frame = Init_Frame(
        OUT,
        Varlist_Of_Level_Force_Managed(level_),
        Level_Label(level_),
        Level_Coupling(level_)
    );

    Copy_Cell(ARG_N(1), x1);
    if (x2)
        Copy_Cell(ARG_N(2), x2);  // use extracted arg x instead of pair arg
    Value* x_frame = rebValue(CANON(COPY), rebQ(frame));

    Copy_Cell(ARG_N(1), y1);
    if (y2)
        Copy_Cell(ARG_N(2), y2);  // use extracted arg y instead of pair arg
    Value* y_frame = rebValue(CANON(COPY), rebQ(frame));

    return rebValue(
        "make pair! reduce [",
            "to integer! eval @", rebR(x_frame),
            "to integer! eval @", rebR(y_frame),
        "]"
    );
}


//=//// TO CONVERSIONS ////////////////////////////////////////////////=//

IMPLEMENT_GENERIC(TO, Is_Pair)
{
    INCLUDE_PARAMS_OF_TO;

    Element* v = Element_ARG(VALUE);
    Heart to = Datatype_Builtin_Heart(ARG(TYPE));

    if (Any_List_Type(to)) {
        Source* a = Make_Source_Managed(2);
        Set_Flex_Len(a, 2);
        Copy_Cell(Array_At(a, 0), Cell_Pair_First(v));
        Copy_Cell(Array_At(a, 1), Cell_Pair_Second(v));
        return Init_Any_List(OUT, to, a);
    }

    if (Any_String_Type(to) or to == TYPE_RUNE) {
        DECLARE_MOLDER (mo);
        Push_Mold(mo);
        Mold_Element(mo, Cell_Pair_First(v));
        Append_Codepoint(mo->strand, ' ');
        Mold_Element(mo, Cell_Pair_Second(v));
        if (Any_String_Type(to))
            return Init_Any_String(OUT, to, Pop_Molded_Strand(mo));

        if (Try_Init_Small_Utf8_Untracked(
            OUT,
            to,
            cast(Utf8(const*), Binary_At(mo->strand, mo->base.size)),
            Strand_Len(mo->strand) - mo->base.index,
            Strand_Size(mo->strand) - mo->base.size
        )){
            return OUT;
        }
        Strand* s = Pop_Molded_Strand(mo);
        Freeze_Flex(s);
        return Init_Any_String(OUT, to, s);
    }

    panic (UNHANDLED);
}


IMPLEMENT_GENERIC(TWEAK_P, Is_Pair)
{
    INCLUDE_PARAMS_OF_TWEAK_P;

    Element* pair = Element_ARG(LOCATION);

    const Value* picker = ARG(PICKER);
    REBINT n = Index_From_Picker_For_Pair(pair, picker);

    Value* dual = ARG(DUAL);
    if (Not_Lifted(dual)) {
        if (Is_Dual_Nulled_Pick_Signal(dual))
            goto handle_pick;

        panic (Error_Bad_Poke_Dual_Raw(dual));
    }

    goto handle_poke;

  handle_pick: { /////////////////////////////////////////////////////////////

    if (n != 1 and n != 2)
        return DUAL_SIGNAL_NULL_ABSENT;

    Value* which = (n == 1) ? Cell_Pair_First(pair) : Cell_Pair_Second(pair);
    return DUAL_LIFTED(Copy_Cell(OUT, which));

} handle_poke: { /////////////////////////////////////////////////////////////

    Unliftify_Known_Stable(dual);

    if (Is_Antiform(dual))
        panic (Error_Bad_Antiform(dual));

    Element* poke = Known_Element(dual);

    if (not Is_Integer(poke))
        panic (PARAM(DUAL));

    Value* which = (n == 1) ? Cell_Pair_First(pair) : Cell_Pair_Second(pair);
    Copy_Cell(which, poke);

    return NO_WRITEBACK_NEEDED;  // PAIR! is two independent cells in Ren-C
}}


IMPLEMENT_GENERIC(REVERSE, Is_Pair)
{
    INCLUDE_PARAMS_OF_REVERSE;

    if (Bool_ARG(PART))
        panic (Error_Bad_Refines_Raw());

    const Element* pair = Element_ARG(SERIES);

    return Init_Pair(OUT, Cell_Pair_Y(pair), Cell_Pair_X(pair));
}


// 1. This cast to Value should not be necessary, Element should be tolerated
//    by the API.  Review.
//
IMPLEMENT_GENERIC(MULTIPLY, Is_Pair)
{
    INCLUDE_PARAMS_OF_MULTIPLY;

    Value* pair1 = ARG(VALUE1);
    Value* v2 = ARG(VALUE2);

    if (not Is_Integer(v2))
        panic (PARAM(VALUE2));

    return rebDelegate(CANON(MAKE), CANON(PAIR_X), "[",
        CANON(MULTIPLY), v2, cast(Value*, Cell_Pair_First(pair1)),  // !!! [1]
        CANON(MULTIPLY), v2, cast(Value*, Cell_Pair_Second(pair1)),
    "]");
}
