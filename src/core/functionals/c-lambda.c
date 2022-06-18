//
//  File: %c-lambda.c
//  Summary: "Low-overhead virtual binding ACTION! with no Typecheck/RETURN"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2021-2022 Ren-C Open Source Contributors
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
// LAMBDA is an efficient form of ACTION! which has no RETURN, has no type
// checking, and which does not copy the block that serves as its body.  It
// is enfixed as `->` and is intended for uses like light branching.
//
//     >> if true [10] then x -> [print ["The branch produced" x]]
//     The branch produced 10
//
// The implementation is essentially like `does [use 'x [...]]`, but it's
// made as an optimized generator to drive the cost down in uses like the
// branch above.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * Though lighter-weight than a plain FUNC, lambdas still have to pay for
//   a DETAILS array (minimum length 2), a paramlist (also minimum length 2),
//   and a keylist (which may be optimizable to just a REBSTR* in the single
//   variable case...which is in the works).  Further optimizations would
//   need to weigh in the question of how AS FRAME! SOME-LAMBDA could work,
//   and if it would be worth it in the scheme of things.
//
// * Invisibility is allowed in lambda, so `x -> []` is void
//

#include "sys-core.h"

enum {
    IDX_LAMBDA_BLOCK = 1,
    IDX_LAMBDA_MAX
};


//
//  Lambda_Dispatcher: C
//
// Uses virtual binding (essentially like a USE)
//
// !!! Review if this should be unified with the Block_Dispatcher() of DOES.
// It could detect the case of no parameters to the frame, and thus avoid
// doing any virtual binding.  However, there's some difference w.r.t. the
// "derived binding" that need a going-over.
//
REB_R Lambda_Dispatcher(REBFRM *f)
//
// 1. We have to use Make_Or_Reuse_Patch() here, because it could be the case
//    that a higher level wrapper used the frame and virtually bound it.
//
// 2. Currently, since we are evaluating the block with its own virtual
//    binding being taken into account, using that block's binding as the
//    `next` (VAL_SPECIFIER(block)) means it's redundant when creating the
//    feed, since it tries to apply this specifier on top of that *again*.
//    The merging notices the redundancy and doesn't create a new specifier
//    which is good...but this is still inefficient.  This all needs review.
{
    REBFRM *frame_ = f;

    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(phase);
    assert(ARR_LEN(details) == IDX_LAMBDA_MAX);

    const REBVAL *block = DETAILS_AT(details, IDX_LAMBDA_BLOCK);
    assert(IS_BLOCK(block));

    SET_SERIES_FLAG(f->varlist, MANAGED);  // not manually tracked...

    REBSPC *specifier = Make_Or_Reuse_Patch(  // may reuse, see [1]
        CTX(f->varlist),
        CTX_LEN(CTX(f->varlist)),
        VAL_SPECIFIER(block),  // redundant with feed, see [2]
        REB_WORD
    );

    REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_MAYBE_STALE;
    delegate_core (OUT, flags, block, specifier, END);
}


//
//  Lambda_Unoptimized_Dispatcher: C
//
// Used by LAMBDA when it can't use the optimized form.  This runs very much
// like function dispatch, except there's no RETURN to catch.  So it can
// execute directly into the output cell.
//
REB_R Lambda_Unoptimized_Dispatcher(REBFRM *frame_)
{
    REBACT *phase = FRM_PHASE(FRAME);
    REBARR *details = ACT_DETAILS(phase);
    Cell *body = ARR_AT(details, IDX_DETAILS_1);  // code to run
    assert(IS_BLOCK(body) and IS_RELATIVE(body) and VAL_INDEX(body) == 0);

    REBFLGS flags = EVAL_MASK_DEFAULT | EVAL_FLAG_MAYBE_STALE;
    delegate_core (OUT, flags, body, SPC(FRAME->varlist), END);
}


//
//  lambda: native [
//
//  {Makes an anonymous function that evaluates to its body, and has no RETURN}
//
//      return: [action!]
//      spec "Names of arguments (will not be type checked)"
//          [blank! word! lit-word! meta-word! block!]
//      body "Code to execute"
//          [<const> block!]
//  ]
//
REBNATIVE(lambda)
//
// 1. For the moment, this lazily reuses Pop_Paramlist(), just because that
//    code is a vetted path.  It could be done much more efficiently, but at
//    the risk of getting the incantation wrong.  Optimize later if needed.
{
    INCLUDE_PARAMS_OF_LAMBDA;

    REBVAL *spec = ARG(spec);
    REBVAL *body = ARG(body);

    bool optimizable = true;

    REBSPC *item_specifier;
    const Cell *item_tail;
    const Cell *item;
    if (IS_BLOCK(spec)) {
        item = VAL_ARRAY_AT(&item_tail, spec);
        item_specifier = VAL_SPECIFIER(spec);
    }
    else if (
        IS_WORD(spec)
        or IS_META_WORD(spec)
        or IS_QUOTED(spec)
    ){
        item = spec;
        item_specifier = SPECIFIED;
        item_tail = item + 1;
    }
    else {
        assert(IS_BLANK(spec));
        item_specifier = SPECIFIED;
        item = nullptr;
        item_tail = nullptr;
    }

    REBDSP dsp_orig = DSP;  // reuses Pop_Paramlist(), see [1]

    Init_None(DS_PUSH());  // key slot (signal for no pushes)
    Init_Trash(DS_PUSH());  // unused
    Init_Trash(DS_PUSH());  // unused
    Init_Nulled(DS_PUSH());  // description slot

    for (; item != item_tail; ++item) {
        Derelativize(DS_PUSH(), item, item_specifier);

        // First in triad needs to be a WORD!, after pclass extracted...
        //
        enum Reb_Param_Class pclass;
        if (IS_WORD(DS_TOP))
            pclass = PARAM_CLASS_NORMAL;
        else if (IS_META_WORD(DS_TOP)) {
            pclass = PARAM_CLASS_META;
            const Symbol *symbol = VAL_WORD_SYMBOL(DS_TOP);
            Init_Word(DS_TOP, symbol);
        }
        else if (IS_QUOTED(DS_TOP)) {
            Unquotify(DS_TOP, 1);
            if (not IS_WORD(DS_TOP))
                fail (item);
            pclass = PARAM_CLASS_HARD;
        }
        else if (IS_SET_WORD(item) and VAL_WORD_ID(item) == SYM_RETURN) {
            fail ("LAMBDA (->) does not offer RETURN facilities, use FUNCTION");
        }
        else {
            if (not IS_BLOCK(spec))
                fail ("Invalid LAMBDA specification");

            optimizable = false;
            continue;
        }

        Init_Param(DS_PUSH(), pclass | PARAM_FLAG_VANISHABLE, TS_OPT_VALUE);

        Init_Nulled(DS_PUSH());  // types (not supported)
        Init_Nulled(DS_PUSH());  // notes (not supported)
    }

    if (not optimizable) {
        DS_DROP_TO(dsp_orig);

        REBACT *lambda = Make_Interpreted_Action_May_Fail(
            spec,
            body,
            MKF_KEYWORDS,  // no MKF_RETURN
            &Lambda_Unoptimized_Dispatcher,
            1 + IDX_DETAILS_1  // archetype and one array slot (will be filled)
        );

        return Init_Action(OUT, lambda, ANONYMOUS, UNBOUND);
    }

    REBCTX *meta;
    REBARR *paramlist = Pop_Paramlist_With_Meta_May_Fail(
        &meta,
        dsp_orig,
        MKF_KEYWORDS,
        0  // no definitional_return_dsp
    );

    REBACT* lambda = Make_Action(
        paramlist,
        nullptr,  // no partials
        &Lambda_Dispatcher,
        IDX_LAMBDA_MAX  // same as specialization, just 1 (for archetype)
    );

    assert(ACT_META(lambda) == nullptr);

    REBARR *details = ACT_DETAILS(lambda);
    Copy_Cell(ARR_AT(details, IDX_LAMBDA_BLOCK), body);

    return Init_Action(OUT, lambda, ANONYMOUS, UNBOUND);
}
