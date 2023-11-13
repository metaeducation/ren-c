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
//     >> ap1: adapt :append [if integer? :value [value: value + 716]]
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
//    >> negbad: adapt :negate [number: to text! number]
//
//    >> negbad 1020
//    ** Error: Internal phase disallows TEXT! for its `number` argument
//
// More complete control of execution and manipulating the return result is
// possible with the ENCLOSE operation, but at a greater performance cost.
//

#include "sys-core.h"

enum {
    IDX_ADAPTER_PRELUDE = 1,  // Relativized block to run before Adaptee
    IDX_ADAPTER_ADAPTEE,  // The ACTION! being adapted
    IDX_ADAPTER_MAX
};


//
//  Adapter_Dispatcher: C
//
// Each time a function created with ADAPT is executed, this code runs to
// invoke the "prelude" before passing control to the "adaptee" function.
//
Bounce Adapter_Dispatcher(Frame(*) f)
//
// 1. When an ADAPT is done, it does not leave its product in the output
//    cell.  This means ADAPT of COMMENT will still be invisible.
//
// 2. The adapted action's RETURN slot--if it has one--will not be filled when
//    the prelude runs.  It would also be somewhat ambiguous what RETURN
//    would mean (Return from the prelude but still run the body?  Don't run
//    the body at all?)  ENCLOSE should be used for these complex intents.
//
// 3. We want to run the adapted function in the same frame, but the prelude
//    may have put invalid types in parameter slots.  So it needs to be
//    typechecked before executing.
{
    Frame(*) frame_ = f;  // for RETURN macros

    Phase(*) phase = FRM_PHASE(f);
    Details(*) details = ACT_DETAILS(phase);
    assert(ARR_LEN(details) == IDX_ADAPTER_MAX);

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

    Cell(*) prelude = ARR_AT(details, IDX_ADAPTER_PRELUDE);  // code to run
    assert(
        IS_BLOCK(prelude)
        and IS_RELATIVE(prelude)
        and VAL_INDEX(prelude) == 0
    );

    STATE = ST_ADAPTER_RUNNING_PRELUDE;  // no definitional RETURN, see [2]

    return CONTINUE_CORE(  // Note: we won't catch throws or errors
        SPARE,  // Evaluate prelude into SPARE cell (result discarded, see [1])
        FRAME_MASK_NONE,  // plain result
        SPC(f->varlist), prelude
    );

} run_adaptee_in_same_frame: {  //////////////////////////////////////////////

    REBVAL* adaptee = DETAILS_AT(details, IDX_ADAPTER_ADAPTEE);

    INIT_FRM_PHASE(f, ACT_IDENTITY(VAL_ACTION(adaptee)));
    INIT_FRM_BINDING(f, VAL_FRAME_BINDING(adaptee));

    return BOUNCE_REDO_CHECKED;  // redo uses updated phase & binding, see [3]
}}


//
//  adapt*: native [
//
//  {Create a variant of an action that preprocesses its arguments}
//
//      return: [activation!]
//      original "Code to be run after the prelude is complete"
//          [<unrun> frame!]
//      prelude "Code to run in constructed frame before adaptee runs"
//          [block!]
//  ]
//
DECLARE_NATIVE(adapt_p)  // see extended definition ADAPT in %base-defs.r
{
    INCLUDE_PARAMS_OF_ADAPT_P;

    REBVAL *adaptee = ARG(original);

    // !!! There was code here which would hide it so adapted code had no
    // access to the locals.  That requires creating a new paramlist.  Is
    // there a better way to do that with phasing?

    Phase(*) adaptation = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(adaptee)),  // reuse partials/exemplar/etc.
        ACT_PARTIALS(VAL_ACTION(adaptee)),
        &Adapter_Dispatcher,
        IDX_ADAPTER_MAX  // details array capacity => [prelude, adaptee]
    );

    // !!! As with FUNC, we copy and bind the block the user gives us.  This
    // means we will not see updates to it.  So long as we are copying it,
    // we might as well mutably bind it--there's no incentive to virtual
    // bind things that are copied.
    //
    Array(*) prelude = Copy_And_Bind_Relative_Deep_Managed(
        ARG(prelude),
        adaptation,
        VAR_VISIBILITY_INPUTS
    );

    // We can't use a simple Init_Block() here, because the prelude has been
    // relativized.  It is thus not a REBVAL*, but a Cell(*)...so the
    // Adapter_Dispatcher() must combine it with the FRAME! instance before
    // it can be executed (e.g. the `Frame(*) f` it is dispatching).
    //
    Details(*) details = ACT_DETAILS(adaptation);
    Init_Relative_Block(
        ARR_AT(details, IDX_ADAPTER_PRELUDE),
        adaptation,
        prelude
    );
    Copy_Cell(DETAILS_AT(details, IDX_ADAPTER_ADAPTEE), adaptee);

    return Init_Activation(OUT, adaptation, VAL_FRAME_LABEL(adaptee), UNBOUND);
}
