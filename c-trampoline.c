//
//  File: %c-trampoline.c
//  Summary: "Central Interpreter Loop for 'Stackless' Evaluation"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2020 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This is the main loop of the interpreter.  We call this a "trampoline", in
// the spirit of the word as used in Lisp implementations.  That's because
// sub-expressions aren't evaluated with direct recursions of a C evaluator
// function, but using C's `return` to "bounce back" to a single loop,
// which invokes returned continuations.  Hence, there are no nested function
// calls and the stack won't grow:
//
// https://en.wikipedia.org/wiki/Trampoline_(computing)#High-level_programming
//

#include "sys-core.h"


#if defined(DEBUG_COUNT_TICKS)  // <-- THIS IS VERY USEFUL, SEE %sys-eval.h!
    //
    // This counter is incremented each time a function dispatcher is run
    // or a parse rule is executed.  See UPDATE_TICK_COUNT().
    //
    REBTCK TG_Tick;

    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    REBTCK TG_Break_At_Tick =      0;
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***

#endif  // ^-- SERIOUSLY: READ ABOUT C-DEBUG-BREAK AND PLACES TICKS ARE STORED


enum {
    ST_CLEANER_RUNNING_THROWN = 88,
    ST_CLEANER_RUNNING_NORMAL = 101,
    ST_CLEANER_FINISHED = 255
};


//
//  Cleaner_Executor: C
//
// Whether a frame has succeeded or failed, the Cleanup_Executor() has to
// be run on it.  This will run any DEFER functions.  It should be the last
// executor that is put into effect.
//
REB_R Cleaner_Executor(REBFRM *f)
{
    switch (STATE_BYTE(f)) {
      case ST_CLEANER_RUNNING_THROWN: goto process_next_deferred;
      case ST_CLEANER_RUNNING_NORMAL: goto process_next_deferred;
      default: assert(false);
    }

    // !!! This might be the best place to complain about leaked API handles,
    // based on the ABRUPT_FAILURE status of the frame (?)
    //
  process_next_deferred: {
    REBNOD *n = f->alloc_value_list;
    for (; n != NOD(f); n = LINK(ARR(n)).custom.node) {
        REBARR *a = ARR(n);
        if (NOT_ARRAY_FLAG(a, DEFERRED_CODE))
            continue;

        REBVAL *code = SPECIFIC(ARR_SINGLE(a));

        Push_Continuation_With(
            F_SPARE(f),  // !!! Making non f->out legal output is WIP
            f,
            EVAL_FLAG_DISPATCHER_CATCHES,
            code,  // gets copied to new frame so freeing is not a problem
            END_NODE  // no /WITH for block execution
        );
        Free_Value(code);

        return R_CONTINUATION;
    }

    if (STATE_BYTE(f) == ST_CLEANER_RUNNING_THROWN) {
        Init_Thrown_With_Label(f->out, f->out, DS_TOP);
        DS_DROP();
        STATE_BYTE(f) = ST_CLEANER_FINISHED;
        return R_THROWN;
    }
    assert(STATE_BYTE(f) == ST_CLEANER_RUNNING_NORMAL);
    STATE_BYTE(f) = ST_CLEANER_FINISHED;
    return f->out;  // we should not have changed f->out
  }
}


//
//  Just_Use_Out_Executor: C
//
// This is a simplistic executor that can be used in cases that hold frames
// alive on the stack and want to be bypassed, or if it's easier to push a
// "no-op" frame than to special-case handling of not pushing a frame.
//
// Note: The branch continuations consider the "no frame necessary for
// QUOTED!s or BLANK!s to be worth it to special-case, vs. pushing this.
//
REB_R Just_Use_Out_Executor(REBFRM *f)
{
    if (Is_Throwing(f))
        return R_THROWN;
    return f->out;
}


//
//  Trampoline_Throws: C
//
// !!! The end goal is that this function is never found recursively on a
// standard evaluation stack.  The only way it should be found on the stack
// more than once would be to call out to non-Rebol code, which then turned
// around and made an API call back in...it would not be able to gracefully
// unwind across such C stack frames.  In the interim, not all natives have
// been rewritten as state machines.
//
// !!! There was an old concept that the way to write a stepwise debugger
// would be to replace this function in such a way that it would do some work
// related to examining the "pre" state of a frame... delegate to the "real"
// eval function... and then look at the end result after that call.  This
// meant hooking every recursion.  The new idea would be to make this
// "driver" easier to rewrite in its entirety, and examine the frame state
// as continuations are run.  This is radically different, and is requiring
// rethinking during the stackless transition.
//
bool Trampoline_Throws(REBFRM *f)
{
    // The instigating call to this function cannot be unwound across, as it
    // represents a "stackful" invocation of the evaluator.  YIELD must know
    // the passed-in frame is uncrossable, so that it can raise an error if
    // you try to unwind the Revolt stack across a top-level Trampoline call.
    //
    // (It's more efficient for the caller to set the bit in one assignment
    // with the other header bits it sets--so just have the debug build check
    // to make sure they did so.)
    //
    // !!! There could be a "promise" variant which didn't expect a concrete
    // result back, but was willing to accept a frame stack that would run
    // later to provide the result.  For now, we consider this a barrier.
    //
    assert(GET_EVAL_FLAG(f, ROOT_FRAME));

    // In theory, a caller could push several frames to be evaluated, and
    // the passed in `f` would just be where evaluation should *stop*.  No
    // cases of this exist yet, but the `f = FS_TOP` below would allow it.
    //
    assert(f == FS_TOP);

  push_again: ;

    // There is only one setjmp() point for each trampoline invocation.  Any
    // frame that is interrupted at an arbitrary moment by a fail() will be
    // "teleported" up to this point.  The running C stack variables will be
    // lost, but the frame stack will still be intact.
    //
    // Only the topmost frame may raise an error.  This means that if a frame
    // pushes another frame to do work with EVAL_FLAG_TRAMPOLINE_KEEPALIVE,
    // that must be dropped before failing.
    //
    // A *cooperative* failure is done by raising the error and returning it
    // like a throw.  This form of failure assumes balance in the frame was
    // achieved before returning, and the frame will be considered done.  If
    // EVAL_FLAG_TRAMPOLINE_KEEPALIVE wasn't used, it will be dropped.
    //
    // On the other hand, an *uncooperative* failure can happen at any moment,
    // even due to something like a failed memory allocation requested by
    // the executor itself.  As evidenced by fail()s in an Action_Executor()
    // which are caused by subdispatch to a native, the executor must get a
    // chance to clean up after fails that happen on its watch.

    struct Reb_Jump jump;
    PUSH_TRAP_SO_FAIL_CAN_JUMP_BACK_HERE(&jump);

    // The first time through the following code 'error' will be null, but...
    // `fail` can longjmp here, so 'error' won't be null *if* that happens!
    //
    if (jump.error) {

        // The mechanisms for THROW-ing and FAIL-ing are somewhat unified in
        // stackless...(a TRAPpable failure is just any "thrown" value with
        // a VAL_THROWN_LABEL() which is an ERROR!).  So the trampoline just
        // converts the longjmp into a throw.

        Init_Thrown_With_Label(
            FS_TOP->out,
            NULLED_CELL,  // no "thrown value"
            CTX_ARCHETYPE(jump.error)  // only the ERROR! as a label
        );

        goto push_again;
    }

    // This assignment is needed to avoid `f could be clobbered by longjmp`
    // warning (see also note about how it would facilitate a caller who
    // pushed more stack levels and didn't pass FS_TOP as initial parameter).
    //
    f = FS_TOP;

  bounce: ;  // ...on the trampoline.  :-)

    REB_R r;

    // Currently we do the signals *before* the executor is called, because
    // if we did it after then we might see a frame whose handling is to be
    // dropped...and it may confuse the GC if seen still in the stack.  This
    // is because frames are identified by their executor, and the executor
    // is no longer set to null before returning values.
    //
    assert(Eval_Count >= 0);
    if (--Eval_Count == 0) {
        //
        // Note that Do_Signals_Throws() may do a recycle step of the GC,
        // or may spawn an entire interactive debugging session via
        // breakpoint before it returns.  May also FAIL and longjmp out.
        //
        // We can't just test on the `nullptr` case of finishing an executor
        // result, because that would not provide termination in something
        // that was deeply tunneling with no resolution.
        //
        // The F_SPARE() is passed in to be used for the location to write
        // a throw, but shouldn't be written unless a throw happens...because
        // the spare cell is in use by the executor.
        //
        if (Do_Signals_Throws(F_SPARE(f))) {  // see note on F_SPARE()
            Move_Value(f->out, F_SPARE(f));
            r = R_THROWN;
            goto thrown;
        }
    }


    UPDATE_TICK_DEBUG(f, nullptr);

    // v-- This is the TG_Break_At_Tick or C-DEBUG-BREAK landing spot --v

    // CALL THE EXECUTOR
    //
    // It is expected that all executors are able to handle the Is_Throwing()
    // state, even if just to pass it through.  The executor may push more
    // frames or change the executor of the frame it receives.
    //

    r = (f->executor)(f);  // Note: f may not be FS_TOP at this moment

    if (r == R_CONTINUATION) {
        //
        // The frame stack is singly-linked from lower stacks to higher.  Yet
        // the GC needs to find the bottom of stacks when sweeping, in order
        // to gracefully unwind suspended stacks (e.g. a GENERATOR' YIELD)
        // that have not been marked as "in use".
        //
        // A cheap concept which also helps a bit with error checking is to
        // say that all continuations have nonzero state bytes.  Then things
        // like YIELD will be at state byte zero: the root of an unwind.
        //
        // !!! This test being specific about YIELD is really something that
        // needs to account for usages of R_DEWIND, or something abstract, but
        // it works for the moment.
        //
        assert(
            f == FS_TOP
            or STATE_BYTE(f) != 0
            or (Is_Action_Frame(f) and F_PHASE(f) == NATIVE_ACT(yield))
        );

        f = FS_TOP;
        goto bounce;
    }

    f = FS_TOP;  // A return of "f->out" is in terms of the topmost frame

    if (r == f->out) {  // no further execution for frame, drop it
      #if !defined(NDEBUG)
        Eval_Core_Exit_Checks_Debug(f);   // called unless a fail() longjmps
      #endif

        // !!! This is going to be the right place to handle other variants of
        // return values consistently, e.g. API handles.  The return results
        // from native dispatchers may be specific to interactions.

        #if !defined(NDEBUG)
        //assert(NOT_EVAL_FLAG(f, DOING_PICKUPS));
        //assert(
        //    (f->flags.bits & ~EVAL_FLAG_TOOK_HOLD) == F->initial_flags
        //);  // changes should be restored, va_list reification may take hold
        #endif

        TRASH_CFUNC_IF_DEBUG(REBNAT, f->executor);

        assert(IS_SPECIFIC(cast(RELVAL*, f->out)));

        if (NOT_EVAL_FLAG(f, KEEP_STALE_BIT))
            f->out->header.bits &= ~(CELL_FLAG_OUT_MARKED_STALE);

        // !!! Currently we do not drop the topmost frame, because some code
        // (e.g. MATCH) would ask for a frame to be filled, and then steal
        // its resulting varlist.  However, if MATCH is on the stack when it
        // makes the call, it's not stackless...e.g. it should be written
        // some other way.
        //
        if (GET_EVAL_FLAG(f, ROOT_FRAME)) {
            if (PG_Tasks and f == PG_Tasks->go_frame) {
                //
                // If a task finishes, its output result just goes into the
                // void.  It was resumed during a block, and we need to pick
                // up another task.  Kill it off, and then go back to main
                // to see if whatever blocked main is ready, if not pick up
                // another task.
                //
                REBTSK *done_task = PG_Tasks;
                Circularly_Unlink_Task(done_task);

                if (NOT_END(&done_task->channel)) {
                    //
                    // !!! We have to be careful here, because we're in the
                    // trampoline...so we can't call SEND-CHAN.
                    //
                    REBCTX *ctx = VAL_CONTEXT(&done_task->channel);
                    REBLEN n = Find_Canon_In_Context(
                        ctx,
                        Canon(SYM_BUFFER),
                        true   // !!! "always"?
                    );
                    REBVAL *buffer = CTX_VAR(ctx, n);
                    Quotify(f->out, 1);
                    Append_Value(VAL_ARRAY(buffer), f->out);
                }

                FREE(REBTSK, done_task);

                Init_Unreadable_Void(f->out);  // Blockers shouldn't read
            }
            else {
                DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&jump);
                STATE_BYTE(f) = 0;  // !!! Frame gets reused, review
                return false;
            }
        }

        // Some natives and executors want to be able to leave a pushed frame
        // intact as the "top of stack" even when it has completed.  This
        // means that when those executors run, their frame parameter is
        // not the technical top of the stack.
        //
        if (GET_EVAL_FLAG(f, TRAMPOLINE_KEEPALIVE)) {
            f = f->prior;
            assert(f != FS_TOP);  // sanity check (*not* the top of stack)
        }
        else {
            REBFRM *prior = f->prior;
            Drop_Frame(f);
            f = prior;
            assert(f == FS_TOP);  // sanity check (is the top of the stack)
        }
        goto bounce;
    }

    if (r == R_WAITING) {
        if (not PG_Tasks)
            fail ("Deadlock reached (main thread blocking with no tasks)");

        if (not PG_Tasks->plug_frame) {  // it's plugged in, so plug is null
            //
            // A task is running and it blocked.  Unplug it, move it to the
            // back of the line, and give the main thread a chance.
            //
            assert(GET_EVAL_FLAG(PG_Tasks->go_frame, ROOT_FRAME));
            CLEAR_EVAL_FLAG(PG_Tasks->go_frame, ROOT_FRAME);  // for unplug
            assert(not PG_Tasks->plug_frame);
            PG_Tasks->plug_frame = f;
            Unplug_Stack(
                &PG_Tasks->plug,
                f,
                PG_Tasks->go_frame->prior
            );
            PG_Tasks = PG_Tasks->next;  // circularly linked
        }
        else {
            Init_Void(f->out);  // R_BLOCKING was returned, f->out unknown

            // Main is running and there are tasks.  Go ahead and start up
            // the first one available (last one to execute).
            //
            Replug_Stack(PG_Tasks->plug_frame, f, SPECIFIC(&PG_Tasks->plug));
            assert(IS_TRASH_DEBUG(&PG_Tasks->plug));
            PG_Tasks->plug_frame = nullptr;

            // The scheduler tests when root frames are reached if that root
            // frame is the function frame of the GO action of the currently
            // running task (PG_Task).  If so, that task is disposed of.
            //
            assert(NOT_EVAL_FLAG(PG_Tasks->go_frame, ROOT_FRAME));
            SET_EVAL_FLAG(PG_Tasks->go_frame, ROOT_FRAME);  // uncrossable
        }

        f = FS_TOP;  // Refresh after plug or unplug

        goto bounce;
    }

    if (r == R_THROWN) {
      thrown:

      #if !defined(NDEBUG)
        Eval_Core_Exit_Checks_Debug(f);   // called unless a fail() longjmps
      #endif

        // When an executor does `return R_THROWN;` cooperatively, it is
        // expected that it has balanced all of its API handles and memory
        // allocations.  The executor is changed to a "trash" pointer to
        // indicate it did not end normally and should not be called again
        // (distinct from the 'nullptr' which signals normal execution done).
        // This is because the trashing is not necessary in release builds
        //
        assert(not IS_CFUNC_TRASH_DEBUG(REBNAT, f->executor));

        if (f->executor != &Cleaner_Executor) {
            Move_Value(DS_PUSH(), VAL_THROWN_LABEL(f->out));
            CATCH_THROWN(f->out, f->out);
            STATE_BYTE(f) = 0;
            TRASH_CFUNC_IF_DEBUG(REBNAT, f->executor);
            INIT_F_EXECUTOR(f, &Cleaner_Executor);
            STATE_BYTE(f) = ST_CLEANER_RUNNING_THROWN;
            goto bounce;
        }

        TRASH_CFUNC_IF_DEBUG(REBNAT, f->executor);  // cleaner finished

        if (GET_EVAL_FLAG(f, ROOT_FRAME)) {
            if (PG_Tasks and f == PG_Tasks->go_frame) {
                //
                // !!! When you get an uncaught throw or failure and it is in
                // a goroutine, that goroutine has to stop and signal its
                // error somehow.
                //
                // In terms of raising errors on the main thread, it's kind of
                // like a Ctrl-C fabricating an error on any innocuous
                // statement you might have--if the scheduler were allowed to
                // run at any minute.  But right now, the only time it will
                // happen is when the main thread is in a block on a SEND-CHAN
                // or RECEIVE-CHAN.  Rethink.
                //
                REBCTX *error = Error_No_Catch_For_Throw(f->out);
                Abort_Frame(f);

                REBTSK *failed_task = PG_Tasks;
                Circularly_Unlink_Task(failed_task);

                if (NOT_END(&failed_task->channel)) {
                    //
                    // !!! We have to be careful here, because we're in the
                    // trampoline...so we can't call SEND-CHAN.
                    //
                    REBCTX *ctx = VAL_CONTEXT(&failed_task->channel);
                    REBLEN n = Find_Canon_In_Context(
                        ctx,
                        Canon(SYM_BUFFER),
                        true   // !!! "always"?
                    );
                    REBVAL *buffer = CTX_VAR(ctx, n);
                    Append_Value(VAL_ARRAY(buffer), CTX_ARCHETYPE(error));
                    error = nullptr;
                }

                FREE(REBTSK, failed_task);

                if (error)
                    fail (error);

                f = FS_TOP;
                goto bounce;
            }

            assert(NOT_EVAL_FLAG(f, TRAMPOLINE_KEEPALIVE));  // always kept
            DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&jump);
            return true;
        }

        if (GET_EVAL_FLAG(f, TRAMPOLINE_KEEPALIVE))
            f = f->prior;
        else {
            Abort_Frame(f);
            f = FS_TOP;  // refresh
        }

        goto bounce;
    }

    assert(!"executor(f) not f->out, R_CONTINUATION, R_WAITING, R_THROWN");
    panic (r);
}


//
//  go: native [
//      {Start a new independent coroutine stack}
//
//      return: "If /CHANNEL is used, channel w/quoted result or error"
//          [void! object!]
//      source [block! action!]
//      /kernel "Do not permit debugging of this goroutine thread"
//      /channel "Return quoted result of evaluation over a returned channel"
//  ]
//
REBNATIVE(go)
{
    INCLUDE_PARAMS_OF_GO;

    enum {
        ST_GO_INITIAL_ENTRY = 0,
        ST_GO_EVALUATING_ARGS
    };

    REBVAL *source = ARG(source);

    switch (D_STATE_BYTE) {
      case ST_GO_INITIAL_ENTRY: goto initial_entry;
      case ST_GO_EVALUATING_ARGS: assert(false);  // not stackless yet
      default: assert(false);
    }

  initial_entry: {
    DECLARE_END_FRAME (f, EVAL_MASK_DEFAULT | EVAL_FLAG_ROOT_FRAME);

    // We don't actually want to return a value out of our generated frame
    // to the output of GO.  But for convenience, the frame builder throws
    // into the output of the frame it is given vs. having a separate arg.
    // So while the frame is being built, let it be set to D_OUT.
    //
    if (IS_ACTION(source)) {
        Push_Frame(D_OUT, f, &Action_Executor);
        Push_Action(f, VAL_ACTION(source), VAL_BINDING(source));
        REBSTR *opt_label = nullptr;
        Begin_Prefix_Action(f, opt_label);
        f_param = END_NODE;
    }
    else {
        Push_Frame(D_OUT, f, &Evaluator_Executor);
        assert(IS_BLOCK(source));
        DECLARE_FEED_AT (feed, source);
        f->feed = feed;
        SET_EVAL_FLAG(f, ALLOCATED_FEED);
        SET_EVAL_FLAG(f, TO_END);
    }

    // Now that the frame is built, we want it to be executed on its own
    // stack.  Add it to the "Tasks" list.
    //
    REBTSK *task = TRY_ALLOC(REBTSK);
    if (not task) {
        Drop_Frame(f);
        fail ("Not enough memory for GO to allocate a new task");
    }
    Prep_Cell(&task->plug);

    // !!! For the moment, you can't Unplug a root frame, and you can't
    // stackfully evaluate unless something is a root frame.  Take the flag
    // off of the frame so we can unplug it.
    //
    task->go_frame = f;
    CLEAR_EVAL_FLAG(f, ROOT_FRAME);

    Prep_Cell(&task->channel);
    if (REF(channel)) {
        REBVAL *chan = rebValue("make-chan", rebEND);
        Move_Value(D_OUT, chan);
        Move_Value(&task->channel, chan);
        rebRelease(chan);
    }
    else {
        Init_Void(D_OUT);
        SET_END(&task->channel);
    }

    // !!! Theorized granularity of debugging is on a per-Task basis.  The
    // "main thread" is thus not something that the debugger would step
    // through, nor are service routines in the console itself.
    //
    task->debuggable = not REF(kernel);

    // Start off being willing to recycle, Ctrl-C, etc.  (This might not be
    // the best idea, e.g. a task might ask not to allow recycles because it
    // has a good reason, so if another task says it's okay, might be bad.)
    //
    task->sigmask = ALL_BITS;

    // There's a sanity check that the caller of unplug isn't in 0 state,
    // so make sure we aren't.
    //
    D_STATE_BYTE = ST_GO_EVALUATING_ARGS;
    Unplug_Stack(&task->plug, f, frame_);
    task->plug_frame = f;  // must match frame
    assert(TG_Top_Frame == frame_);

    Circularly_Link_Task(task);

    return D_OUT;  // OBJECT! if /CHANNEL, else VOID!
  }
}


//
//  wait2: native [
//      {Beginnings of a new WAIT instruction}
//  ]
//
REBNATIVE(wait2)
{
    INCLUDE_PARAMS_OF_WAIT2;

    enum {
        ST_WAIT2_INITIAL_ENTRY = 0,
        ST_WAIT2_SIMULATING_WAITING
    };

    switch (D_STATE_BYTE) {
      case ST_WAIT2_INITIAL_ENTRY: goto initial_entry;
      case ST_WAIT2_SIMULATING_WAITING: goto return_void;
      default: assert(false);
    }

  initial_entry: {
    D_STATE_BYTE = ST_WAIT2_SIMULATING_WAITING;
    return R_WAITING;
  }

  return_void: {
    assert(IS_UNREADABLE_DEBUG(D_OUT));
    return Init_Void(D_OUT);
  }
}


//
//  defer: native [
//
//  {Add code that will run when the implied FRAME! ends}
//
//      return: [void!]
//      code [block!]
//  ]
//
REBNATIVE(defer)
{
    INCLUDE_PARAMS_OF_DEFER;

    REBVAL *deferred = Alloc_Value_Core(frame_->prior);
    REBARR *a = Singular_From_Cell(deferred);
    SET_ARRAY_FLAG(a, DEFERRED_CODE);

    Move_Value(deferred, ARG(code));

    return Init_Void(D_OUT);
}
