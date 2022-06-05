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
REB_R Encloser_Dispatcher(REBFRM *f)
{
    REBFRM *frame_ = f;  // for RETURN macros

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_ENCLOSER_MAX);

    REBVAL *inner = DETAILS_AT(details, IDX_ENCLOSER_INNER);
    assert(IS_ACTION(inner));  // same args as f
    REBVAL *outer = DETAILS_AT(details, IDX_ENCLOSER_OUTER);
    assert(IS_ACTION(outer));  // takes 1 arg (a FRAME!)

    // We want to call OUTER with a FRAME! value that will dispatch to INNER
    // when (and if) it runs DO on it.  That frame is the one built for this
    // call to the encloser.  (The encloser can run the frame multiple times
    // via DO COPY of the frame if they like.)
    //
    // Since we are unplugging the varlist from the REBFRM* in which it is
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
    // So instead we make f->varlist point to a universal inaccessible array
    // and keep the varlist itself valid, so extant FRAME!s still work.  This
    // may be a bad idea, so keeping the old code on hand in case it turns
    // out to be fundamentally broken for some reason.
    //
    //-----------------------------------------------------------begin-old-code
    // REBCTX *c = Steal_Context_Vars(
    //     CTX(f->varlist),
    //     ACT_KEYLIST(FRM_PHASE(f))
    // );
    //
    // INIT_BONUS_KEYSOURCE(CTX_VARLIST(c), ACT_KEYLIST(VAL_ACTION(inner)));
    //
    // assert(GET_SERIES_FLAG(f->varlist, INACCESSIBLE));  // look dead
    //
    // // f->varlist may or may not have wound up being managed.  It was not
    // // allocated through the usual mechanisms, so if unmanaged it's not in
    // // the tracking list Init_Any_Context() expects.  Just fiddle the bit.
    // //
    // SET_SERIES_FLAG(CTX_VARLIST(c), MANAGED); */
    //-------------------------------------------------------------end-old-code

    //-----------------------------------------------------------begin-new-code
    REBARR *varlist = f->varlist;
    REBCTX *c = CTX(varlist);

    // Replace the f->varlist with a dead list.
    //
    f->varlist = &PG_Inaccessible_Series;

    // The varlist is still pointed to by any extant frames.  Its keysource
    // should not be this frame any longer.
    //
    assert(BONUS(KeySource, varlist) == f);
    INIT_BONUS_KEYSOURCE(varlist, ACT_KEYLIST(f->original));
    //-------------------------------------------------------------end-new-code

    // We're passing the built context to the `outer` function as a FRAME!,
    // which that function can DO (or not).  But when the DO runs, we don't
    // want it to run the encloser again--that would be an infinite loop.
    // Update CTX_FRAME_ACTION() to point to the `inner` that was enclosed.
    //
    REBVAL *rootvar = CTX_ROOTVAR(c);
    INIT_VAL_FRAME_PHASE(rootvar, VAL_ACTION(inner));
    INIT_VAL_FRAME_BINDING(rootvar, VAL_ACTION_BINDING(inner));

    // We want people to be able to DO the FRAME! being given back.
    //
    assert(GET_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED));
    CLEAR_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED);

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
    SET_SERIES_FLAG(varlist, MANAGED);

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
    INIT_VAL_FRAME_PHASE_OR_LABEL(SPARE, VAL_ACTION_LABEL(inner));

    if (rebRunCoreThrows(
        OUT,  // <-- output cell
        EVAL_MASK_DEFAULT | EVAL_FLAG_MAYBE_STALE | EVAL_FLAG_NO_RESIDUE,
        outer, rootcopy
    )){
        return_thrown (OUT);
    }

    if (Is_Stale(OUT))
        return_void (OUT);

    return OUT;
}


//
//  enclose*: native [
//
//  {Wrap code around an ACTION! with access to its FRAME! and return value}
//
//      return: [action!]
//      inner "Action that a FRAME! will be built for, then passed to OUTER"
//          [action!]
//      outer "Gets a FRAME! for INNER before invocation, can DO it (or not)"
//          [action!]
//  ]
//
REBNATIVE(enclose_p)  // see extended definition ENCLOSE in %base-defs.r
{
    INCLUDE_PARAMS_OF_ENCLOSE_P;

    REBVAL *inner = ARG(inner);
    REBVAL *outer = ARG(outer);

    // The new function has the same interface as `inner`
    //
    // !!! Return result may differ; similar issue comes up with CHAIN
    //
    REBACT *enclosure = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(inner)),  // same interface as inner
        ACT_PARTIALS(VAL_ACTION(inner)),
        &Encloser_Dispatcher,
        IDX_ENCLOSER_MAX  // details array capacity => [inner, outer]
    );

    REBARR *details = ACT_DETAILS(enclosure);
    Copy_Cell(ARR_AT(details, IDX_ENCLOSER_INNER), inner);
    Copy_Cell(ARR_AT(details, IDX_ENCLOSER_OUTER), outer);

    return Init_Action(OUT, enclosure, VAL_ACTION_LABEL(inner), UNBOUND);
}
