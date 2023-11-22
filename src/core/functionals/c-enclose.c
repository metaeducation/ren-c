//
//  File: %c-enclose.c
//  Summary: "Mechanism for making a function that wraps another's execution"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2017-2020 Ren-C Open Source Contributors
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
//            return 1 + do f
//         ]
//
//     >> add2x3x+1 10 20
//     == 81  ; e.g. (10 * 2) + (20 * 3) + 1
//
// This affords significant flexibility to the "outer" function, as it can
// choose when to `DO F` to execute the frame... or opt to not execute it.
// Given the mechanics of FRAME!, it's also possible to COPY the frame for
// multiple invocations.
//
//     >> print2x: enclose :print func [f [frame!]] [
//            do copy f
//            f.value: append f.value "again!"
//            do f
//        ]
//
//     >> print2x ["Print" "me"]
//     Print me
//     Print me again!
//
// (Note: Each time you DO a FRAME!, the original frame becomes inaccessible,
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
// An encloser is called with a frame that was built compatibly to invoke an
// "inner" function.  It wishes to pass this frame as an argument to an
// "outer" function, that takes only that argument.  To do this, the frame's
// varlist must thus be detached from `f` and transitioned from an "executing"
// to "non-executing" state...so that it can be used with DO.
//
// Note: Not static because it's checked for by pointer in RESKIN.
//
Bounce Encloser_Dispatcher(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_ENCLOSER_MAX);

    REBVAL *inner = DETAILS_AT(details, IDX_ENCLOSER_INNER);
    assert(Is_Frame(inner));  // same args as f
    REBVAL *outer = DETAILS_AT(details, IDX_ENCLOSER_OUTER);
    assert(Is_Frame(outer));  // takes 1 arg (a FRAME!)

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when (and if) it runs DO on it.  That frame is the one built for this
    // call to the encloser.  (The encloser can run the frame multiple times
    // via DO COPY of the frame if they like.)
    //
    // Since we are unplugging the varlist from the Level* in which it is
    // running, we at one time would actually `Steal_Context_Vars()` on it...
    // which would mean all outstanding FRAME! that had been pointing at
    // the varlist would go stale.  This hampered tricks like:
    //
    //     f: func [x /augmented [frame!]] [
    //        reduce [x if augmented [augmented.y]]
    //     ]
    //
    //     a: adapt augment :f [y] [augmented: binding of 'y]
    //
    //     >> f 10
    //     == [10]
    //
    //     >> a 10 20
    //     == [10 20]
    //
    // So instead we make L->varlist point to a universal inaccessible array
    // and keep the varlist itself valid, so extant FRAME!s still work.  This
    // may be a bad idea, so keeping the old code on hand in case it turns
    // out to be fundamentally broken for some reason.
    //
    //-----------------------------------------------------------begin-old-code
    // Context* c = Steal_Context_Vars(
    //     cast(Context*, L->varlist),
    //     ACT_KEYLIST(Level_Phase(L))
    // );
    //
    // INIT_BONUS_KEYSOURCE(CTX_VARLIST(c), ACT_KEYLIST(VAL_ACTION(inner)));
    //
    // assert(Get_Series_Flag(L->varlist, INACCESSIBLE));  // look dead
    //
    // // L->varlist may or may not have wound up being managed.  It was not
    // // allocated through the usual mechanisms, so if unmanaged it's not in
    // // the tracking list Init_Context_Cell() expects.  Just fiddle the bit.
    // //
    // Set_Node_Managed_Bit(CTX_VARLIST(c)); */
    //-------------------------------------------------------------end-old-code

    //-----------------------------------------------------------begin-new-code
    Array* varlist = L->varlist;
    Context* c = cast(Context*, varlist);

    // Replace the L->varlist with a dead list.
    //
    L->varlist = &PG_Inaccessible_Series;

    // The varlist is still pointed to by any extant frames.  Its keysource
    // should not be this frame any longer.
    //
    assert(BONUS(KeySource, varlist) == L);
    INIT_BONUS_KEYSOURCE(varlist, ACT_KEYLIST(L->u.action.original));
    //-------------------------------------------------------------end-new-code

    // We're passing the built context to the `outer` function as a FRAME!,
    // which that function can DO (or not).  But when the DO runs, we don't
    // want it to run the encloser again--that would be an infinite loop.
    // Update CTX_FRAME_PHASE() to point to the `inner` that was enclosed.
    //
    REBVAL *rootvar = CTX_ROOTVAR(c);
    INIT_VAL_FRAME_PHASE(rootvar, ACT_IDENTITY(VAL_ACTION(inner)));
    INIT_VAL_FRAME_BINDING(rootvar, VAL_FRAME_BINDING(inner));

    // We want people to be able to DO the FRAME! being given back.
    //
    assert(Get_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED));
    Clear_Subclass_Flag(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED);

    // We don't actually know how long the frame we give back is going to
    // live, or who it might be given to.  And it may contain things like
    // bindings in a RETURN or a VARARGS! which are to the old varlist, which
    // may not be managed...and so when it goes off the stack it might try
    // and think that since nothing managed it then it can be freed.  Go
    // ahead and mark it managed--even though it's dead--so that returning
    // won't free it if there are outstanding references.
    //
    // Note that since varlists aren't added to the manual series list, the
    // bit must be tweaked vs. using Force_Series_Managed.
    //
    Set_Node_Managed_Bit(varlist);

    // Because the built context is intended to be used with DO, it must be
    // "phaseless".  The property of phaselessness allows detection of when
    // the frame should heed FRAME_HAS_BEEN_INVOKED (phased frames internal
    // to the implementation must have full visibility of locals/etc.)
    //
    // !!! A bug was observed here in the stackless build that required a
    // copy instead of using the archetype.  However, the "phaseless"
    // requirement for DO was introduced since...suggesting the copy would
    // be needed regardless.  Be attentive should this ever be switched to
    // try and use CTX_ARCHETYPE() directly to GC issues.
    //
    REBVAL *rootcopy = Copy_Cell(SPARE, rootvar);
    INIT_VAL_FRAME_PHASE_OR_LABEL(SPARE, VAL_FRAME_LABEL(inner));

    return DELEGATE(OUT, outer, rootcopy);
}


//
//  enclose*: native [
//
//  {Wrap code around a frame with access to its instance and return value}
//
//      return: [activation!]
//      inner "Frame to be copied, then passed to OUTER"
//          [<unrun> frame!]
//      outer "Gets a FRAME! for INNER before invocation, can DO it (or not)"
//          [<unrun> frame!]
//  ]
//
DECLARE_NATIVE(enclose_p)  // see extended definition ENCLOSE in %base-defs.r
{
    INCLUDE_PARAMS_OF_ENCLOSE_P;

    Value(*) inner = ARG(inner);
    Value(*) outer = ARG(outer);

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
    Copy_Cell(DETAILS_AT(details, IDX_ENCLOSER_INNER), inner);
    Copy_Cell(DETAILS_AT(details, IDX_ENCLOSER_OUTER), outer);

    return Init_Activation(OUT, enclosure, VAL_FRAME_LABEL(inner), UNBOUND);
}
