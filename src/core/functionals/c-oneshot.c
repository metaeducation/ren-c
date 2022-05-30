//
//  File: %c-oneshot.c
//  Summary: "Generates function that will run code N times, then return null"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2020 Ren-C Open Source Contributors
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
// The N-SHOT is a somewhat fanciful generalization of ONESHOT, which is the
// idea of making a code block executor that will run code once and then
// return NULL every time thereafter:
//
//     >> once: oneshot
//
//     >> once [5 + 5]
//     == 10
//
//     >> once [5 + 5]
//     ; null
//
//     >> once [5 + 5]
//     ; null
//
// !!! This experiment predates "stackless" and generators, which would make
// it easy to create this via a counter state and YIELD, ultimately ending the
// generator and returning NULL.  So it's somewhat redundant, though much
// more efficient than a usermode generator.  Review whether it is worth it to
// keep in the core.
//

#include "sys-core.h"

enum {
    IDX_ONESHOT_COUNTER = 1,  // Count that is going down to 0
    IDX_ONESHOT_MAX
};


REB_R Downshot_Dispatcher(REBFRM *f)  // runs until count is reached
{
    REBFRM *frame_ = f;  // for RETURN macros

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_ONESHOT_MAX);

    RELVAL *n = DETAILS_AT(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT64(n) == 0)
        return nullptr;  // always return null once 0 is reached
    --VAL_INT64(n);

    REBVAL *code = FRM_ARG(f, 2);  // skip the RETURN
    if (Do_Branch_Throws(OUT, code))
        return_thrown (OUT);

    return OUT;
}


REB_R Upshot_Dispatcher(REBFRM *f)  // won't run until count is reached
{
    REBFRM *frame_ = f;

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_ONESHOT_MAX);

    RELVAL *n = DETAILS_AT(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT64(n) < 0) {
        ++VAL_INT64(n);
        return nullptr;  // return null until 0 is reached
    }

    REBVAL *code = FRM_ARG(f, 2);  // skip the RETURN
    if (Do_Branch_Throws(OUT, code))
        return_thrown (OUT);

    return OUT;
}


//
//  do-branch: native [
//
//  {Sample Interface for a Simplified DO that just runs a Branch}
//
//      return: [<opt> any-value!]
//      branch [any-branch!]
//  ]
//
REBNATIVE(do_branch)
//
// !!! This function only exists to serve as the interface for the generated
// function from N-SHOT.  More thinking is necessary about how to layer DO
// on top of a foundational DO* (instead of the current way, which has the
// higher level DO as a native that calls out to helper code for its
// implementation...)  Revisit.
{
    INCLUDE_PARAMS_OF_DO_BRANCH;
    UNUSED(ARG(branch));

    fail ("DO-BRANCH is theoretical and not part of an API yet.");
}


//
//  n-shot: native [
//
//  {Create a DO variant that executes what it's given for N times}
//
//      return: [action!]
//      n "Number of times to execute before being a no-op"
//          [integer!]
//  ]
//
REBNATIVE(n_shot)
{
    INCLUDE_PARAMS_OF_N_SHOT;

    REBI64 n = VAL_INT64(ARG(n));

    REBACT *n_shot = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(Lib(DO_BRANCH))),
        nullptr,  // no partials
        n >= 0 ? &Downshot_Dispatcher : &Upshot_Dispatcher,
        IDX_ONESHOT_MAX  // details array capacity
    );
    Init_Integer(ARR_AT(ACT_DETAILS(n_shot), IDX_ONESHOT_COUNTER), n);

    return Init_Action(OUT, n_shot, ANONYMOUS, UNBOUND);
}
