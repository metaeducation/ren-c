//
//  File: %c-combinator.c
//  Summary: "Makes Function Suitable for Use As a PARSE Keyword"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021 Ren-C Open Source Contributors
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
// The idea behind a combinator is that a function follows a standard set of
// inputs and outputs to make it fit into an ecology of parsing operations.
// At its most basic level, this function takes in a position in an input
// series and then returns an indication of how much input it consumed (the
// "remainder") as well as a synthesized value.  One of its possible return
// results is a signal of failure, which is done by synthesizing a "pure" NULL
// (as opposed to a null isotope).
//
// So one of the reasons to have a separate COMBINATOR function generator is
// to force some of those implicit function arguments and returns.
//
// But another reason is to get a hook into each time a combinator is executed.
// Without such a hook, there'd be no way to centrally know when combinators
// were being invoked (barring some more universal systemic trace facility),
// because combinators call each other without going through any intermediary
// requesting service.  This also permits being able to know things like the
// furthest point in input that was reached--even if overall the parsing
// winded up not matching.
//

#include "sys-core.h"

enum {
    IDX_COMBINATOR_BODY = 1,  // Code wrapped up
    IDX_COMBINATOR_MAX
};

// !!! These are the positions that COMBINATOR has for its known arguments in
// the generated spec.  Changes to COMBINATOR could change this.
//
enum {
    IDX_COMBINATOR_PARAM_RETURN = 1,
    IDX_COMBINATOR_PARAM_REMAINDER,
    IDX_COMBINATOR_PARAM_STATE,
    IDX_COMBINATOR_PARAM_INPUT
};

// !!! With a native UPARSE, these would come from INCLUDE_PARAMS_OF_UPARSE.
// Until that happens, this could get out of sync with the index positions of
// the usermode function.
//
enum {
    IDX_UPARSE_PARAM_RETURN = 1,
    IDX_UPARSE_PARAM_FURTHEST,
    IDX_UPARSE_PARAM_SERIES,
    IDX_UPARSE_PARAM_RULES,
    IDX_UPARSE_PARAM_COMBINATORS,
    IDX_UPARSE_PARAM_CASE,
    IDX_UPARSE_PARAM_FULLY,
    IDX_UPARSE_PARAM_VERBOSE,
    IDX_UPARSE_PARAM_COLLECTING,
    IDX_UPARSE_PARAM_GATHERING
};


//
//  Combinator_Dispactcher: C
//
// The main responsibilities of the combinator dispatcher is to provide a hook
// for verbose debugging, as well as to record the furthest point reached.
// At the moment we focus on the furthest point reached.
//
REB_R Combinator_Dispatcher(REBFRM *f)
{
    REB_R r = Returner_Dispatcher(f);
    if (r == R_THROWN)
        return r;

    if (r == nullptr or (not IS_END(r) and IS_NULLED(r)))
        return r;  // did not advance, don't update furthest

    // This particular parse succeeded, but did the furthest point exceed the
    // previously measured furthest point?  This only is a question that
    // matters if there was a request to know the furthest point...
    //
    REBVAL *state = FRM_ARG(f, IDX_COMBINATOR_PARAM_STATE);
    assert(IS_FRAME(state));  // combinators *must* have this as the UPARSE.
    REBFRM *frame_ = CTX_FRAME_MAY_FAIL(VAL_CONTEXT(state));
    REBVAL *furthest_word = FRM_ARG(frame_, IDX_UPARSE_PARAM_FURTHEST);
    if (IS_NULLED(furthest_word))
        return r;

    REBVAL *furthest_var = Lookup_Mutable_Word_May_Fail(
        furthest_word,
        SPECIFIED
    );

    REBVAL *remainder_word = FRM_ARG(f, IDX_COMBINATOR_PARAM_REMAINDER);
    const REBVAL *remainder_var = Lookup_Word_May_Fail(
        remainder_word,
        SPECIFIED
    );

    assert(VAL_SERIES(remainder_var) == VAL_SERIES(furthest_var));

    if (VAL_INDEX(remainder_var) > VAL_INDEX(furthest_var))
        Copy_Cell(furthest_var, remainder_var);

    return r;
}


//
//  combinator: native [
//
//  {Make a stylized ACTION! that fulfills the interface of a combinator}
//
//      spec [block!]
//      body [block!]
//  ]
//
REBNATIVE(combinator)
{
    INCLUDE_PARAMS_OF_COMBINATOR;

    // Building an expanded spec could be done more laboriously with raw code
    // pushing symbols to the data stack.  But we're not particularly concerned
    // about the performance of making a combinator at the moment...just
    // running one.  This code is directly from the original userspace
    // combinator implementation.

    REBVAL *expanded_spec = rebValue(
        "let spec:", ARG(spec),  // alias input spec to variable for easier use

        "compose [",
            // Get the text description if given

            "((if text? spec.1 [spec.1, elide spec: my next]))",

            // Get the RETURN: definition if there is one, otherwise add one
            // so that we are sure that we know the position/order of the
            // arguments.

            "((if set-word? spec.1 [",
                "assert [spec.1 = 'return:]",
                "assert [text? spec.2]",
                "assert [block? spec.3]",

                "reduce [spec.1 spec.2 spec.3]",
                "elide spec: my skip 3",
            "] else [",
                "[return: [<opt> <invisible> any-value!]]",
            "]))",

            "remainder: [<opt> any-series!]",

            "state [frame!]",
            "input [any-series!]",

            "((spec))",  // arguments the combinator takes, if any.
        "]"
    );
    
    REBCTX *meta;
    REBFLGS flags = MKF_KEYWORDS | MKF_RETURN;
    REBARR *paramlist = Make_Paramlist_Managed_May_Fail(
        &meta,
        expanded_spec,
        &flags
    );

    rebRelease(expanded_spec);  // was an API handle allocated by rebValue

    REBACT *combinator = Make_Action(
        paramlist,
        &Combinator_Dispatcher,
        IDX_COMBINATOR_MAX  // details array capacity
    );

    // !!! As with FUNC, we copy and bind the block the user gives us.  This
    // means we will not see updates to it.  So long as we are copying it,
    // we might as well mutably bind it--there's no incentive to virtual
    // bind things that are copied.
    //
    REBARR *relativized = Copy_And_Bind_Relative_Deep_Managed(
        ARG(body),
        combinator,
        TS_WORD
    );

    Init_Relative_Block(
        ARR_AT(ACT_DETAILS(combinator), IDX_COMBINATOR_BODY),
        combinator,
        relativized
    );

    return Init_Action(D_OUT, combinator, ANONYMOUS, UNBOUND);
}
