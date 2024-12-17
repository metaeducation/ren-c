//
//  File: %c-adapt.c
//  Summary: "Function generator injecting code block before running another"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2022 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the GNU Lesser General Public License (LGPL), Version 3.0.
// You may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.en.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The ADAPT operation is an efficient way to create a variation of a function
// that does some amount of pre-processing (which can include modifying the
// arguments), before the original implementation is called:
//
//     >> /ap1: adapt append/ [if integer? value [value: value + 716]]
//
//     >> ap1 [a b c] 304
//     == [a b c 1020]
//
// What makes it efficient is that the adapted function operates on the same
// function frame as what it is adapting.  It does--however--need to run a
// type check on any modified arguments before passing control to the original
// "adaptee", as failure to do so could pass bad bit patterns to natives
// and lead to crashes.
//
//    >> /bad-negate: adapt negate/ [number: to text! number]
//
//    >> bad-negate 1020
//    ** Error: Internal phase disallows TEXT! for its `number` argument
//
// More complete control of execution and manipulating the return result is
// possible with the ENCLOSE operation, but at a greater performance cost.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * The adapted action's RETURN slot--if it has one--will not be filled when
//   the prelude runs.  If you want a way to return from the prelude but still
//   run the body, you can use CATCH + THROW.  But there's no way to avoid
//   running the body at all with current ADAPT, so use ENCLOSE instead if you
//   need to make such a decision.
//


#include "sys-core.h"

enum {
    IDX_ADAPTER_PRELUDE = 1,  // Relativized block to run before Adaptee
    /* IDX_ADAPTER_ADAPTEE, */  // Adaptee is implicit (Details rootvar)
    IDX_ADAPTER_MAX
};


//
//  Adapter_Dispatcher: C
//
// Each time a function created with ADAPT is executed, this code runs to
// invoke the "prelude" before passing control to the "adaptee" function.
//
Bounce Adapter_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == IDX_ADAPTER_MAX);

    enum {
        ST_ADAPTER_INITIAL_ENTRY = STATE_0,
        ST_ADAPTER_RUNNING_PRELUDE
    };

    switch (STATE) {
      case ST_ADAPTER_INITIAL_ENTRY: goto initial_entry;
      case ST_ADAPTER_RUNNING_PRELUDE: goto run_adaptee_in_same_frame;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Value* prelude = Details_At(details, IDX_ADAPTER_PRELUDE);
    assert(Is_Block(prelude) and VAL_INDEX(prelude) == 0);

    STATE = ST_ADAPTER_RUNNING_PRELUDE;  // no definitional RETURN [1]

    Force_Level_Varlist_Managed(L);

    Copy_Cell(SPARE, prelude);
    node_LINK(NextVirtual, L->varlist) = BINDING(prelude);
    BINDING(SPARE) = L->varlist;

    return CONTINUE_CORE(  // Note: we won't catch throws or errors
        OUT,  // result discarded [1]
        LEVEL_MASK_NONE,  // plain result
        SPECIFIED,
        stable_SPARE
    );

} run_adaptee_in_same_frame: {  //////////////////////////////////////////////

    // 1. We want to run the adapted function in the same frame, but prelude
    //    may have put invalid types in parameter slots.  So it needs to be
    //    typechecked before executing.

    Value* adaptee = Phase_Archetype(details);

    Tweak_Level_Phase(L, Cell_Frame_Phase(adaptee));
    Tweak_Level_Coupling(L, Cell_Frame_Coupling(adaptee));

    return BOUNCE_REDO_CHECKED;  // redo uses updated phase & coupling [1]
}}


//
//  Adapter_Details_Querier: C
//
bool Adapter_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Adapter_Dispatcher);
    assert(Details_Max(details) == IDX_ADAPTER_MAX);

    switch (property) {
      case SYM_RETURN: {
        Value* adaptee = Phase_Archetype(details);
        Details* adaptee_details = Phase_Details(Cell_Frame_Phase(adaptee));
        DetailsQuerier* querier = Details_Querier(adaptee_details);
        return (*querier)(out, adaptee_details, SYM_RETURN); }

      default:
        break;
    }

    return false;
}


//
//  /adapt: native [
//
//  "Create a variant of an action that preprocesses its arguments"
//
//      return: [action?]
//      original "Code to be run after the prelude is complete"
//          [<unrun> frame!]
//      prelude "Code to run in constructed frame before adaptee runs"
//          [block!]
//  ]
//
DECLARE_NATIVE(adapt)
//
// 1. !!! We could probably make ADAPT cheaper, if the full cell of the
//    adaptee were put in the Details[0] slot.  Then the Details array
//    would only need to hold the prelude in Details[1].  Review.
//
// 2. As with FUNC, we deep copy the block the user gives us.  Perhaps this
//    should be optional...but so long as we are copying it, we might as well
//    mutably bind it.
{
    INCLUDE_PARAMS_OF_ADAPT;

    Value* adaptee = ARG(original);
    Value* prelude = ARG(prelude);

    Details* details = Make_Dispatch_Details(
        DETAILS_MASK_NONE,
        adaptee,  // same parameters as adaptee [1]
        &Adapter_Dispatcher,
        IDX_ADAPTER_MAX  // details array capacity => [prelude, adaptee]
    );

    Source* prelude_copy = Copy_And_Bind_Relative_Deep_Managed(  // copy [2]
        prelude,
        details,
        LENS_MODE_INPUTS  // adapted code should not see adaptee locals
    );

    Element* rebound = Init_Block(
        Array_At(details, IDX_ADAPTER_PRELUDE),
        prelude_copy
    );
    BINDING(rebound) = Cell_List_Binding(prelude);

    return Init_Action(OUT, details, Cell_Frame_Label_Deep(adaptee), UNBOUND);
}
