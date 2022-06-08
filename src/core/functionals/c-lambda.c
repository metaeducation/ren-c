//
//  File: %c-lambda.c
//  Summary: "Low-overhead virtual binding ACTION! with no Typecheck/RETURN"
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
{
    REBFRM *frame_ = f;

    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(phase);
    assert(ARR_LEN(details) == IDX_LAMBDA_MAX);

    const REBVAL *block = DETAILS_AT(details, IDX_LAMBDA_BLOCK);
    assert(IS_BLOCK(block));

    SET_SERIES_FLAG(f->varlist, MANAGED);  // not manually tracked...

    // We have to use Make_Or_Reuse_Patch() here, because it could be the
    // case that a higher level wrapper used the frame and virtually bound it.
    //
    // !!! Currently, since we are evaluating the block with its own virtual
    // binding being taken into account, using that block's binding as the
    // `next` (VAL_SPECIFIER(block)) means it's redundant when creating the
    // feed, since it tries to apply this specifier on top of that *again*.
    // The merging notices the redundancy and doesn't create a new specifier
    // which is good...but this is still inefficient.  This all needs review.
    //
    REBSPC *specifier = Make_Or_Reuse_Patch(
        CTX(f->varlist),
        CTX_LEN(CTX(f->varlist)),
        VAL_SPECIFIER(block),
        REB_WORD
    );

    // Note: Invisibility is not allowed, e.g. `x -> [elide x]` or `x -> []`
    // will return a ~void~ isotope.  Hence prior `f->out` is always wiped out.

    if (Do_Any_Array_At_Throws(OUT, block, specifier))
        return_thrown (OUT);

    return OUT;
}


//
//  lambda: native [
//
//  {Create an ACTION! variant that acts the same, but has added parameters}
//
//      return: [action!]
//      :words "Names of arguments (will not be type checked)"
//          [<end> word! lit-word! meta-word! block!]
//      body "Code to execute"
//          [block!]
//  ]
//
REBNATIVE(lambda)
{
    INCLUDE_PARAMS_OF_LAMBDA;

    // The view of the body of the lambda needs to be const.  (Like a FUNC, it
    // is potentially run many times.  Additionally, it's virtually bound.)
    //
    REBVAL *body = Constify(ARG(body));

    // The reason <end> is allowed is for the enfix case, `x: -> [print "hi"]`
    // Though you could use DOES for this, it's still up in the air whether
    // DOES will be different or not.
    //
    // (Arguably the <end> tolerance should be specially implemented by the
    // enfix form and not applicable to the prefix form, but it seems fine.)
    //
    REBVAL *wordlist = ARG(words);
    REBSPC *item_specifier;
    const Cell *item_tail;
    const Cell *item;
    if (IS_BLOCK(wordlist)) {
        item = VAL_ARRAY_AT(&item_tail, wordlist);
        item_specifier = VAL_SPECIFIER(wordlist);
    }
    else if (
        IS_WORD(wordlist)
        or IS_META_WORD(wordlist)
        or IS_QUOTED(wordlist)
    ){
        item = ARG(words);
        item_specifier = SPECIFIED;
        item_tail = item + 1;
    }
    else {
        item_specifier = SPECIFIED;
        assert(IS_NULLED(wordlist));
        item = nullptr;
        item_tail = nullptr;
    }

    // For the moment, this lazily reuses Pop_Paramlist(), just because that
    // code is a vetted path.  It could be done much more efficiently, but
    // at the risk of getting the incantation wrong.  Optimize this when
    // things are more fully pinned down.

    REBDSP dsp_orig = DSP;

    // Start with pushing nothings for the [0] slot
    //
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
            const REBSYM *symbol = VAL_WORD_SYMBOL(DS_TOP);
            Init_Word(DS_TOP, symbol);
        }
        else if (IS_QUOTED(DS_TOP)) {
            Unquotify(DS_TOP, 1);
            if (not IS_WORD(DS_TOP))
                fail (item);
            pclass = PARAM_CLASS_HARD;
        }
        else
            fail (item);

        Init_Param(DS_PUSH(), pclass | PARAM_FLAG_VANISHABLE, TS_OPT_VALUE);

        Init_Nulled(DS_PUSH());  // types (not supported)
        Init_Nulled(DS_PUSH());  // notes (not supported)
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
