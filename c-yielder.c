//
//  File: %c-yielder.c
//  Summary: "Routines for Creating Coroutine Functions via Stackless Methods"
//  Section: datatypes
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Revolt Open Source Contributors
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
// Generators utilize the ability of the system to suspend and resume stacks.
//

#include "sys-core.h"

enum {
    IDX_YIELDER_BODY = 0,  // Push_Continuation_Details_0() uses details[0]
    IDX_YIELDER_STATE = 1,  // can't be frame spare (that's reset each call!)
    IDX_YIELDER_LAST_YIELDER_CONTEXT = 2,  // frame stack fragment to resume
    IDX_YIELDER_LAST_YIELD_RESULT = 3,  // so that `z: yield 1 + 2` is useful
    IDX_YIELDER_PLUG = 4,  // saved if you YIELD, captures data stack etc.
    IDX_YIELDER_OUT = 5,  // whatever f->out in-progress was when interrupted
    IDX_YIELDER_CHAINS = 6,  // could share space with IDX_YIELDER_OUT
    IDX_YIELDER_MAX
};


//
//  Yielder_Dispatcher: C
//
// A yielder is a function instance which is made by a generator, that keeps
// a memory of the frame state it was in.  YIELD packs up the frame in a
// restartable way and unwinds it, allowing the continuation to request
// that be the frame that gets executed in the continuation.
//
REB_R Yielder_Dispatcher(REBFRM *f)
{
    REBFRM *f = frame_;

    enum {
        ST_YIELDER_WAS_INVOKED = 0,
        ST_YIELDER_RUNNING_BODY
    };

    REBACT *phase = FRM_PHASE(f);
    REBARR *details = ACT_DETAILS(phase);
    RELVAL *state = ARR_AT(details, IDX_YIELDER_STATE);

    switch (STATE_BYTE) {
      case ST_YIELDER_WAS_INVOKED: goto invoked;
      case ST_YIELDER_RUNNING_BODY: goto body_finished_or_threw;
      default: assert(false);
    }

  invoked: {
    //
    // Because yielders accrue state as they run, more than one can't be in
    // flight at a time.  Hence what would usually be an "initial entry" of
    // a new call for other dispatchers, each call is effectively to the same
    // "instance" of this yielder.  So the ACT_DETAILS() is modified while
    // running, and it's the `state` we pay attention to.
    //
    // (Note the frame state is in an array, and thus can't be NULL.)

    if (Is_None(state))  // currently on the stack and running
        return Init_Thrown_Failure(f->out, Error_Yielder_Reentered_Raw());

    if (IS_LOGIC(state)) {  // terminated due to finishing the body or error
        if (VAL_LOGIC(state))
            return nullptr;

        return Init_Thrown_Failure(f->out, Error_Yielder_Errored_Raw());
    }

    // If there's any accrued state in the yielder (e.g. CHAINs to run) then
    // we want that state to apply on the YIELD.  Consider:
    //
    //    g: generator [yield 1]
    //    c: chain [:g | func [x] [if x [x + 1]]
    //
    // You would like a call to `c` to generate 2 on the first call.  This
    // means that since the YIELD is actually what produces the result of `g`,
    // the chain stack has to be in effect when the YIELD returns.  So we
    // take the extra stack and store it to be reused on the YIELD (as well as
    // when the generator finishes).
    //
    // Note: This might be doable if the Unplug/Replug stack mechanics were
    // changed to plug in to a baseline of the stack level above the yielder.
    // Then any state the yielder had in effect initially would be kept (e.g.
    // mold buffers and other things too).  But that might break tasks; so
    // review the Plug/Unplug API as tasks mature to see if that's viable.
    //
    assert(f == FS_TOP);
    RELVAL *chains = ARR_AT(details, IDX_YIELDER_CHAINS);
    assert(Is_Trash(chains));
    if (DSP == f->baseline.dsp)
        Init_Blank(chains);
    else
        Init_Block(chains, Pop_Stack_Values(f->baseline.dsp));

    if (IS_FRAME(state))  // we were suspended by YIELD, and want to resume
        goto resume_body;

    assert(IS_BLANK(state));  // set by the YIELDER creation routine
    goto first_run;
  }

  first_run: {
    //
    // Whatever we pass through here as the specifier has to stay working,
    // because it will be threaded and preserved in variables by the
    // running code (also, it's the binding of the YIELD statement, which
    // needs to be able to find the right frame).
    //
    // If there is no yield, we want a callback so we can mark the
    // generator as finished.
    //
    Push_Continuation_Details_0_Core(
        f->out,
        f,
        EVAL_FLAG_DISPATCHER_CATCHES  // we want to see throws and errors
    );
    STATE_BYTE(f) = ST_YIELDER_RUNNING_BODY;
    Init_None(state);  // indicate "running"
    return R_CONTINUATION;
  }

  resume_body: {
    assert(IS_FRAME(state));

    REBFRM *yielder_frame = f;  // alias for clarity
    REBFRM *yield_frame = CTX_FRAME_IF_ON_STACK(VAL_CONTEXT(state));
    assert(yield_frame != nullptr);

    // The YIELD binding pointed to the context varlist we used in the
    // original yielder dispatch.  That completed--but we need to reuse
    // the identity for this new yielder frame for the YIELD to find it
    // in the stack walk.
    //
    REBCTX *last_yielder_context = VAL_CONTEXT(
        ARR_AT(details, IDX_YIELDER_LAST_YIELDER_CONTEXT)
    );

    // We want the identity of the old varlist to replace this yielder's
    // varlist identity.  But we want the frame's values to reflect the
    // args the user passed in to this invocation of the yielder.  So move
    // those into the old varlist before replacing this varlist with that
    // prior identity.
    //
    REBVAL *param = CTX_KEYS_HEAD(last_yielder_context);
    REBVAL *dest = CTX_VARS_HEAD(last_yielder_context);
    REBVAL *src = F_ARGS_HEAD(yielder_frame);
    for (; NOT_END(src); ++param, ++dest, ++src) {
        if (VAL_PARAM_CLASS(param) == REB_P_LOCAL)
            continue;  // don't overwrite locals (including YIELD)
        Move_Cell(dest, src);  // all arguments/refinements are fair game
    }
    assert(IS_END(src));
    assert(IS_END(dest));

    // With variables extracted, we no longer need the varlist for this
    // invocation (wrong identity) so we free it, if it isn't GC-managed,
    // as it wouldn't get freed otherwise.
    //
    if (NOT_SERIES_FLAG(yielder_frame->varlist, MANAGED)) {
        //
        // We only want to kill off this one frame; but the GC will think
        // that we want to kill the whole stack of frames if we don't
        // zero out the keylist node.
        //
        LINK(yielder_frame->varlist).custom.node = nullptr;

        GC_Kill_Series(SER(yielder_frame->varlist));  // Note: no tracking
    }

    // When the last yielder dropped from the frame stack, it should have
    // decayed its keysource from a REBFRM* to the action that was
    // invoked (which could be an arbitrary specialization--e.g. different
    // variants of the yielder with different f_original could be used
    // between calls).  This means we can only compare underlying actions.
    //
    // Now we have a new REBFRM*, so we can reattach the context to that.
    //
    assert(
        ACT_UNDERLYING(ACT(LINK_KEYSOURCE(last_yielder_context)))
        == ACT_UNDERLYING(F_ORIGINAL(yielder_frame))
    );
    INIT_LINK_KEYSOURCE(last_yielder_context, NOD(yielder_frame));

    // Now that the last call's context varlist is pointing at our current
    // invocation frame, we point the other way from the frame to the
    // varlist.  We also update the cached pointer to the rootvar of that
    // frame (used to speed up F_PHASE() and F_BINDING())
    //
    f->varlist = CTX_VARLIST(last_yielder_context);
    f->rootvar = CTX_ARCHETYPE(last_yielder_context);  // must match

    RELVAL *plug = SPECIFIC(ARR_AT(details, IDX_YIELDER_PLUG));
    Replug_Stack(yield_frame, yielder_frame, SPECIFIC(plug));
    Init_Unreadable_Void(plug);  // Replug trashes, make GC safe

    // Restore the in-progress output cell state that was going on when
    // the YIELD ran (e.g. if it interrupted a CASE or something, this
    // would be what the case had in the out cell at moment of interrupt).
    // Note special trick used to encode END inside an array by means of
    // using the hidden identity of the details array itself.
    //
    REBVAL *out_copy = SPECIFIC(ARR_AT(details, IDX_YIELDER_OUT));
    if (
        KIND3Q_BYTE_UNCHECKED(out_copy) == REB_BLOCK
        and VAL_ARRAY(out_copy) == details
    ){
        SET_END(yielder_frame->out);
    }
    else
        Move_Cell(yielder_frame->out, out_copy);
    if (out_copy->header.bits & CELL_FLAG_OUT_MARKED_STALE)
        yielder_frame->out->header.bits |= CELL_FLAG_OUT_MARKED_STALE;

    // We could make YIELD appear to return a VOID! when we jump back in
    // to resume it.  But it's more interesting to return what the YIELD
    // received as an arg (YIELD cached it in details before jumping)
    //
    Move_Value(
        yield_frame->out,
        SPECIFIC(ARR_AT(details, IDX_YIELDER_LAST_YIELD_RESULT))
    );

    // If the yielder actually reaches its end (instead of YIELD-ing)
    // we need to know, so we can mark that it is finished.
    //
    assert(NOT_EVAL_FLAG(yielder_frame, DELEGATE_CONTROL));

    STATE_BYTE(yielder_frame) = ST_YIELDER_RUNNING_BODY;  // need to set again
    SET_EVAL_FLAG(yielder_frame, DISPATCHER_CATCHES);  // need to set again
    Init_None(state);  // indicate running
    return R_DEWIND;  // ...resuming where we left off
  }

  body_finished_or_threw: {
    //
    // Apply pending CHAINs to this completion (see notes above).
    //
    assert(f == FS_TOP);
    RELVAL *chains = ARR_AT(details, IDX_YIELDER_CHAINS);
    if (IS_BLOCK(chains))
        Push_Stack_Values(VAL_ARRAY(chains));
    else
        assert(IS_BLANK(chains));

    // Clean up all the details fields so the GC can reclaim the memory
    //
    Init_Trash(ARR_AT(details, IDX_YIELDER_LAST_YIELDER_CONTEXT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_LAST_YIELD_RESULT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_PLUG));
    Init_Trash(ARR_AT(details, IDX_YIELDER_OUT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_CHAINS));

    if (Is_Throwing(f)) {
        if (IS_ERROR(VAL_THROWN_LABEL(f->out))) {
            //
            // We treat a failure as if it was an invalid termination of the
            // yielder.  Future calls will raise an error.
            //
            Init_False(ARR_AT(details, IDX_YIELDER_STATE));
        }
        else {
            // We treat a throw as if it was a valid termination of the
            // yielder (e.g. a RETURN which crosses out of it).  Future calls
            // will return NULL.
            //
            Init_True(ARR_AT(details, IDX_YIELDER_STATE));
        }
        return R_THROWN;
    }

    Init_True(ARR_AT(details, IDX_YIELDER_STATE));  // finished successfully
    return nullptr;  // the true signals return NULL for all future calls
  }
}


//
//  yielder: native [
//
//      return: "Action that can be called repeatedly until it yields NULL"
//          [action!]
//      spec "Arguments passed in to each call for the generator"
//          [block!]
//      body "Code containing YIELD statements"
//          [block!]
//  ]
//
REBNATIVE(yielder)
{
    INCLUDE_PARAMS_OF_YIELDER;

    // We start by making an ordinary-seeming interpreted function, but that
    // has a local "yield" which is bound to the frame upon execution.
    //
    REBVAL *body = rebValue("compose [",
        "let yield: bind :lib.yield binding of 'yield",
        "(as group!", ARG(body), ")",  // GROUP! so it can't backquote 'YIELD
    "]", rebEND);

    REBACT *yielder = Make_Interpreted_Action_May_Fail(
        ARG(spec),
        body,
        MKF_KEYWORDS,  // no RETURN (similar to DOES)
        IDX_YIELDER_MAX  // details array capacity
    );
    rebRelease(body);

    // !!! Make_Interpreted_Action_May_Fail() does not take a dispatcher
    // argument, because it makes a decision on if an optimized one should
    // be used based on the paramlist.  So we override it, but this points
    // out some dissonance: e.g. what would a `return: <void>` yielder mean?

    REBARR *details = ACT_DETAILS(yielder);
    MISC(details).dispatcher = &Yielder_Dispatcher;

    assert(IS_BLOCK(ARR_AT(details, IDX_YIELDER_BODY)));
    Init_Blank(ARR_AT(details, IDX_YIELDER_STATE));  // starting
    Init_Trash(ARR_AT(details, IDX_YIELDER_LAST_YIELDER_CONTEXT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_LAST_YIELD_RESULT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_PLUG));
    Init_Trash(ARR_AT(details, IDX_YIELDER_OUT));
    Init_Trash(ARR_AT(details, IDX_YIELDER_CHAINS));

    return Init_Action_Unbound(OUT, yielder);
}


//
//  generator: native [
//
//      return: "Arity-0 action you can call repeatedly until it yields NULL"
//          [action!]
//      body "Code containing YIELD statements"
//          [block!]
//  ]
//
REBNATIVE(generator)
{
    INCLUDE_PARAMS_OF_GENERATOR;

    return rebValue(Lib(YIELDER), EMPTY_BLOCK, ARG(body));
}


//
//  yield: native [
//
//  {Function used with GENERATOR and YIELDER to give back results}
//
//      return: "Same value given as input, won't return until resumption"
//          [<opt> any-value!]
//      value "Value to yield (null is no-op)"
//          [<opt> any-value!]
//  ]
//
REBNATIVE(yield)
//
// The benefits of distinguishing NULL as a generator result meaning the body
// has completed are considered to outweigh the ability to yield NULL.  A
// modified generator that yields quoted values and unquotes on exit points
// can be used to work around this.
{
    INCLUDE_PARAMS_OF_YIELD;

    assert(frame_ == TG_Top_Frame);  // frame_ is an implicit REBNATIVE() arg
    assert(FRM_PHASE(frame_) == NATIVE_ACT(yield));
    REBFRM * const yield_frame = frame_;  // ...make synonyms more obvious

    REBNOD *yield_binding = FRM_BINDING(yield_frame);
    if (not yield_binding)
        fail (Error_Yield_Archetype_Raw());  // must have yielder to jump to

    REBCTX *yielder_context = CTX(yield_binding);
    REBFRM *yielder_frame = CTX_FRAME_MAY_FAIL(yielder_context);
    if (not yielder_frame)
        fail ("Cannot yield to generator that has completed");

    REBACT *yielder_phase = FRM_PHASE(yielder_frame);
    assert(ACT_DISPATCHER(yielder_phase) == &Yielder_Dispatcher);

    // !!! How much sanity checking should be done before doing the passing
    // thru of the NULL?  Err on the side of safety first, and don't let NULL
    // be yielded to the unbound archetype or completed generators.
    //
    if (IS_NULLED(ARG(value)))
        return nullptr;

    REBARR *yielder_details = ACT_DETAILS(yielder_phase);

    // Evaluations will frequently use the f->out to accrue state, perhaps
    // preloading with something (like NULL) that is expected to be there.
    // But we're interrupting the frame and returning what YIELD had instead
    // of that evaluative product.  It must be preserved.  But since we can't
    // put END values in blocks, use the hidden block to indicate that
    //
    REBVAL *out_copy = SPECIFIC(ARR_AT(yielder_details, IDX_YIELDER_OUT));
    if (IS_END(yielder_frame->out))
        Init_Block(out_copy, yielder_details);  // special identity
    else
        Move_Cell(out_copy, yielder_frame->out);
    if (yielder_frame->out->header.bits & CELL_FLAG_OUT_MARKED_STALE)
        out_copy->header.bits |= CELL_FLAG_OUT_MARKED_STALE;

    RELVAL *plug = ARR_AT(yielder_details, IDX_YIELDER_PLUG);
    assert(Is_Trash(plug));
    Unplug_Stack(plug, yield_frame, yielder_frame);

    // We preserve the fragment of call stack leading from the yield up to the
    // yielder in a FRAME! value that the yielder holds in its `details`.
    // The garbage collector should notice it is there, and mark it live up
    // until the nullptr that we put at the root.
    //
    RELVAL *state = ARR_AT(yielder_details, IDX_YIELDER_STATE);
    assert(Is_None(state));  // should be the signal for "currently running"
    Init_Frame(state, Context_For_Frame_May_Manage(yield_frame));
    ASSERT_ARRAY_MANAGED(VAL_CONTEXT(state));
    assert(CTX_FRAME_IF_ON_STACK(VAL_CONTEXT(state)) == yield_frame);

    // We store the frame chain into the yielder, as a FRAME! value.  The
    // GC of the ACTION's details will keep it alive.
    //
    Init_Frame(
        ARR_AT(yielder_details, IDX_YIELDER_LAST_YIELDER_CONTEXT),
        yielder_context
    );

    // The Init_Frame() should have managed the yielder_frame varlist, which
    // means that when the yielder does Drop_Frame() yielder_context survives.
    // It should decay the keysource from a REBFRM* to the action paramlist,
    // but the next run of the yielder will swap in its new REBFRM* over that.
    //
    assert(CTX_VARLIST(yielder_context) == yielder_frame->varlist);
    ASSERT_ARRAY_MANAGED(yielder_frame->varlist);

    // We don't only write the yielded value into the output slot so it is
    // returned from the yielder.  We also stow an extra copy of the value
    // into the yielder details, which we use to make it act act as the
    // apparent return result of the YIELD when the yielder is called again.
    //
    //    x: yield 1 + 2
    //    print [x]  ; could be useful if this was 3 upon resumption, right?
    //
    Move_Cell(yielder_frame->out, ARG(value));
    Move_Cell(
        ARR_AT(yielder_details, IDX_YIELDER_LAST_YIELD_RESULT),
        ARG(value)
    );

    // Apply pending chains to this YIELD (see notes in YIELDER).
    //
    RELVAL* chains = ARR_AT(yielder_details, IDX_YIELDER_CHAINS);
    if (IS_BLOCK(chains))
        Push_Stack_Values(VAL_ARRAY(chains));
    else
        assert(IS_BLANK(chains));
    Init_Trash(chains);

    /* REBACT *target_fun = FRM_UNDERLYING(target_frame); */

    return R_DEWIND;
}
