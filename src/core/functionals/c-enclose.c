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
//     >> /add2x3x+1: enclose add/ func [f [frame!]] [
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
//     >> /print2x: enclose print/ func [f [frame!]] [
//            eval copy f
//            f.value: append f.value "again!"
//            eval f
//        ]
//
//     >> print2x ["Print" "me"]
//     Print me
//     Print me again!
//
// Note: Each time you EVAL a FRAME!, it makes a copy instead of using the
// frame's variables for the execution to destructively use.  Hence you can
// still access the fields of the input frame after the EVAL.  If you wish,
// you can FREE the frame after the EVAL to reclaim memory faster than GC.
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
//    Update Paramlist_Archetype_Phase() to point to the `inner` that was enclosed.
//
// 3. EVAL does not allow you to invoke a FRAME! that is currently running.
//    we have to clear the FRAME_HAS_BEEN_INVOKED_FLAG to allow EVAL INNER.
//
// 4. The FRAME! we're making demands that the varlist be managed to put it
//    into a cell.  It may already have been managed...but since varlists
//    aren't added to the manual Flex list, the bit must be tweaked vs.
//    using Force_Flex_Managed().
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

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == IDX_ENCLOSER_MAX);

    Value* inner = Details_At(details, IDX_ENCLOSER_INNER);
    assert(Is_Frame(inner));  // same args as f
    Value* outer = Details_At(details, IDX_ENCLOSER_OUTER);
    assert(Is_Frame(outer));  // takes 1 arg (a FRAME!)

    VarList* varlist = Varlist_Of_Level_Maybe_Unmanaged(L);

    L->varlist = nullptr;  // we're going to push new action in this level
    Corrupt_Pointer_If_Debug(L->rootvar);

    assert(MISC(RunLevel, varlist) == L);  // need to change runlevel [1]
    MISC(VarlistAdjunct, varlist) = nullptr;
    Set_Stub_Flag(varlist, MISC_NODE_NEEDS_MARK);

    Element* rootvar = Rootvar_Of_Varlist(varlist);  // no more encloser [2]
    Tweak_Cell_Frame_Phase(rootvar, Phase_Details(VAL_ACTION(inner)));
    Tweak_Cell_Frame_Coupling(rootvar, Cell_Frame_Coupling(inner));

    assert(Get_Flavor_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED));
    Clear_Flavor_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED);  // [3]

    possibly(Is_Node_Managed(varlist));
    Set_Node_Managed_Bit(varlist);  // can't use Force_Flex_Managed [4]

    Element* rootcopy = Copy_Cell(SPARE, rootvar);  // need phaseless copy [5]
    Tweak_Cell_Frame_Phase_Or_Label(SPARE, Cell_Frame_Label(inner));

    assert(Is_Level_Dispatching(L));
    Clear_Executor_Flag(ACTION, L, IN_DISPATCH);  // reuse this level [6]
    Set_Level_Infix_Mode(L, PREFIX_0);  // clear out for reuse...?

    Option(const Symbol*) original_label = L->u.action.label;
    Corrupt_Pointer_If_Debug(L->u.action.label);
  #if DEBUG_LEVEL_LABELS
    L->label_utf8 = nullptr;  // Begin_Action() requires
  #endif
    Prep_Action_Level(L, outer, rootcopy);
    if (original_label) {
        Corrupt_Pointer_If_Debug(L->u.action.label);
      #if DEBUG_LEVEL_LABELS
        L->label_utf8 = nullptr;
      #endif
        Set_Action_Level_Label(L, original_label);  // prefer original name [7]
    }

    STATE = ST_ACTION_TYPECHECKING;

    return BOUNCE_CONTINUE;
}


//
//  /enclose: native [
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
//
// 1. The ENCLOSE'd function will have the same arguments and refinements, so
//    it can reuse the interface of INNER.  But note that the return result
//    will be typed according to the result of OUTER.
{
    INCLUDE_PARAMS_OF_ENCLOSE;

    Value* inner = ARG(inner);
    Value* outer = ARG(outer);

    Details* details = Make_Dispatch_Details(
        Phase_Paramlist(VAL_ACTION(inner)),  // same interface as inner [1]
        &Encloser_Dispatcher,
        IDX_ENCLOSER_MAX  // details array capacity => [inner, outer]
    );

    Copy_Cell(Details_At(details, IDX_ENCLOSER_INNER), inner);
    Copy_Cell(Details_At(details, IDX_ENCLOSER_OUTER), outer);

    return Init_Action(OUT, details, Cell_Frame_Label(inner), UNBOUND);
}
