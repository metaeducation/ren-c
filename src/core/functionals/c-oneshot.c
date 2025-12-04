//
//  file: %c-oneshot.c
//  summary: "Generates function that will run code N times, then return null"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
//     >> /once: oneshot
//
//     >> once [5 + 5]
//     == 10
//
//     >> once [5 + 5]
//     == ~null~  ; anti
//
//     >> once [5 + 5]
//     == ~null~  ; anti
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
    MAX_IDX_ONESHOT = IDX_ONESHOT_COUNTER
};


//
//  Downshot_Dispatcher: C
//
Bounce Downshot_Dispatcher(Level* const L)  // runs until count is reached
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_ONESHOT);

    Stable* n = Details_At(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT64(n) == 0)
        return NULLED;  // always return null once 0 is reached
    mutable_VAL_INT64(n) -= 1;

    Stable* code = Known_Stable(Level_Arg(L, 1));
    return DELEGATE_BRANCH(OUT, code);
}


//
//  Upshot_Dispatcher: C
//
Bounce Upshot_Dispatcher(Level* const L)  // won't run until count is reached
{
    USE_LEVEL_SHORTHANDS (L);

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_ONESHOT);

    Stable* n = Details_At(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT64(n) < 0) {
        mutable_VAL_INT64(n) += 1;
        return NULLED;  // return null until 0 is reached
    }

    Stable* code = Known_Stable(Level_Arg(L, 1));
    return DELEGATE_BRANCH(OUT, code);
}

//
//  Oneshot_Details_Querier: C
//
bool Oneshot_Details_Querier(
    Sink(Stable) out,
    Details* details,
    SymId property
){
    assert(
        Details_Dispatcher(details) == &Upshot_Dispatcher
        or Details_Dispatcher(details) == &Downshot_Dispatcher
    );
    UNUSED(details);

    switch (property) {
      case SYM_RETURN_OF:
        Init_Unconstrained_Parameter(
            out,
            FLAG_PARAMCLASS_BYTE(PARAMCLASS_NORMAL)
        );
        return true;

      default:
        break;
    }

    return false;
}


//
//  do-branch: native [
//
//  "Sample Interface for a Simplified EVAL that just runs a Branch"
//
//      return: [any-value?]
//      branch [any-branch?]
//  ]
//
DECLARE_NATIVE(DO_BRANCH)
//
// !!! This function only exists to serve as the interface for the generated
// function from N-SHOT.  More thinking is necessary about how to layer DO
// on top of a foundational DO* (instead of the current way, which has the
// higher level EVAL as a native that calls out to helper code for its
// implementation...)  Revisit.
{
    INCLUDE_PARAMS_OF_DO_BRANCH;
    UNUSED(ARG(BRANCH));

    panic ("DO-BRANCH is theoretical and not part of an API yet.");
}


//
//  n-shot: native [
//
//  "Create an EVAL variant that executes what it's given for N times"
//
//      return: [~[action!]~]
//      n "Number of times to execute before being a no-op"
//          [integer!]
//  ]
//
DECLARE_NATIVE(N_SHOT)
{
    INCLUDE_PARAMS_OF_N_SHOT;

    REBI64 n = VAL_INT64(ARG(N));

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        LIB(DO_BRANCH),
        n >= 0 ? &Downshot_Dispatcher : &Upshot_Dispatcher,
        MAX_IDX_ONESHOT  // details array capacity
    );
    Init_Integer(Details_At(details, IDX_ONESHOT_COUNTER), n);

    Init_Action(OUT, details, ANONYMOUS, UNCOUPLED);
    return Packify_Action(OUT);
}
