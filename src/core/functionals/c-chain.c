//
//  File: %c-chain.c
//  Summary: "Function generator for making a pipeline of post-processing"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2020 Ren-C Open Source Contributors
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
// CHAIN is used to create a function that matches the interface of a "first"
// function, and then pipes its results through to several subsequent
// post-processing actions:
//
//     >> negadd: chain [:add | :negate]
//
//     >> negadd 2 2
//     == -4
//
// For more complex post-processing which may involve access to the original
// inputs to the first function (or other memory in the process), consider
// using ENCLOSE...which is less efficient, but more powerful.
//
// !!! CHAIN is one of the oldest function derivations, and has not been
// revisited much in its design--e.g. to support multiple return values.
//

#include "sys-core.h"

enum {
    IDX_CHAINER_PIPELINE = 1,  // Chain of functions to execute
    IDX_CHAINER_MAX
};


//
//  Push_Downshifted_Frame: C
//
// When a derived function dispatcher receives a frame built for the function
// it derived from, sometimes it can do some work...update the phase...and
// keep running in that same REBFRM* allocation.
//
// But if it wants to stay in control and do post-processing (as CHAIN does)
// then it needs to remain linked into the stack.  This function helps to
// move the built frame into a new frame that can be executed with a new
// entry to Process_Action().  The ability is also used by RESKINNED.
//
REBFRM *Push_Downshifted_Frame(REBVAL *out, REBFRM *f) {
    DECLARE_FRAME (
        sub,
        f->feed,
        EVAL_MASK_DEFAULT
            | EVAL_FLAG_MAYBE_STALE
    );
    Push_Frame(out, sub);
    assert(sub->varlist == nullptr);
    sub->varlist = f->varlist;
    assert(BONUS(KeySource, sub->varlist) == f);
    INIT_BONUS_KEYSOURCE(sub->varlist, sub);
    sub->rootvar = SPECIFIC(ARR_HEAD(sub->varlist));

    f->varlist = &PG_Inaccessible_Series;
    f->rootvar = nullptr;

    sub->key = nullptr;
    sub->key_tail = nullptr;
    sub->arg = sub->rootvar + 1;  // !!! enforced by entering Process_Action()
    sub->param = cast_PAR(END);

    return sub;
}


//
//  Chainer_Dispatcher: C
//
// The frame built for the CHAIN matches the arguments needed by the first
// function in the pipeline.  Having the same interface as that function
// makes a chained function specializable.
//
// A first cut at implementing CHAIN did it all within one REBFRM.  It changed
// the FRM_PHASE() and returned a REDO signal--with actions pushed to the data
// stack that the evaluator was complicit in processing as "things to run
// afterward".  This baked awareness of chaining into %c-eval.c, when it is
// better if the process was localized inside the dispatcher.
//
// Handling it inside the dispatcher means the Chainer_Dispatcher() stays on
// the stack and in control.  This means either unhooking the current `f` and
// putting a new REBFRM* above it, or stealing the content of the `f` into a
// new frame to put beneath it.  The latter is chosen to avoid disrupting
// existing pointers to `f`.
//
// (Having a separate frame for the overall chain has an advantage in error
// messages too, as there is a frame with the label of the function that the
// user invoked in the stack trace...instead of just the chained item that
// causes an error.)
//
// !!! Note: Stealing the built varlist to give to a new REBFRM for the head
// of the chain leaves the actual chainer frame with no varlist content.  That
// means debuggers introspecting the stack may see a "stolen" frame state.
//
REB_R Chainer_Dispatcher(REBFRM *f)
{
    REBFRM *frame_ = f;  // for RETURN macros

    REBARR *details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_CHAINER_MAX);

    const REBARR *pipeline = VAL_ARRAY(ARR_AT(details, IDX_CHAINER_PIPELINE));
    const Cell *chained_tail = ARR_TAIL(pipeline);
    const Cell *chained = ARR_HEAD(pipeline);

    assert(Is_Void(SPARE));
    REBFRM *sub = Push_Downshifted_Frame(SPARE, f);

    INIT_FRM_PHASE(sub, VAL_ACTION(chained));
    INIT_FRM_BINDING(sub, VAL_ACTION_BINDING(chained));

    sub->original = VAL_ACTION(chained);
    sub->label = VAL_ACTION_LABEL(chained);
  #if !defined(NDEBUG)
    sub->label_utf8 = sub->label
        ? STR_UTF8(unwrap(sub->label))
        : "(anonymous)";
  #endif

    // Now apply the functions that follow.  The original code reused the
    // frame of the chain, this reuses the subframe.
    //
    // (On the head of the chain we start at the dispatching phase since the
    // frame is already filled, but each step after that uses enfix and
    // runs from the top.)

    while (true) {
        if (Process_Action_Core_Throws(sub)) {
            Abort_Frame(sub);
            return_thrown (sub->out);
        }

        // We reuse the subframe's REBFRM structure, but have to drop the
        // action args, as the paramlist is almost certainly completely
        // incompatible with the next chain step.

        ++chained;
        if (chained == chained_tail)
            break;

        if (sub->varlist and NOT_SERIES_FLAG(sub->varlist, MANAGED))
            GC_Kill_Series(sub->varlist);

        sub->varlist = nullptr;
        Push_Action(sub, VAL_ACTION(chained), VAL_ACTION_BINDING(chained));

        // We use the same mechanism as enfix operations do...give the
        // next chain step its first argument coming from f->out
        //
        // !!! One side effect of this is that unless CHAIN is changed
        // to check, your chains can consume more than one argument.
        // This might be interesting or it might be bugs waiting to
        // happen, trying it out of curiosity for now.
        //
        Begin_Prefix_Action(sub, VAL_ACTION_LABEL(chained));
        assert(NOT_FEED_FLAG(sub->feed, NEXT_ARG_FROM_OUT));
        SET_FEED_FLAG(sub->feed, NEXT_ARG_FROM_OUT);

        FRM_STATE_BYTE(sub) = ST_ACTION_INITIAL_ENTRY;
    }

    Drop_Frame(sub);

    return_value (SPARE);
}


//
//  chain*: native [
//
//  {Create a processing pipeline of actions, each consuming the last result}
//
//      return: [action!]
//      pipeline "Block of ACTION!s to apply (will be LOCKed)"
//          [block!]
//  ]
//
REBNATIVE(chain_p)  // see extended definition CHAIN in %base-defs.r
{
    INCLUDE_PARAMS_OF_CHAIN_P;

    REBVAL *out = OUT;  // plan ahead for factoring into Chain_Action(out..

    REBVAL *pipeline = ARG(pipeline);
    const Cell *tail;
    const Cell *first = VAL_ARRAY_AT(&tail, pipeline);

    // !!! Current validation is that all are actions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    const Cell *check = first;
    for (; check != tail; ++check) {
        if (not IS_ACTION(check)) {
            DECLARE_LOCAL (specific);
            Derelativize(specific, check, VAL_SPECIFIER(pipeline));
            fail (specific);
        }
    }

    // The chained function has the same interface as head of the chain.
    //
    // !!! Output (RETURN) should match the *tail* of the chain.  Is this
    // worth a new paramlist?  Should return mechanics be just reviewed in
    // general, possibly that all actions put the return slot in a separate
    // sliver that includes the partials?
    //
    REBACT *chain = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(first)),  // same interface as first action
        ACT_PARTIALS(VAL_ACTION(first)),
        &Chainer_Dispatcher,
        IDX_CHAINER_MAX  // details array capacity
    );
    Force_Value_Frozen_Deep(pipeline);
    Copy_Cell(ARR_AT(ACT_DETAILS(chain), IDX_CHAINER_PIPELINE), pipeline);

    return Init_Action(out, chain, VAL_ACTION_LABEL(first), UNBOUND);
}
