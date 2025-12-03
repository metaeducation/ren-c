//
//  file: %c-chain.c
//  summary: "Function generator for making a pipeline of post-processing"
//  section: datatypes
//  project: "Ren-C Language Interpreter and Run-time Environment"
//  homepage: https://github.com/metaeducation/ren-c/
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
// CASCADE is used to create a function that matches the interface of a "first"
// function, and then pipes its results through to several subsequent
// post-processing actions:
//
//     >> negate-add: cascade [add/ negate/]
//
//     >> negate-add 2 2
//     == -4
//
// For more complex post-processing which may involve access to the original
// inputs to the first function (or other memory in the process), consider
// using ENCLOSE...which is less efficient, but more powerful.
//

#include "sys-core.h"

enum {
    IDX_CASCADER_PIPELINE = 1,  // BLOCK! of what should be all FRAME!
    MAX_IDX_CASCADER = IDX_CASCADER_PIPELINE
};


//
//  Push_Downshifted_Level: C
//
// When a derived function dispatcher receives a frame built for the function
// it derived from, sometimes it can do some work...update the phase...and
// keep running in that same Level* allocation.
//
// But if it wants to stay in control and do post-processing (as CASCADE does)
// then it needs to remain linked into the stack.  This function helps to
// move the built level into a new level that can be executed with a new
// entry to Process_Action().
//
// 1. Note that it can occur that this may be a TRAMPOLINE_KEEPALIVE sublevel
//    of something like another CASCADE, that it intends to reuse (!)  This
//    means it started out thinking we were going to run an action in that
//    frame and drop it, when in reality we're changing the executor and
//    everything.  This is clearly voodoo but maybe it can be formalized.
//
Level* Push_Downshifted_Level(Atom* out, Level* L) {
    assert(L->executor == &Action_Executor);

    Flags flags = ACTION_EXECUTOR_FLAG_IN_DISPATCH;

    require (
      Level* sub = Make_Level(&Action_Executor, L->feed, flags)
    );
    Push_Level_Erase_Out_If_State_0(out, sub);
    assert(sub->varlist == nullptr);
    sub->varlist = L->varlist;
    assert(Misc_Runlevel(sub->varlist) == L);
    Tweak_Misc_Runlevel(sub->varlist, sub);
    sub->rootvar = Array_Head(Varlist_Array(sub->varlist));

    L->varlist = nullptr;  // Note: may be TRAMPOLINE_KEEPALIVE! [1]
    Corrupt_If_Needful(L->rootvar);

    Corrupt_If_Needful(L->executor);  // caller must set
    Corrupt_If_Needful(L->u.action.label);
  #if DEBUG_LEVEL_LABELS
    L->label_utf8 = nullptr;
  #endif

    Corrupt_If_Needful(L->u);  // no longer action; corrupt after get stack base

    return sub;
}


//
//  Cascader_Executor: C
//
// The frame built for the CASCADE matches the arguments needed by the first
// function in the pipeline.  Having the same interface as that function
// makes a cascaded function specializable.
//
// The first CASCADE implementation did it all within one level.  It changed
// the Level_Phase() and returned a REDO signal--pushing actions to the data
// stack that the evaluator was complicit in processing as "things to run
// afterward".  This baked awareness of cascading into %c-action.c, when it is
// better if the process was localized in the dispatcher.
//
// Handling it in the dispatcher means the Cascader_Executor() stays on
// the stack and in control.  This means either unhooking the current `L` and
// putting a new Level* above it, or stealing the content of the `L` into a
// new level to put beneath it.  The latter is chosen to avoid disrupting
// existing pointers to `L`.
//
// (Having a separate level for the overall pipeline has an advantage in error
// messages too, as there is a level with the label of the function that the
// user invoked in the stack trace...instead of just the cascaded item that
// causes an error.)
//
Bounce Cascader_Executor(Level* const L)
{
    USE_LEVEL_SHORTHANDS (L);

    if (THROWING)  // Unlike dispatchers, *all* executors must handle throws
        return THROWN;

    enum {
        ST_CASCADER_INITIAL_ENTRY = STATE_0,
        ST_CASCADER_RUNNING_SUBFUNCTION
    };

    switch (STATE) {
      case ST_CASCADER_INITIAL_ENTRY: goto initial_entry;
      case ST_CASCADER_RUNNING_SUBFUNCTION: goto run_next_in_pipeline;
      default: assert(false);
    }

  initial_entry: {  //////////////////////////////////////////////////////////

    // 1. Stealing the varlist leaves the actual cascader frame w/no varlist
    //    content.  That means debuggers introspecting the stack may see a
    //    "stolen" frame state.
    //
    // 2. You can't have an Action_Executor() Level on the stack unless it
    //    has a lot of things (like a varlist, which provides the phase, etc.)
    //    We switch it around to where the level that had its varlist stolen
    //    uses Cascader_Executor() as its executor, so we get called back.
    //
    // 3. At the head of the pipeline we start at the dispatching phase since
    //    the frame is already filled, but each step after that uses infix and
    //    runs from the top.)

    Details* details = Ensure_Level_Details(L);
    assert(Details_Max(details) == MAX_IDX_CASCADER);

    Element* pipeline = Init_Block(
        SPARE,  // index of BLOCK! is current step
        Cell_Array(Details_At(details, IDX_CASCADER_PIPELINE))
    );

    Level* sub = Push_Downshifted_Level(OUT, L);  // steals varlist [1]
    L->executor = &Cascader_Executor;  // so trampoline calls us [2]

    const Element* first = List_Item_At(pipeline);
    ++SERIES_INDEX_UNBOUNDED(pipeline);  // point series index to next FRAME! to call

    Tweak_Level_Phase(
        sub,
        Phase_Details(Frame_Phase(first))  // has varlist already [3]
    );
    Tweak_Level_Coupling(sub, Frame_Coupling(first));

    sub->u.action.original = Frame_Phase(first);
    Set_Action_Level_Label(sub, Frame_Label_Deep(first));

    STATE = ST_CASCADER_RUNNING_SUBFUNCTION;
    Set_Level_Flag(sub, TRAMPOLINE_KEEPALIVE);
    return BOUNCE_DOWNSHIFTED;

} run_next_in_pipeline: {  ///////////////////////////////////////////////////

    // 1. We use the same mechanism as infix operations do...give the next
    //    cascade step its first argument coming from L->out.
    //
    //    !!! One side effect of this is that unless CASCADE changes to check,
    //    pipeline items can consume more than one argument.  Interesting,
    //    but it might be bugs waiting to happen, trying it this way for now.

    Level* sub = SUBLEVEL;
    if (sub->varlist and Not_Base_Managed(sub->varlist))
        GC_Kill_Flex(sub->varlist);

    sub->varlist = nullptr;

    Element* pipeline = Known_Element(SPARE);
    assert(Is_Block(pipeline));  // series indexes frame to call
    const Element* pipeline_tail;
    const Element* pipeline_at = List_At(&pipeline_tail, pipeline);

    if (pipeline_at == pipeline_tail)
        goto finished;

    ++SERIES_INDEX_UNBOUNDED(pipeline);  // update series index to next FRAME! to call

    Restart_Action_Level(sub);  // see notes
    require (
      Push_Action(sub, pipeline_at, PREFIX_0)
    );

    LEVEL_STATE_BYTE(sub) = ST_ACTION_INITIAL_ENTRY_INFIX;  // [1]
    Clear_Executor_Flag(ACTION, sub, DISPATCHER_CATCHES);
    Clear_Executor_Flag(ACTION, sub, IN_DISPATCH);

    assert(STATE == ST_CASCADER_RUNNING_SUBFUNCTION);
    return CONTINUE_SUBLEVEL(sub);

} finished: {  ///////////////////////////////////////////////////////////////

    // Note that Drop_Action() will not be called on LEVEL, because we
    // took over from Action_Executor().  The varlist should be nullptr.

    Drop_Level(SUBLEVEL);

    assert(L->varlist == nullptr);
    Assert_Corrupted_If_Needful(L->rootvar);

    return OUT;
}}


//
//  Cascader_Details_Querier: C
//
bool Cascader_Details_Querier(
    Sink(Value) out,
    Details* details,
    SymId property
){
    assert(Details_Dispatcher(details) == &Cascader_Executor);
    assert(Details_Max(details) == MAX_IDX_CASCADER);

    switch (property) {
      case SYM_RETURN_OF: {
        Element* pipeline = cast(Element*,
            Details_At(details, IDX_CASCADER_PIPELINE)
        );
        assert(Is_Block(pipeline));

        const Element* last = Array_Last(Cell_Array(pipeline));

        Details* last_details = Phase_Details(Frame_Phase(last));
        DetailsQuerier* querier = Details_Querier(last_details);
        return (*querier)(out, last_details, SYM_RETURN_OF); }

      default:
        break;
    }

    return false;
}


//
//  cascade*: native [
//
//  "Create a processing pipeline of actions, each consuming the last result"
//
//      return: [~[action!]~]
//      pipeline "Block of ACTION!s to apply (will be LOCKed)"
//          [block!]
//  ]
//
DECLARE_NATIVE(CASCADE_P)  // see extended CASCADE in %base-defs.r
//
// 1. !!! Current validation is that all are frames.  Should there be other
//    checks?  (That inputs match outputs in the pipeline?)  Should it be a
//    dialect and allow things other than functions?
//
// 2. !!! While the cascaded function has the same interface as head, its
//    RETURN should match the *tail* of the pipeline (TBD)
{
    INCLUDE_PARAMS_OF_CASCADE_P;

    Element* pipeline = Element_ARG(PIPELINE);
    const Element* tail;
    const Element* first = List_At(&tail, pipeline);

    const Element* check = first;
    for (; check != tail; ++check) {  // validate pipeline is all FRAME! [1]
        if (not Is_Frame(check)) {
            DECLARE_ATOM (specific);
            Derelativize(specific, check, List_Binding(pipeline));
            panic (specific);
        }
    }

    Details* details = Make_Dispatch_Details(
        BASE_FLAG_MANAGED,
        first,  // cascade has same interface as its first action [2]
        &Cascader_Executor,
        MAX_IDX_CASCADER  // details array capacity
    );
    Force_Value_Frozen_Shallow(pipeline);
    Copy_Cell(  // index of this block gets incremented as pipeline executes
        Details_At(details, IDX_CASCADER_PIPELINE),
        pipeline
    );

    Init_Action(OUT, details, Frame_Label_Deep(first), UNCOUPLED);

    return Packify_Action(OUT);
}
