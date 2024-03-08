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
//  Push_Downshifted_Level: C
//
// When a derived function dispatcher receives a frame built for the function
// it derived from, sometimes it can do some work...update the phase...and
// keep running in that same Level* allocation.
//
// But if it wants to stay in control and do post-processing (as CHAIN does)
// then it needs to remain linked into the stack.  This function helps to
// move the built level into a new level that can be executed with a new
// entry to Process_Action().  The ability is also used by RESKINNED.
//
Level* Push_Downshifted_Level(Atom* out, Level* L) {
    assert(L->executor == &Action_Executor);

    Flags flags = ACTION_EXECUTOR_FLAG_IN_DISPATCH;
    flags |= L->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK;

    Level* sub = Make_Level(&Action_Executor, L->feed, flags);
    Push_Level(out, sub);
    assert(sub->varlist == nullptr);
    sub->varlist = L->varlist;
    assert(BONUS(KeySource, sub->varlist) == L);
    INIT_BONUS_KEYSOURCE(sub->varlist, sub);
    sub->rootvar = Array_Head(sub->varlist);

    // Note that it can occur that this may be a TRAMPOLINE_KEEPALIVE sublevel
    // of something like another CHAIN, that it intends to reuse (!)  This
    // means it started out thinking we were going to run an action in that
    // frame and drop it, when in reality we're changing the executor and
    // everything.  This is clearly voodoo but maybe it can be formalized.
    //
    L->varlist = nullptr;
    Corrupt_Pointer_If_Debug(L->rootvar);

    Corrupt_Function_Pointer_If_Debug(L->executor);  // caller must set
    Corrupt_Pointer_If_Debug(L->label);

    sub->u.action.dispatcher_base = L->u.action.dispatcher_base;

    Corrupt_If_Debug(L->u);  // no longer action; corrupt after get stack base

    return sub;
}


//
//  Chainer_Dispatcher: C
//
// The frame built for the CHAIN matches the arguments needed by the first
// function in the pipeline.  Having the same interface as that function
// makes a chained function specializable.
//
// A first cut at implementing CHAIN did it all within one level.  It changed
// the Level_Phase() and returned a REDO signal--pushing actions to the data
// stack that the evaluator was complicit in processing as "things to run
// afterward".  This baked awareness of chaining into %c-action.c, when it is
// better if the process was localized in the dispatcher.
//
// Handling it in the dispatcher means the Chainer_Dispatcher() stays on
// the stack and in control.  This means either unhooking the current `L` and
// putting a new Level* above it, or stealing the content of the `L` into a
// new level to put beneath it.  The latter is chosen to avoid disrupting
// existing pointers to `L`.
//
// (Having a separate level for the overall chain has an advantage in error
// messages too, as there is a level with the label of the function that the
// user invoked in the stack trace...instead of just the chained item that
// causes an error.)
//
Bounce Chainer_Dispatcher(Level* const L)
//
// 1. Stealing the varlist leaves the actual chainer frame with no varlist
//    content.  That means debuggers introspecting the stack may see a
//    "stolen" frame state.
//
// 2. You can't have an Action_Executor()-based frame on the stack unless it
//    has a lot of things (like a varlist, which provides the phase, etc.)
//    So we switch it around to where the level that had its varlist stolen
//    just uses Chainer_Dispatcher() as its executor, so we get called back.
//
// 3. At the head of the chain we start at the dispatching phase since the
//    frame is already filled, but each step after that uses enfix and runs
//    from the top.)
//
// 4. We use the same mechanism as enfix operations do...give the next chain
//    step its first argument coming from L->out.
//
//    !!! One side effect of this is that unless CHAIN is changed to check,
//    your chains can consume more than one argument.  It might be interesting
//    or it might be bugs waiting to happen, trying it this way for now.
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)  // this routine is both dispatcher and executor [2]
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

    Details* details = Phase_Details(PHASE);
    assert(Array_Len(details) == IDX_CHAINER_MAX);

    Value* pipeline_at = Init_Block(
        SPARE,  // index of BLOCK! is current step
        Cell_Array(Details_At(details, IDX_CHAINER_PIPELINE))
    );

    Level* sub = Push_Downshifted_Level(OUT, L);  // steals varlist [1]
    L->executor = &Chainer_Dispatcher;  // so trampoline calls us [2]

    const Cell* chained = Cell_Array_Item_At(pipeline_at);
    ++VAL_INDEX_RAW(pipeline_at);

    INIT_LVL_PHASE(
        sub,
        ACT_IDENTITY(VAL_ACTION(chained))  // has varlist already [3]
    );
    INIT_LVL_TARGET(sub, VAL_FRAME_TARGET(chained));

    sub->u.action.original = VAL_ACTION(chained);
    sub->label = VAL_FRAME_LABEL(chained);
  #if !defined(NDEBUG)
    sub->label_utf8 = sub->label
        ? String_UTF8(unwrap(sub->label))
        : "(anonymous)";
  #endif

    STATE = ST_CHAINER_RUNNING_SUBFUNCTION;
    Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);
    return CATCH_CONTINUE_SUBLEVEL(sub);

} run_next_in_chain: {  //////////////////////////////////////////////////////

    Level* sub = SUBLEVEL;
    if (Get_Level_Flag(L, RAISED_RESULT_OK))
        assert(Get_Level_Flag(sub, RAISED_RESULT_OK));

    if (sub->varlist and Not_Node_Managed(sub->varlist))
        GC_Kill_Series(sub->varlist);

    sub->varlist = nullptr;

    assert(Is_Block(SPARE));
    Value* pipeline_at = cast(Value*, SPARE);
    const Element* chained_tail;
    const Element* chained = Cell_Array_At(&chained_tail, pipeline_at);

    if (chained == chained_tail)
        goto finished;

    ++VAL_INDEX_RAW(pipeline_at);

    Restart_Action_Level(sub);  // see notes
    Push_Action(sub, VAL_ACTION(chained), VAL_FRAME_TARGET(chained));

    Begin_Prefix_Action(sub, VAL_FRAME_LABEL(chained));

    Level_State_Byte(sub) = ST_ACTION_INITIAL_ENTRY_ENFIX;  // [4]
    Clear_Executor_Flag(ACTION, sub, DISPATCHER_CATCHES);
    Clear_Executor_Flag(ACTION, sub, IN_DISPATCH);
    Clear_Level_Flag(sub, NOTIFY_ON_ABRUPT_FAILURE);

    assert(STATE == ST_CHAINER_RUNNING_SUBFUNCTION);
    return CATCH_CONTINUE_SUBLEVEL(sub);

} finished: {  ///////////////////////////////////////////////////////////////

    // Note that Drop_Action() will not be called on LEVEL, because we
    // took over from Action_Executor().  The varlist should be nullptr.

    Drop_Level(SUBLEVEL);

    assert(L->varlist == nullptr and Is_Pointer_Corrupt_Debug(L->rootvar));

    return OUT;
}}


//
//  chain*: native [
//
//  "Create a processing pipeline of actions, each consuming the last result"
//
//      return: [action?]
//      pipeline "Block of ACTION!s to apply (will be LOCKed)"
//          [block!]
//  ]
//
DECLARE_NATIVE(chain_p)  // see extended definition CHAIN in %base-defs.r
{
    INCLUDE_PARAMS_OF_CHAIN_P;

    Atom* out = OUT;  // plan ahead for factoring into Chain_Action(out..

    Element* pipeline = cast(Element*, ARG(pipeline));
    const Element* tail;
    const Element* first = Cell_Array_At(&tail, pipeline);

    // !!! Current validation is that all are frames.  Should there be other
    // checks?  (That inputs match outputs in the chain?)  Should it be
    // a dialect and allow things other than functions?
    //
    const Element* check = first;
    for (; check != tail; ++check) {
        if (not Is_Frame(check)) {
            DECLARE_ATOM (specific);
            Derelativize(specific, check, Cell_Specifier(pipeline));
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
    Phase* chain = Make_Action(
        ACT_PARAMLIST(VAL_ACTION(first)),  // same interface as first action
        ACT_PARTIALS(VAL_ACTION(first)),
        &Chainer_Dispatcher,
        IDX_CHAINER_MAX  // details array capacity
    );
    Force_Value_Frozen_Deep(pipeline);
    Copy_Cell(Array_At(Phase_Details(chain), IDX_CHAINER_PIPELINE), pipeline);

    return Init_Action(out, chain, VAL_FRAME_LABEL(first), UNBOUND);
}
