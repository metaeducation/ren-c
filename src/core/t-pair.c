//
//  File: %t-pair.c
//  Summary: "pair datatype"
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
// See %sys-pair.h for explanation.
//


#include "sys-core.h"


//
//  CT_Pair: C
//
REBINT CT_Pair(const Cell* a, const Cell* b, bool strict)
{
    UNUSED(strict);  // !!! Should this be heeded for the decimal?

    REBI64 diff;

    if ((diff = VAL_PAIR_Y_INT(a) - VAL_PAIR_Y_INT(b)) == 0)
        diff = VAL_PAIR_X_INT(a) - VAL_PAIR_X_INT(b);
    return (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
}


//
//  MAKE_Pair: C
//
Bounce MAKE_Pair(
    Level* level_,
    Kind kind,
    Option(const Value*) parent,
    const Value* arg
){
    assert(kind == REB_PAIR);
    if (parent)
        return RAISE(Error_Bad_Make_Parent(kind, unwrap parent));

    if (Is_Pair(arg))
        return Copy_Cell(OUT, arg);

    if (Is_Text(arg)) {
        //
        // -1234567890x-1234567890
        //
        Size size;
        const Byte* bp
            = Analyze_String_For_Scan(&size, arg, Cell_Series_Len_At(arg));
        const Byte* ep;

        if (not (ep = maybe Try_Scan_Pair_To_Stack(bp, size)))
            goto bad_make;
        UNUSED(ep);  // !!! don't check?
        return Move_Drop_Top_Stack_Element(OUT);
    }

    const Cell* x;
    const Cell* y;

    if (Is_Integer(arg)) {
        x = arg;
        y = arg;
    }
    else if (Is_Block(arg)) {
        const Element* tail;
        const Element* item = Cell_List_At(&tail, arg);

        if (Is_Integer(item))
            x = item;
        else
            goto bad_make;

        ++item;
        if (item == tail)
            goto bad_make;

        if (Is_Integer(item))
            y = item;
        else
            goto bad_make;

        ++item;
        if (item != tail)
            goto bad_make;
    }
    else
        goto bad_make;

    return Init_Pair_Int(OUT, VAL_INT64(x), VAL_INT64(y));

  bad_make:

    return RAISE(Error_Bad_Make(REB_PAIR, arg));
}


//
//  TO_Pair: C
//
Bounce TO_Pair(Level* level_, Kind kind, const Value* arg)
{
    return MAKE_Pair(level_, kind, nullptr, arg);
}


//
//  Min_Max_Pair: C
//
void Min_Max_Pair(
    Sink(Value*) out,
    const Value* a,
    const Value* b,
    bool maxed
){
    REBI64 x;
    if (VAL_PAIR_X_INT(a) > VAL_PAIR_X_INT(b))
        x = maxed ? VAL_PAIR_X_INT(a) : VAL_PAIR_X_INT(b);
    else
        x = maxed ? VAL_PAIR_X_INT(b) : VAL_PAIR_X_INT(a);

    REBI64 y;
    if (VAL_PAIR_Y_INT(a) > VAL_PAIR_Y_INT(b))
        y = maxed ? VAL_PAIR_Y_INT(a) : VAL_PAIR_Y_INT(b);
    else
        y = maxed ? VAL_PAIR_Y_INT(b) : VAL_PAIR_Y_INT(a);

    Init_Pair_Int(out, x, y);
}


//
//  MF_Pair: C
//
void MF_Pair(REB_MOLD *mo, const Cell* v, bool form)
{
    Mold_Or_Form_Element(mo, VAL_PAIR_X(v), form);

    Append_Codepoint(mo->string, 'x');

    Mold_Or_Form_Element(mo, VAL_PAIR_Y(v), form);
}


REBINT Index_From_Picker_For_Pair(
    const Value* pair,
    const Value* picker
){
    UNUSED(pair); // Might the picker be pair-sensitive?

    REBINT n;
    if (Is_Word(picker)) {
        if (Cell_Word_Id(picker) == SYM_X)
            n = 1;
        else if (Cell_Word_Id(picker) == SYM_Y)
            n = 2;
        else
            fail (picker);
    }
    else if (Is_Integer(picker)) {
        n = Int32(picker);
        if (n != 1 and n != 2)
            fail (picker);
    }
    else
        fail (picker);

    return n;
}


//
//  REBTYPE: C
//
// !!! R3-Alpha turned all the PAIR! operations from integer to decimal, but
// they had floating point precision (otherwise you couldn't fit a full cell
// for two values into a single cell).  This meant they were neither INTEGER!
// nor DECIMAL!.  Ren-C stepped away from this idea of introducing a new
// numeric type and instead created a more compact "Pairing" that could fit
// in a single STUB_POOL node and hold two arbitrary values.
//
// With the exception of operations that are specifically pair-aware (e.g.
// REVERSE swapping X and Y), this chains to retrigger the action onto the
// pair elements and then return a pair made of that.
//
REBTYPE(Pair)
{
    Value* v = D_ARG(1);

    Value* x1 = VAL_PAIR_X(v);
    Value* y1 = VAL_PAIR_Y(v);

    Value* x2 = nullptr;
    Value* y2 = nullptr;

    switch (Symbol_Id(verb)) {

    //=//// PICK* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_PICK_P: {
        INCLUDE_PARAMS_OF_PICK_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n = Index_From_Picker_For_Pair(v, picker);
        const Value* which = (n == 1) ? VAL_PAIR_X(v) : VAL_PAIR_Y(v);

        return Copy_Cell(OUT, which); }


    //=//// POKE* (see %sys-pick.h for explanation) ////////////////////////=//

      case SYM_POKE_P: {
        INCLUDE_PARAMS_OF_POKE_P;
        UNUSED(ARG(location));

        const Value* picker = ARG(picker);
        REBINT n = Index_From_Picker_For_Pair(v, picker);

        Value* setval = ARG(value);

        if (not Is_Integer(setval))
            fail (PARAM(value));

        Value* which = (n == 1) ? VAL_PAIR_X(v) : VAL_PAIR_Y(v);
        Copy_Cell(which, setval);

        return nullptr; }


      case SYM_REVERSE:
        return Init_Pair_Int(OUT, VAL_PAIR_Y_INT(v), VAL_PAIR_X_INT(v));

      case SYM_ADD:
      case SYM_SUBTRACT:
      case SYM_DIVIDE:
      case SYM_MULTIPLY:
        if (Is_Pair(D_ARG(2))) {
            x2 = VAL_PAIR_X(D_ARG(2));
            y2 = VAL_PAIR_Y(D_ARG(2));
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
        Level_Label(level_)
    );

    Copy_Cell(D_ARG(1), x1);
    if (x2)
        Copy_Cell(D_ARG(2), x2);  // use extracted arg x instead of pair arg
    Value* x_frame = rebValue(Canon(COPY), rebQ(frame));

    Copy_Cell(D_ARG(1), y1);
    if (y2)
        Copy_Cell(D_ARG(2), y2);  // use extracted arg y instead of pair arg
    Value* y_frame = rebValue(Canon(COPY), rebQ(frame));

    return rebValue(
        "make pair! reduce [",
            "to integer! eval @", rebR(x_frame),
            "to integer! eval @", rebR(y_frame),
        "]"
    );
}
