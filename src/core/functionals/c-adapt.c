//
//  file: %c-adapt.c
//  summary: "Function generator injecting code block before running another"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
//     >> ap1: adapt append/ [if integer? value [value: value + 716]]
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
//    >> bad-negate: adapt negate/ [value: to text! value]
//
//    >> bad-negate 1020
//    ** Error: Internal phase disallows TEXT! for its `value` argument
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
    MAX_IDX_ADAPTER = IDX_ADAPTER_PRELUDE
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
    assert(Details_Max(details) == MAX_IDX_ADAPTER);

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

  // 1. When we put ParamList directly into a binding chain, that's just one
  //    pointer, vs. a full FRAME! Cell.  Since there is no Lens, it has to
  //    act as not only the data but also the Lens.  This "self-lensing"
  //    implies showing *all* the fields.
  //
  //    An ADAPT-ed function shouldn't have that much privilege.  Instead it
  //    wants to have a Lens that shows only the inputs to the adaptee to the
  //    prelude code.  That's done with "non-self-lensed" ParamList lenses.
  //
  //    We have to put a Use Stub into the binding chain to specify a Lens,
  //    by means of using a full FRAME! Cell.

    Element* prelude = Details_Element_At(details, IDX_ADAPTER_PRELUDE);
    assert(Is_Block(prelude) and Series_Index(prelude) == 0);

    STATE = ST_ADAPTER_RUNNING_PRELUDE;

    assert(not Link_Inherit_Bind_Raw(L->varlist));  // can't own [1]

    Force_Level_Varlist_Managed(L);

    require (
      Use* use = Alloc_Use_Inherits(Cell_Binding(prelude))
    );
    Lens* lens = Lens_Inputs(details);  // from adaptee
    Init_Frame(
        Stub_Cell(use),
        Level_Varlist(L),  // varlist != lens
        lens,  // non-self-ParamList lens: only adaptee's inputs visible [1]
        Level_Coupling(L)
    );

    Element* spare = Copy_Cell(SPARE, prelude);
    Tweak_Cell_Binding(spare, use);  // must USE [1]

    return CONTINUE_CORE(  // Note: we won't catch throws or errors
        OUT,  // note: result is discarded
        LEVEL_MASK_NONE,  // plain result (warning if it was an error)
        SPECIFIED,
        spare
    );

} run_adaptee_in_same_frame: {  //////////////////////////////////////////////

    // 1. We want to run the adapted function in the same frame, but prelude
    //    may have put invalid types in parameter slots.  So it needs to be
    //    typechecked before executing.

    Stable* adaptee = Phase_Archetype(details);

  require (
    Tweak_Level_Phase(L, Frame_Phase(adaptee))
  );

    Tweak_Level_Coupling(L, Frame_Coupling(adaptee));

    return BOUNCE_REDO_CHECKED;  // redo uses updated phase & coupling [1]
}}


//
//  Adapter_Details_Querier: C
//
bool Adapter_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Adapter_Dispatcher);
    assert(Details_Max(details) == MAX_IDX_ADAPTER);

    switch (property) {
      case SYM_RETURN_OF: {
        Stable* adaptee = Phase_Archetype(details);
        Details* adaptee_details = unwrap Phase_Details(Frame_Phase(adaptee));
        DetailsQuerier* querier = Details_Querier(adaptee_details);
        return (*querier)(out, adaptee_details, SYM_RETURN_OF); }

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
//      return: [action! frame!]
//      ^original "Code to be run after the prelude is complete"
//          [action! frame!]
//      prelude "Code to run in constructed frame before adaptee runs"
//          [block!]
//  ]
//
DECLARE_NATIVE(ADAPT)
//
// 1. The adaptee is in the Details[0] slot, so we don't need a separate
//    place to put it.
//
// 2. We copy the given block shallowly (should we?)
{
    INCLUDE_PARAMS_OF_ADAPT;

    Value* adaptee = ARG(ORIGINAL);
    Element* prelude = Element_ARG(PRELUDE);

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        adaptee,  // same parameters as adaptee [1]
        &Adapter_Dispatcher,
        MAX_IDX_ADAPTER
    );

    Source* prelude_copy = Copy_Array_At_Shallow(  // copy [2]
        Cell_Array(prelude),
        Series_Index(prelude)
    );

    Element* rebound = Init_Block(
        Details_At(details, IDX_ADAPTER_PRELUDE),
        prelude_copy
    );
    Tweak_Cell_Binding(rebound, List_Binding(prelude));

    Init_Frame(OUT, details, Frame_Label_Deep(adaptee), UNCOUPLED);
    Copy_Vanishability(OUT, adaptee);
    Proxy_Frame_Activation(OUT, adaptee);

    return BOUNCE_OUT;
}
