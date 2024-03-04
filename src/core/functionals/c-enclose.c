//
//  File: %c-enclose.c
//  Summary: "Mechanism for making a function that wraps another's execution"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2017-2023 Ren-C Open Source Contributors
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
// ENCLOSE gives a fully generic ability to make a function that wraps the
// execution of another.  When the enclosure is executed, a frame is built
// for the "inner" (wrapped) function--but not executed.  Then that frame is
// passed to an "outer" function, which can modify the frame arguments and
// also operate upon the result:
//
//     >> add2x3x+1: enclose :add func [f [frame!]] [
//            f.value1: f.value1 * 2
//            f.value2: f.value2 * 3
//            return 1 + eval f
//         ]
//
//     >> add2x3x+1 10 20
//     == 81  ; e.g. (10 * 2) + (20 * 3) + 1
//
// This affords significant flexibility to the "outer" function, as it can
// choose when to `EVAL F` to execute the frame... or opt to not execute it.
// Given the mechanics of FRAME!, it's also possible to COPY the frame for
// multiple invocations.
//
//     >> print2x: enclose :print func [f [frame!]] [
//            eval copy f
//            f.value: append f.value "again!"
//            eval f
//        ]
//
//     >> print2x ["Print" "me"]
//     Print me
//     Print me again!
//
// (Note: Each time you EVAL a FRAME!, the original frame becomes inaccessible,
// because its contents--the "varlist"--are stolen for function execution,
// where the function freely modifies the argument data while it runs.  If
// the frame did not expire, it would not be practically reusable.)
//
// ENCLOSE has the benefit of inheriting the interface of the function it
// wraps, and should perform better than trying to accomplish similar
// functionality manually.  It's still somewhat expensive, so if ADAPT or
// CHAIN can achieve a goal of simple pre-or-post processing then they may
// be better choices.
//

#include "sys-core.h"

enum {
    IDX_ENCLOSER_INNER = 1,  // The ACTION! being enclosed
    IDX_ENCLOSER_OUTER,  // ACTION! that gets control of inner's FRAME!
    IDX_ENCLOSER_MAX
};


//
//  Encloser_Dispatcher: C
//
// An encloser is called with a varlist that was built compatibly to invoke an
// "inner" function.  It wishes to pass this list as a FRAME! argument to an
// "outer" function, that takes only that argument.  To do this, the frame's
// varlist must thus be detached from `L` and transitioned from an "executing"
// to "non-executing" state...so that it can be used with DO.
//
// 1. The varlist is still pointed to by any extant FRAME!s.  Its keysource
//    should not be this level any longer.
//
// 2. We're passing the built context to the `outer` function as a FRAME!,
//    which that function can EVAL (or not).  But when the EVAL runs, we don't
//    want it to run the encloser again--that would be an infinite loop.
//    Update CTX_FRAME_PHASE() to point to the `inner` that was enclosed.
//
// 3. EVAL does not allow you to invoke a FRAME! that is currently running.
//    we have to clear the FRAME_HAS_BEEN_INVOKED_FLAG to allow EVLA INNER.
//
// 4. The FRAME! we're making demands that the varlist be managed to put it
//    into a cell.  It may already have been managed...but since varlists
//    aren't added to the manual series list, the bit must be tweaked vs.
//    using Force_Series_Managed().
//
// 5. Because the built FRAME! is intended to be used with DO, it must be
//    "phaseless".  The property of phaselessness allows detection of when
//    the frame should heed FRAME_HAS_BEEN_INVOKED (phased frames internal
//    to the implementation must have full visibility of locals/etc.)  Hence
//    we make a separate FRAME! in SPARE and don't use the ROOTVAR directly.
//
// 6. At one time, the encloser would leave its level on the frame with a
//    garbage varlist, that had to be handled specially when Drop_Action()
//    happened.  It's now an invariant that if a level uses Action_Executor()
//    that it must have a valid varlist, so the more efficient choice is
//    to just reuse this level for running the OUTER action.  This means
//    that you only see one stack level for both the encloser and outer.
//
// 7. The encloser frame had a label of the WORD! (or cached name) that it
//    was invoked as.  We're reusing the level to invoke OUTER, and the level
//    can only have one label in the stack.  We favor the name used for the
//    original invocation.  See [6] regarding why we don't use two levels.
//
Bounce Encloser_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_ENCLOSER_MAX);

    Value* inner = Details_At(details, IDX_ENCLOSER_INNER);
    assert(Is_Frame(inner));  // same args as f
    Value* outer = Details_At(details, IDX_ENCLOSER_OUTER);
    assert(Is_Frame(outer));  // takes 1 arg (a FRAME!)

    Array* varlist = L->varlist;
    Context* c = cast(Context*, varlist);

    L->varlist = nullptr;  // we're going to push new action in this level
    Corrupt_Pointer_If_Debug(L->rootvar);

    assert(BONUS(KeySource, varlist) == L);  // need to change keysource [1]
    INIT_BONUS_KEYSOURCE(varlist, ACT_KEYLIST(L->u.action.original));

    Element* rootvar = CTX_ROOTVAR(c);  // don't phase run encloser again [2]
    INIT_VAL_FRAME_PHASE(rootvar, ACT_IDENTITY(VAL_ACTION(inner)));
    INIT_VAL_FRAME_BINDING(rootvar, VAL_FRAME_BINDING(inner));

    assert(Get_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED));
    Clear_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED);  // [3]

    Set_Node_Managed_Bit(varlist);  // can't use Force_Series_Managed [4]

    Element* rootcopy = Copy_Cell(SPARE, rootvar);  // need phaseless copy [5]
    INIT_VAL_FRAME_PHASE_OR_LABEL(SPARE, VAL_FRAME_LABEL(inner));

    assert(Is_Level_Dispatching(L));
    Clear_Executor_Flag(ACTION, L, IN_DISPATCH);  // reuse this level [6]
    Clear_Executor_Flag(ACTION, L, RUNNING_ENFIX);

    Option(const Symbol*) original_label = L->label;
    Corrupt_Pointer_If_Debug(L->label);  // Begin_Action() requires
    Prep_Action_Level(L, outer, rootcopy);
    if (original_label)
        L->label = original_label;  // prefer original name [7]

    STATE = ST_ACTION_TYPECHECKING;

    return BOUNCE_CONTINUE;
}


//
//  enclose: native [
//
//  "Wrap code around a frame with access to its instance and return value"
//
//      return: [action?]
//      inner "Frame to be copied, then passed to OUTER"
//          [<unrun> frame!]
//      outer "Gets a FRAME! for INNER before invocation, can EVAL it (or not)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(enclose)
{
    INCLUDE_PARAMS_OF_ENCLOSE;

    Value* inner = ARG(inner);
    Value* outer = ARG(outer);

    // The new function has the same interface as `inner`
    //
    // !!! Return result may differ; similar issue comes up with CHAIN
    //
    Phase* enclosure = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(inner)),  // same interface as inner
        ACT_PARTIALS(VAL_ACTION(inner)),
        &Encloser_Dispatcher,
        IDX_ENCLOSER_MAX  // details array capacity => [inner, outer]
    );

    Details* details = Phase_Details(enclosure);
    Copy_Cell(Details_At(details, IDX_ENCLOSER_INNER), inner);
    Copy_Cell(Details_At(details, IDX_ENCLOSER_OUTER), outer);

    return Init_Action(OUT, enclosure, VAL_FRAME_LABEL(inner), UNBOUND);
}
