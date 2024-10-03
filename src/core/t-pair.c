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
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//

#include "sys-core.h"


//
//  CT_Pair: C
//
REBINT CT_Pair(const Cell* a, const Cell* b, REBINT mode)
{
    if (mode >= 0) return Cmp_Pair(a, b) == 0; // works for INTEGER=0 too (spans x y)
    if (Is_Pair(b) && 0 == VAL_INT64(b)) { // for negative? and positive?
        if (mode == -1)
            return (VAL_PAIR_X_DEC(a) >= 0 || VAL_PAIR_Y_DEC(a) >= 0); // not LT
        return (VAL_PAIR_X_DEC(a) > 0 && VAL_PAIR_Y_DEC(a) > 0); // NOT LTE
    }
    return -1;
}


//
//  MAKE_Pair: C
//
Bounce MAKE_Pair(Value* out, enum Reb_Kind kind, const Value* arg)
{
    assert(kind == REB_PAIR);
    UNUSED(kind);

    if (Is_Pair(arg))
        return Copy_Cell(out, arg);

    if (Is_Text(arg)) {
        //
        // -1234567890x-1234567890
        //
        Size size;
        Byte *bp = Analyze_String_For_Scan(&size, arg, Cell_Series_Len_At(arg));

        Erase_Cell(out);
        if (nullptr == Scan_Pair(out, bp, size))
            goto bad_make;

        return out;
    }

    const Cell* x;
    const Cell* y;

    if (Is_Block(arg)) {
        if (Cell_Series_Len_At(arg) != 2)
            goto bad_make;

        x = Cell_List_At(arg);
        y = Cell_List_At(arg) + 1;
    }
    else {
        x = arg;
        y = arg;
    }

    if (
        not (Is_Integer(x) or Is_Decimal(x))
        or not (Is_Integer(y) or Is_Decimal(y))
    ){
        goto bad_make;
    }

    return Init_Pair(out, KNOWN(x), KNOWN(y));

  bad_make:
    fail (Error_Bad_Make(REB_PAIR, arg));
}


//
//  TO_Pair: C
//
Bounce TO_Pair(Value* out, enum Reb_Kind kind, const Value* arg)
{
    return MAKE_Pair(out, kind, arg);
}


//
//  Cmp_Pair: C
//
// Given two pairs, compare them.
//
REBINT Cmp_Pair(const Cell* t1, const Cell* t2)
{
    REBDEC diff;

    if ((diff = VAL_PAIR_Y_DEC(t1) - VAL_PAIR_Y_DEC(t2)) == 0)
        diff = VAL_PAIR_X_DEC(t1) - VAL_PAIR_X_DEC(t2);
    return (diff > 0.0) ? 1 : ((diff < 0.0) ? -1 : 0);
}


//
//  Min_Max_Pair: C
//
void Min_Max_Pair(Value* out, const Value* a, const Value* b, bool maxed)
{
    // !!! This used to use REBXYF (a structure containing "X" and "Y" as
    // floats).  It's not clear why floats would be preferred here, and
    // also not clear what the types should be if they were mixed (INTEGER!
    // vs. DECIMAL! for the X or Y).  REBXYF is now a structure only used
    // in GOB! so it is taken out of mention here.

    float ax;
    float ay;
    if (Is_Pair(a)) {
        ax = VAL_PAIR_X_DEC(a);
        ay = VAL_PAIR_Y_DEC(a);
    }
    else if (Is_Integer(a))
        ax = ay = cast(REBDEC, VAL_INT64(a));
    else
        fail (Error_Invalid(a));

    float bx;
    float by;
    if (Is_Pair(b)) {
        bx = VAL_PAIR_X_DEC(b);
        by = VAL_PAIR_Y_DEC(b);
    }
    else if (Is_Integer(b))
        bx = by = cast(REBDEC, VAL_INT64(b));
    else
        fail (Error_Invalid(b));

    if (maxed)
        Init_Pair_Dec(out, MAX(ax, bx), MAX(ay, by));
    else
        Init_Pair_Dec(out, MIN(ax, bx), MIN(ay, by));
}


//
//  PD_Pair: C
//
Bounce PD_Pair(
    REBPVS *pvs,
    const Value* picker,
    const Value* opt_setval
){
    REBINT n = 0;

    if (Is_Word(picker)) {
        if (Cell_Word_Id(picker) == SYM_X)
            n = 1;
        else if (Cell_Word_Id(picker) == SYM_Y)
            n = 2;
        else
            return BOUNCE_UNHANDLED;
    }
    else if (Is_Integer(picker)) {
        n = Int32(picker);
        if (n != 1 && n != 2)
            return BOUNCE_UNHANDLED;
    }
    else
        return BOUNCE_UNHANDLED;

    if (opt_setval == nullptr) {
        if (n == 1)
            Copy_Cell(pvs->out, VAL_PAIR_FIRST(pvs->out));
        else
            Copy_Cell(pvs->out, VAL_PAIR_SECOND(pvs->out));
        return pvs->out;
    }

    // PAIR! can mechanically store any pair of values efficiently (more
    // efficiently than a 2-element block, for example).  But only INTEGER!
    // and DECIMAL! are currently allowed.
    //
    if (not Is_Integer(opt_setval) and not Is_Decimal(opt_setval))
        return BOUNCE_UNHANDLED;

    if (n == 1)
        Copy_Cell(VAL_PAIR_FIRST(pvs->out), opt_setval);
    else
        Copy_Cell(VAL_PAIR_SECOND(pvs->out), opt_setval);

    // Using BOUNCE_IMMEDIATE means that although we've updated pvs->out, we'll
    // leave it to the path dispatch to figure out if that can be written back
    // to some variable from which this pair actually originated.
    //
    // !!! Technically since pairs are pairings of values in Ren-C, there is
    // a series node which can be used to update their values, but could not
    // be used to update other things (like header bits) from an originating
    // variable.
    //
    return BOUNCE_IMMEDIATE;
}


//
//  MF_Pair: C
//
void MF_Pair(REB_MOLD *mo, const Cell* v, bool form)
{
    UNUSED(form); // currently no distinction between MOLD and FORM

    Mold_Value(mo, VAL_PAIR_FIRST(v));
    Append_Utf8_Codepoint(mo->series, 'x');
    Mold_Value(mo, VAL_PAIR_SECOND(v));
}


//
//  REBTYPE: C
//
REBTYPE(Pair)
{
    Value* v = D_ARG(1);

    Value* first1 = VAL_PAIR_FIRST(v);
    Value* second1 = VAL_PAIR_SECOND(v);

    Value* first2 = nullptr;
    Value* second2 = nullptr;

    switch (Cell_Word_Id(verb)) {
      case SYM_REVERSE:
        return Init_Pair(OUT, second1, first1);

      case SYM_ADD:
      case SYM_SUBTRACT:
      case SYM_MULTIPLY:
      case SYM_DIVIDE:
      case SYM_REMAINDER: {  // !!! Longer list?
        if (Is_Pair(D_ARG(2))) {
            first2 = VAL_PAIR_FIRST(D_ARG(2));
            second2 = VAL_PAIR_SECOND(D_ARG(2));
        }
        break;
    }

    default:  // !!! Should we limit the actions?
        break;  /* fail (Error_Illegal_Action(REB_PAIR, verb)); */
    }

   // !!! The only way we can generically guarantee the ability to retrigger
    // an action multiple times without it ruining its arguments is to copy
    // the FRAME!.  Technically we don't need two copies, we could reuse
    // this frame...but then the retriggering would have to be done with a
    // mechanical trick vs. the standard DO, because the frame thinks it is
    // already running...and the check for that would be subverted.

    Value* frame = Init_Frame(OUT, Varlist_For_Level_May_Manage(level_));

    Copy_Cell(D_ARG(1), first1);
    if (first2)
        Copy_Cell(D_ARG(2), first2);  // use extracted arg x vs pair arg
    Value* x_frame = rebValue("copy", frame);

    Copy_Cell(D_ARG(1), second1);
    if (second2)
        Copy_Cell(D_ARG(2), second2);  // use extracted arg y vs pair arg
    Value* y_frame = rebValue("copy", frame);

    Value* x = rebValue(rebEval(NAT_VALUE(do)), rebR(x_frame));
    Value* y = rebValue(rebEval(NAT_VALUE(do)), rebR(y_frame));

    Init_Pair(OUT, x, y);

    rebRelease(x);
    rebRelease(y);

    return OUT;
}
