//
//  File: %c-trampoline.c
//  Summary: "Central Interpreter Loop for 'Stackless' Evaluation"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022 Ren-C Open Source Contributors
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
// calls and the C machine stack won't grow just because Ren-C stacks grow:
//
// https://en.wikipedia.org/wiki/Trampoline_(computing)#High-level_programming
//
// !!! The ideal goal is that the Trampoline is never found recursively on a
// standard evaluation stack.  The only way it should be found on the stack
// more than once would be to call out to non-Rebol code, which then turned
// around and made an API call back in (it would not be able to gracefully
// unwind across such C stack frames).  In the interim, not all natives have
// been rewritten as state machines--it's a work-in-progress.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. The instigating call to the Trampoline cannot be unwound across, as it
//    represents a "stackful" invocation of the evaluator.  Functions like
//    YIELD must know the passed-in frame is uncrossable, so that it can raise
//    an error if you try to unwind across a top-level Trampoline call.
//
//    !!! Instead of returning just `bool`, the Trampoline could theoretically
//    offer the option of returning a frame stack to the caller that it could
//    wrap up in a Promise.  This would be an alternative to raising errors.
//
// 2. In theory, a Trampoline caller could push several frames to be evaluated,
//    and the passed in `root` would just be where evaluation should *stop*.
//    No cases of this exist yet, so asserting you only pass in the topmost
//    frame is conservative for now.
//
// 3. A fail() can happen at any moment--even due to something like a failed
//    memory allocation requested by an executor itself.  These are called
//    "abrupt failures" (see EVAL_FLAG_ABRUPT_FAILURE).  The executor which
//    was active when that failure occurred is offered a chance to clean up.
//    But any stacks that it pushed which were not running will be discarded.
//
// 4. When fails occur, any frames which have been pushed that the trampoline
//    is not running currently will be above FRAME.  These are not offered the
//    chance to handle or trap the error.
//
//    (Example: When something like ALL is "between steps", the frame it
//     pushed to process its block will be above it on the stack.  If the ALL
//     decides to call fail(), the non-running stack frame can be "FS_TOP"
//     above the ALL's "FRAME".)
//


#include "sys-core.h"


#if DEBUG_COUNT_TICKS  // <-- THIS IS VERY USEFUL, SEE UPDATE_TICK_DEBUG()

    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    REBTCK TG_Break_At_Tick =      0;
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***

#endif  // ^-- SERIOUSLY: READ ABOUT C-DEBUG-BREAK AND PLACES TICKS ARE STORED


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


// This gives us access to macros related to the idea of the current frame,
// like OUT and SPARE.  It also defines FRAME as a way of getting at the
// jump list's notion of which frame currently has "control"
//
#define frame_ (TG_Jump_List->frame)


//
//  Trampoline_Throws: C
//
bool Trampoline_Throws(REBFRM *root)
{
    struct Reb_Jump jump;  // only one setjmp() point per trampoline invocation

    // !!! More efficient if caller sets this, but set it ourselves for now.
    //
    Set_Eval_Flag(root, ROOT_FRAME);  // can't unwind across, see [1]

    assert(root == FS_TOP);  // this could be relaxed, see [2]

  push_trap_for_longjmp: {  //////////////////////////////////////////////////

    // Any frame that is interrupted at an arbitrary moment by a fail() gets
    // "teleported" back to this point.  The running C stack variables in any
    // natives will be lost.  Heap-allocated Frames will be intact.  See [3]

    PUSH_TRAP_SO_FAIL_CAN_JUMP_BACK_HERE(&jump);

    // (The first time through the following, 'jump.error' will be null, BUT
    // `fail` can longjmp here, so 'error' won't be null *if* that happens!)

    if (jump.error) {
        ASSERT_CONTEXT(jump.error);
        assert(CTX_TYPE(jump.error) == REB_ERROR);

        Set_Eval_Flag(FS_TOP, ABRUPT_FAILURE);

        CLEAR_FEED_FLAG(FS_TOP->feed, NEXT_ARG_FROM_OUT);  // !!! stops asserts

        while (FS_TOP != FRAME) {  // drop idle frames above the fail, see [4]
            if (Is_Action_Frame(FS_TOP)) {
                assert(not Is_Action_Frame_Fulfilling(FS_TOP));
                Drop_Action(FS_TOP);
            }

            Abort_Frame(FS_TOP);  // will call va_end() if variadic frame
        }

        TRASH_POINTER_IF_DEBUG(FRAME);  // note to not use until next setjmp

        // The trampoline can drop frames until it finds one which actually
        // has the DISPATCHER_CATCHES flag set.  Any frame that doesn't have
        // that set can't have allocations that aren't undone by aborting.
        //
        while (
            Not_Eval_Flag(FS_TOP, ROOT_FRAME)
            and not (  // can't trap the abrupt failure
                Get_Eval_Flag(FS_TOP, DISPATCHER_CATCHES)
                and Not_Eval_Flag(FS_TOP, ABRUPT_FAILURE)
            )
        ){
            Abort_Frame(FS_TOP);  // restores to baseline
        }

        // The mechanisms for THROW-ing and FAIL-ing are somewhat unified in
        // stackless...(a TRAPpable failure is just any "thrown" value with
        // a VAL_THROWN_LABEL() which is an ERROR!).  So the trampoline just
        // converts the longjmp into a throw.

        Init_Thrown_With_Label(
            FS_TOP,
            Lib(NULL),  // no "thrown value"
            CTX_ARCHETYPE(jump.error)  // only the ERROR! as a label
        );

        TG_Jump_List = jump.last_jump;  // unlink *after* error/etc. extracted

        if (
            Get_Eval_Flag(FS_TOP, DISPATCHER_CATCHES)
            and Not_Eval_Flag(FS_TOP, ABRUPT_FAILURE)
        ){
            goto push_trap_for_longjmp;  // have to push again to trap again
        }

        assert(Get_Eval_Flag(FS_TOP, ROOT_FRAME));
        return true;
    }

    FRAME = FS_TOP;

} bounce_on_the_trampoline: {  ///////////////////////////////////////////////

    ASSERT_NO_DATA_STACK_POINTERS_EXTANT();

    REB_R r;

  #if !defined(NDEBUG)  // Total_Eval_Cycles is periodically reconciled
    ++Total_Eval_Cycles_Doublecheck;
  #endif

    if (--Eval_Countdown <= 0) {
        //
        // Doing signals covers several things that may cause interruptions:
        //
        //  * Running the garbage collector
        //  * Noticing when a HALT was requested
        //  * (future?) Allowing a break into an interactive debugger
        //
        if (Do_Signals_Throws(FRAME)) {
            r = R_THROWN;
            goto thrown;
        }
    }

    assert(Not_Eval_Flag(FRAME, ABRUPT_FAILURE));

{ //=//// CALL THE EXECUTOR ///////////////////////////////////////////////=//

    // The executor may push more frames or change the executor of the frame
    // it receives.  The FRAME may not match FS_TOP at this moment.

  #if !defined(NDEBUG)
    REBFRM *check = FRAME;  // make sure FRAME doesn't change during executor
  #endif

    UPDATE_TICK_DEBUG(nullptr);

    // v-- This is the TG_Break_At_Tick or C-DEBUG-BREAK landing spot --v
                      r = (FRAME->executor)(FRAME);
    // ^-- **STEP IN** to this call using the debugger to debug it!!! --^

  #if !defined(NDEBUG)
    assert(FRAME == check);  // R is relative to the OUT of FRAME we executed
  #endif

} //=//// HANDLE FINISHED RESULTS /////////////////////////////////////////=//

    if (r == OUT) {
      result_in_out:
        assert(IS_SPECIFIC(cast(Cell*, OUT)));

        if (Get_Eval_Flag(FRAME, MAYBE_STALE)) {
            assert(Not_Eval_Flag(FRAME, BRANCH));
            assert(Not_Eval_Flag(FRAME, META_RESULT));
        }
        else {
            Clear_Stale_Flag(OUT);
            if (Get_Eval_Flag(FRAME, BRANCH))
                Reify_Branch_Out(OUT);
            else if (Get_Eval_Flag(FRAME, META_RESULT))
                Reify_Eval_Out_Meta(OUT);
        }

        if (Get_Eval_Flag(FRAME, ROOT_FRAME)) {
            STATE = 0;  // !!! Frame gets reused, review
            DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&jump);
            return false;
        }

        // Some natives and executors want to be able to leave a pushed frame
        // intact as the "top of stack" even when it has completed.  This
        // means that when those executors run, their frame parameter is
        // not the technical top of the stack.
        //
        if (Get_Eval_Flag(FRAME, TRAMPOLINE_KEEPALIVE)) {
            FRAME = FRAME->prior;
            assert(FRAME != FS_TOP);  // sanity check (*not* the top of stack)
        }
        else {
            REBFRM *prior = FRAME->prior;
            Drop_Frame(FRAME);
            FRAME = prior;
            assert(FRAME == FS_TOP);  // sanity check (is the top of the stack)
        }

        goto bounce_on_the_trampoline;  // some pending frame now has a result
    }

  //=//// HANDLE CONTINUATIONS ////////////////////////////////////////////=//

    if (r == R_CONTINUATION) {
        assert(FRAME == FS_TOP or STATE != 0);

        FRAME = FS_TOP;
        goto bounce_on_the_trampoline;
    }

  //=//// HANDLE THROWS, INCLUDING (NON-ABRUPT) ERRORS ////////////////////=//

    // 1. Having handling of UNWIND be in the trampoline means that any frame
    //    can be "teleported to" with a result, not just ACTION! frames.  It
    //    has a notable use by RETURN from a FUNC, which considers its type
    //    checking to be finished so it can skip past the Action_Executor().
    //
    //    !!! Using R_THROWN makes it possible for the UNWIND to be offered to
    //    dispatchers that catch throws.  This is used for instance in MACRO,
    //    which intercepts the UNWIND issued by RETURN, because it doesn't want
    //    to actually return the block (it wants to splice it).  But that may
    //    suggest MACRO wants to use its own throw type in a definitional
    //    return, so that you could generically UNWIND to a macro frame and
    //    overwrite the result verbatim.
    //
    // 2. Note FRAME->varlist may be SERIES_FLAG_INACCESSIBLE here.  This can
    //    happen with RETURN during ENCLOSE.  So don't use CTX(FRAME->varlist)
    //    here, as that would try to validate it as not being inaccessible.
    //

    if (r == R_THROWN) {
      thrown:

        assert(not IS_CFUNC_TRASH_DEBUG(REBNAT, FRAME->executor));
        TRASH_CFUNC_IF_DEBUG(REBNAT, FRAME->executor);

        while (FS_TOP != FRAME)
            Abort_Frame(FS_TOP);  // !!! Should all inert frames be aborted?

        const REBVAL *label = VAL_THROWN_LABEL(FRAME);  // unwind, see [1]
        if (
            IS_ACTION(label)
            and VAL_ACTION(label) == VAL_ACTION(Lib(UNWIND))
            and TG_Unwind_Frame == FRAME  // may be inaccessible, see [2]
        ){
            if (Is_Void(&TG_Thrown_Arg))
                CATCH_THROWN(SPARE, FRAME);  // act invisibily
            else
                CATCH_THROWN(OUT, FRAME);  // overwrite output

            goto result_in_out;
        }

        if (Get_Eval_Flag(FRAME, ROOT_FRAME)) {  // don't abort top
            assert(Not_Eval_Flag(FS_TOP, TRAMPOLINE_KEEPALIVE));
            DROP_TRAP_SAME_STACKLEVEL_AS_PUSH(&jump);
            return true;
        }

        Abort_Frame(FRAME);  // restores to baseline
        FRAME = FS_TOP;
        goto bounce_on_the_trampoline;  // executor will see the throw
    }

    assert(!"executor(f) not OUT, R_THROWN, or R_CONTINUATION");
    panic (r);
}}
