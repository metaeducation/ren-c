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
    IDX_ONESHOT_COUNTER = 0,  // Count that is going down to 0
    IDX_ONESHOT_MAX
};


REB_R Downshot_Dispatcher(REBFRM *f)  // runs until count is reached
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_ONESHOT_MAX);

    RELVAL *n = ARR_AT(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT_SMALL(n) == 0)
        return nullptr;  // always return null once 0 is reached
    --VAL_INT_SMALL(n);

    REBVAL *code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, FRM_SPARE(f), code))
        return R_THROWN;

    return Voidify_If_Nulled(f->out);
}


REB_R Upshot_Dispatcher(REBFRM *f)  // won't run until count is reached
{
    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_ONESHOT_MAX);

    RELVAL *n = ARR_AT(details, IDX_ONESHOT_COUNTER);
    if (VAL_INT_SMALL(n) < 0) {
        ++VAL_INT_SMALL(ARR_HEAD(details));
        return nullptr;  // return null until 0 is reached
    }

    REBVAL *code = FRM_ARG(f, 1);
    if (Do_Branch_Throws(f->out, FRM_SPARE(f), code))
        return R_THROWN;

    return Voidify_If_Nulled(f->out);
}


//
//  n-shot: native [
//
//  {Create a DO variant that executes what it's given for N times}
//
//      n "Number of times to execute before being a no-op"
//          [integer!]
//  ]
//
REBNATIVE(n_shot)
{
    INCLUDE_PARAMS_OF_N_SHOT;

    REBI64 n = VAL_INT64(ARG(n));

    REBARR *paramlist = Make_Array_Core(
        2,
        SERIES_MASK_PARAMLIST | NODE_FLAG_MANAGED
    );

    REBVAL *archetype = RESET_CELL(
        Alloc_Tail_Array(paramlist),
        REB_ACTION,
        CELL_MASK_ACTION
    );
    VAL_ACT_PARAMLIST_NODE(archetype) = NOD(paramlist);
    INIT_BINDING(archetype, UNBOUND);

    // !!! Should anything DO would accept be legal, as DOES would run?
    //
    Init_Param(
        Alloc_Tail_Array(paramlist),
        REB_P_NORMAL,
        Canon(SYM_VALUE),  // !!! would SYM_CODE be better?
        FLAGIT_KIND(REB_BLOCK) | FLAGIT_KIND(REB_ACTION)
    );

    MISC_META_NODE(paramlist) = nullptr;  // !!! auto-generate info for HELP?

    REBACT *n_shot = Make_Action(
        paramlist,
        n >= 0 ? &Downshot_Dispatcher : &Upshot_Dispatcher,
        nullptr,  // no underlying action (use paramlist)
        nullptr,  // no specialization exemplar (or inherited exemplar)
        IDX_ONESHOT_MAX  // details array capacity
    );
    Init_Integer(ARR_AT(ACT_DETAILS(n_shot), IDX_ONESHOT_COUNTER), n);

    return Init_Action(D_OUT, n_shot, ANONYMOUS, UNBOUND);
}
