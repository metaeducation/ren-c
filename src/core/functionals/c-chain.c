//
//  File: %c-chain.c
//  Summary: "Function generator for making a pipeline of post-processing"
//  Section: datatypes
//  Project: "Ren-C Language Interpreter and Run-time Environment"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2016-2022 Ren-C Open Source Contributors
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
//     >> negadd: chain [:add :negate]
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
// keep running in that same Frame(*) allocation.
//
// But if it wants to stay in control and do post-processing (as CHAIN does)
// then it needs to remain linked into the stack.  This function helps to
// move the built frame into a new frame that can be executed with a new
// entry to Process_Action().  The ability is also used by RESKINNED.
//
Frame(*) Push_Downshifted_Frame(REBVAL *out, Frame(*) f) {
    Flags flags = ACTION_EXECUTOR_FLAG_IN_DISPATCH | FRAME_FLAG_MAYBE_STALE;
    flags |= f->flags.bits & FRAME_FLAG_FAILURE_RESULT_OK;

    Frame(*) sub = Make_Frame(f->feed, flags);
    Push_Frame(out, sub);
    assert(sub->varlist == nullptr);
    sub->varlist = f->varlist;
    assert(BONUS(KeySource, sub->varlist) == f);
    INIT_BONUS_KEYSOURCE(sub->varlist, sub);
    sub->rootvar = SPECIFIC(ARR_HEAD(sub->varlist));

    // Note that it can occur that this may be a TRAMPOLINE_KEEPALIVE subframe
    // of something like another CHAIN, that it intends to reuse (!)  This
    // means it started out thinking we were going to run an action in that
    // frame and drop it, when in reality we're changing the executor and
    // everything.  This is clearly voodoo but maybe it can be formalized.
    //
    f->varlist = &PG_Inaccessible_Series;  // trash?  nullptr?
    f->rootvar = nullptr;
    TRASH_POINTER_IF_DEBUG(f->executor);  // caller must set
    TRASH_POINTER_IF_DEBUG(f->label);

    sub->u.action.dispatcher_base = f->u.action.dispatcher_base;

    sub->executor = &Action_Executor;

    TRASH_IF_DEBUG(f->u);  // not an action anymore; trash after get stack base

    return sub;
}


//
//  Chainer_Dispatcher: C
//
// The frame built for the CHAIN matches the arguments needed by the first
// function in the pipeline.  Having the same interface as that function
// makes a chained function specializable.
//
// A first cut at implementing CHAIN did it all within one frame.  It changed
// the FRM_PHASE() and returned a REDO signal--with actions pushed to the data
// stack that the evaluator was complicit in processing as "things to run
// afterward".  This baked awareness of chaining into %c-eval.c, when it is
// better if the process was localized inside the dispatcher.
//
// Handling it inside the dispatcher means the Chainer_Dispatcher() stays on
// the stack and in control.  This means either unhooking the current `f` and
// putting a new Frame(*) above it, or stealing the content of the `f` into a
// new frame to put beneath it.  The latter is chosen to avoid disrupting
// existing pointers to `f`.
//
// (Having a separate frame for the overall chain has an advantage in error
// messages too, as there is a frame with the label of the function that the
// user invoked in the stack trace...instead of just the chained item that
// causes an error.)
//
Bounce Chainer_Dispatcher(Frame(*) f)
//
// 1. Stealing the varlist leaves the actual chainer frame with no varlist
//    content.  That means debuggers introspecting the stack may see a
//    "stolen" frame state.
//
// 2. You can't have an Action_Executor()-based frame on the stack unless it
//    has a lot of things (like a varlist, which provides the phase, etc.)
//    So we switch it around to where the frame that had its varlist stolen
//    just uses this function as its executor, so we get called back.
//
// 3. At the head of the chain we start at the dispatching phase since the
//    frame is already filled, but each step after that uses enfix and runs
//    from the top.)
//
// 4. We use the same mechanism as enfix operations do...give the next chain
//    step its first argument coming from f->out.
//
//    !!! One side effect of this is that unless CHAIN is changed to check,
//    your chains can consume more than one argument.  It might be interesting
//    or it might be bugs waiting to happen, trying it this way for now.
{
    Frame(*) frame_ = f;  // for RETURN macros

    if (THROWING)  // this routine is both dispatcher and executor, see [2]
        return THROWN;

    enum {
        ST_CHAINER_INITIAL_ENTRY = STATE_0,
        ST_CHAINER_RUNNING_SUBFUNCTION
    };

    switch (STATE) {
      case ST_CHAINER_INITIAL_ENTRY: goto initial_entry;
      case ST_CHAINER_RUNNING_SUBFUNCTION: goto run_next_in_chain;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    Array(*) details = ACT_DETAILS(FRM_PHASE(f));
    assert(ARR_LEN(details) == IDX_CHAINER_MAX);

    Value(*) pipeline_at = Init_Block(
        SPARE,  // index of BLOCK! is current step
        VAL_ARRAY(ARR_AT(details, IDX_CHAINER_PIPELINE))
    );

    Frame(*) sub = Push_Downshifted_Frame(OUT, f);  // steals varlist, see [1]
    f->executor = &Chainer_Dispatcher;  // so trampoline calls us, see [2]

    Cell(const*) chained = VAL_ARRAY_ITEM_AT(pipeline_at);
    ++VAL_INDEX_RAW(pipeline_at);

    INIT_FRM_PHASE(sub, VAL_ACTION(chained));  // has varlist already, see [3]
    INIT_FRM_BINDING(sub, VAL_ACTION_BINDING(chained));

    sub->u.action.original = VAL_ACTION(chained);
    sub->label = VAL_ACTION_LABEL(chained);
  #if !defined(NDEBUG)
    sub->label_utf8 = sub->label
        ? STR_UTF8(unwrap(sub->label))
        : "(anonymous)";
  #endif

    STATE = ST_CHAINER_RUNNING_SUBFUNCTION;
    Set_Frame_Flag(sub, TRAMPOLINE_KEEPALIVE);
    return CATCH_CONTINUE_SUBFRAME(sub);

} run_next_in_chain: {  //////////////////////////////////////////////////////

    Frame(*) sub = SUBFRAME;
    if (Get_Frame_Flag(f, FAILURE_RESULT_OK))
        assert(Get_Frame_Flag(sub, FAILURE_RESULT_OK));

    if (sub->varlist and NOT_SERIES_FLAG(sub->varlist, MANAGED))
        GC_Kill_Series(sub->varlist);

    sub->varlist = nullptr;

    Value(*) pipeline_at = SPARE;
    Cell(const*) chained_tail;
    Cell(const*) chained = VAL_ARRAY_AT(&chained_tail, pipeline_at);

    if (chained == chained_tail)
        goto finished;

    ++VAL_INDEX_RAW(pipeline_at);

    Push_Action(sub, VAL_ACTION(chained), VAL_ACTION_BINDING(chained));

    Begin_Prefix_Action(sub, VAL_ACTION_LABEL(chained));
    assert(Not_Feed_Flag(sub->feed, NEXT_ARG_FROM_OUT));
    Set_Feed_Flag(sub->feed, NEXT_ARG_FROM_OUT);  // act like infix, see [4]

    FRM_STATE_BYTE(sub) = ST_ACTION_INITIAL_ENTRY;  // maybe zeroed (or not)?
    Clear_Executor_Flag(ACTION, sub, DISPATCHER_CATCHES);
    Clear_Executor_Flag(ACTION, sub, IN_DISPATCH);
    Clear_Frame_Flag(sub, NOTIFY_ON_ABRUPT_FAILURE);

    assert(STATE == ST_CHAINER_RUNNING_SUBFUNCTION);
    return CATCH_CONTINUE_SUBFRAME(sub);

} finished: {  ///////////////////////////////////////////////////////////////

    Drop_Frame(SUBFRAME);

    if (Is_Stale(OUT))
        return VOID;

    return OUT;
}}


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
DECLARE_NATIVE(chain_p)  // see extended definition CHAIN in %base-defs.r
{
    INCLUDE_PARAMS_OF_CHAIN_P;

    REBVAL *out = OUT;  // plan ahead for factoring into Chain_Action(out..

    REBVAL *pipeline = ARG(pipeline);
    Cell(const*) tail;
    Cell(const*) first = VAL_ARRAY_AT(&tail, pipeline);

    // !!! Current validation is that all are actions.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    Cell(const*) check = first;
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
    Action(*) chain = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(first)),  // same interface as first action
        ACT_PARTIALS(VAL_ACTION(first)),
        &Chainer_Dispatcher,
        IDX_CHAINER_MAX  // details array capacity
    );
    Force_Value_Frozen_Deep(pipeline);
    Copy_Cell(ARR_AT(ACT_DETAILS(chain), IDX_CHAINER_PIPELINE), pipeline);

    return Init_Action(out, chain, VAL_ACTION_LABEL(first), UNBOUND);
}
