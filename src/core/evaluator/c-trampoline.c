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
//    "abrupt failures" (see FRAME_FLAG_ABRUPT_FAILURE).  The executor which
//    was active when that failure occurred is offered a chance to clean up.
//    But any stacks that it pushed which were not running will be discarded.
//
// 4. When fails occur, any frames which have been pushed that the trampoline
//    is not running currently will be above FRAME.  These are not offered the
//    chance to handle or trap the error.
//
//    (Example: When something like ALL is "between steps", the frame it
//     pushed to process its block will be above it on the stack.  If the ALL
//     decides to call fail(), the non-running stack frame can be "TOP_FRAME"
//     above the ALL's "FRAME".)
//


#include "sys-core.h"


#if DEBUG_COUNT_TICKS  // <-- THIS IS VERY USEFUL, SEE UPDATE_TICK_DEBUG()

    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    Tick TG_break_at_tick =        0;
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
Bounce Just_Use_Out_Executor(Frame(*) f)
{
    if (Is_Throwing(f))
        return BOUNCE_THROWN;
    return f->out;
}


// This gives us access to macros related to the idea of the current frame,
// like OUT and SPARE.  It also defines FRAME as a way of getting at the
// jump list's notion of which frame currently has "control"
//
#define frame_ (TG_Jump_List->frame)


//
//  Trampoline_From_Top_Maybe_Root: C
//
Bounce Trampoline_From_Top_Maybe_Root(void)
{
  bounce_on_trampoline_with_trap:

  // TRAP_BLOCK is an abstraction of `try {} catch(...) {}` which can also
  // work in plain C using setjmp/longjmp().  It's considered desirable to
  // support both approaches: plain C compilation (e.g. with TCC) runs on many
  // legacy/embedded platforms, but structured exception handling has support
  // on other systems like some WebAssembly--where longjmp may not be.
  //
  // Regardless of which implementation you are using, once an "exception" has
  // occurred you must jump up above the block to re-enable the "catching".
  // C++ does not allow a `goto` from outside of `try` block into it:
  //
  //    "A goto or switch statement shall not be used to transfer control
  //     into a try block or into a handler. "
  //
  // In the case of a C setjmp()/longjmp(), we have "used up" the jump buffer
  // after a longjmp() occurs, so obviously it needs to be re-setjmp'd.
  //
  // So either way, we can only jump to `bounce_on_trampoline` if no abrupt
  // fail() has occurred.  otherwise jump to `bounce_on_trampoline_with_trap`
  // to put the trapping back into effect.

  TRAP_BLOCK_IN_CASE_OF_ABRUPT_FAILURE {  ////////////////////////////////////

  bounce_on_trampoline:

  // 1. The Just_Use_Out_Executor() exists vs. using something like nullptr
  //    for the executor just to make it more obvously intentional that a
  //    passthru is intended.  Having it maintain a state byte would be
  //    additional overhead.  (Review in light of use of nonzero for GC
  //    and bookkeeping purposes; e.g. could STATE of 255 mean Just_Use_Out?)
  //
  // 2. Stale voids are allowed because they are used by Alloc_Value() and
  //    DECLARE_LOCAL, in order to help signal an uninitialized value should
  //    not be read from (beyond the usual taboo of looking at voids)

    ASSERT_NO_DATA_STACK_POINTERS_EXTANT();

    Bounce r;

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
            r = BOUNCE_THROWN;
            goto thrown;
        }
    }

    if (Get_Frame_Flag(FRAME, ABRUPT_FAILURE)) {
        assert(Get_Frame_Flag(FRAME, NOTIFY_ON_ABRUPT_FAILURE));
        assert(Is_Throwing(FRAME));
    }
    else if (
        STATE == 0  // can't read STATE when ABRUPT_FAILURE flag is set
        and Not_Frame_Flag(FRAME, MAYBE_STALE)
    ){
        if (FRAME->executor != &Just_Use_Out_Executor)  // exempt, see [1]
            assert(
                Is_Cell_Erased(OUT)
                or Is_Stale_Void(OUT)  // stale ok, see [2]
                or Is_Void(OUT)
            );
    }

{ //=//// CALL THE EXECUTOR ///////////////////////////////////////////////=//

    // The executor may push more frames or change the executor of the frame
    // it receives.  The FRAME may not match TOP_FRAME at this moment.

  #if !defined(NDEBUG)
    Frame(*) check = FRAME;  // make sure FRAME doesn't change during executor
  #endif

    UPDATE_TICK_DEBUG(nullptr);

    // v-- This is the TG_break_at_tick or C-DEBUG-BREAK landing spot --v
                      r = (FRAME->executor)(FRAME);
    // ^-- **STEP IN** to this call using the debugger to debug it!!! --^

  #if !defined(NDEBUG)
    assert(FRAME == check);  // R is relative to the OUT of FRAME we executed
  #endif

} //=//// HANDLE FINISHED RESULTS /////////////////////////////////////////=//

    if (Get_Frame_Flag(FRAME, ABRUPT_FAILURE)) {
        assert(Get_Frame_Flag(FRAME, NOTIFY_ON_ABRUPT_FAILURE));
        assert(r == BOUNCE_THROWN);
        assert(Is_Meta_Of_Raised(VAL_THROWN_LABEL(FRAME)));
    }

  // 1. There may be some optimization possible here if the flag controlling
  //    whether you wanted to keep the stale flag was also using the same
  //    EVAL_FLAG bit as the CELL_FLAG for stale.  It's tricky since for
  //    series nodes that's the bit for being free.

    if (r == OUT) {
      result_in_out:
        assert(IS_SPECIFIC(cast(Cell(*), OUT)));

        if (Is_Raised(OUT)) {
            if (Not_Frame_Flag(FRAME, FAILURE_RESULT_OK)) {
                //
                // treat any failure as if it could have been thrown from
                // anywhere, so it is bubbled up as a throw.
                //
                Reify_Isotope(OUT);
                Init_Thrown_With_Label(
                    FRAME,
                    Lib(NULL),  // no "thrown value"
                    OUT  // only the ERROR! as a label
                );
                goto thrown;
            }

            if (Get_Frame_Flag(FRAME, META_RESULT))
                Reify_Isotope(OUT);
        }
        else if (Get_Frame_Flag(FRAME, META_RESULT)) {
            Clear_Stale_Flag(OUT);  // see [1]
            if (Is_Void(OUT))
                Init_Meta_Of_Void(OUT);
            else
                Meta_Quotify(OUT);
        }
        else if (Get_Frame_Flag(FRAME, BRANCH)) {
            Clear_Stale_Flag(OUT);  // also, see [1]
            if (Is_Void(OUT))
                Init_Void_Isotope(OUT);
            else if (VAL_TYPE_UNCHECKED(OUT) == REB_NULL)
                Init_Null_Isotope(OUT);
        }
        else if (Not_Frame_Flag(FRAME, MAYBE_STALE))
            Clear_Stale_Flag(OUT);  // again, see [1]

        if (Get_Frame_Flag(FRAME, ROOT_FRAME)) {
            STATE = 0;  // !!! Frame gets reused, review
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
            return TOP_FRAME->out;
        }

        // Some natives and executors want to be able to leave a pushed frame
        // intact as the "top of stack" even when it has completed.  This
        // means that when those executors run, their frame parameter is
        // not the technical top of the stack.
        //
        if (Get_Frame_Flag(FRAME, TRAMPOLINE_KEEPALIVE)) {
            FRAME = FRAME->prior;
            assert(FRAME != TOP_FRAME);  // sanity check (*not* the top of stack)
        }
        else {
            assert(FRAME == TOP_FRAME);  // sanity check (is the top of the stack)
            Drop_Frame(FRAME);
            FRAME = TOP_FRAME;
        }

        goto bounce_on_trampoline;  // some pending frame now has a result
    }

  //=//// HANDLE CONTINUATIONS ////////////////////////////////////////////=//
  //
  // 1. It's legal for a frame to implement itself in terms of another frame
  //    that is compatible.  This could have a separate signal, but for now
  //    it's done as BOUNCE_CONTINUE.  Since that delegation may be to an
  //    INITIAL_ENTRY state, 0 needs to be legal.
  //
  // 2. If a frame besides the one that we ran is above on the stack, then
  //    the frame is using that continuation to get a result it is interested
  //    in.  It needs to know it did a push, so the state must be nonzero.
  //
  //    (Technically there could be some other frame field modified to let it
  //    know there was an effect, but we enforce the nonzero rule because it
  //    also helps with bookkeeping and GC features, allowing the zero value
  //    to be reserved to mean something else.)

    if (r == BOUNCE_CONTINUE) {
        if (FRAME != TOP_FRAME)  // continuing self ok, see [1]
            assert(STATE != 0);  // otherwise state enforced nonzero, see [2]

        FRAME = TOP_FRAME;
        goto bounce_on_trampoline;
    }

    if (r == BOUNCE_DELEGATE) {
        //
        // We could unhook the frame from the stack here, but leaving it in
        // provides clarity in the stack.   Hence this should not be used in
        // tail call situations.
        //
        STATE = DELEGATE_255;  // maintain non-zero invariant
        FRAME->executor = &Just_Use_Out_Executor;  // whatever frames make

        FRAME = TOP_FRAME;
        goto bounce_on_trampoline;
    }

    if (r == BOUNCE_SUSPEND) {  // just to get emscripten started w/o Asyncify
        CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
        return BOUNCE_SUSPEND;
    }


  //=//// HANDLE THROWS, INCLUDING (NON-ABRUPT) ERRORS ////////////////////=//

    // 1. Having handling of UNWIND be in the trampoline means that any frame
    //    can be "teleported to" with a result, not just ACTION! frames.  It
    //    has a notable use by RETURN from a FUNC, which considers its type
    //    checking to be finished so it can skip past the Action_Executor().
    //
    //    !!! Using BOUNCE_THROWN makes it possible for the UNWIND to be offered to
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
    // 3. Constructs like REDUCE-EACH keep a subframe pushed to do evaluation,
    //    but then want to keep that state while doing another evaluation
    //    (e.g. the body block).  To "punch a hole" through the evaluation
    //    frame it sets the executor to Just_Use_Out and can get the result
    //    without dropping the frame.  But thrown values like CONTINUE lead
    //    to a problem of how to express wanting TRAMPOLINE_KEEPALIVE to be
    //    applicable to throw situations as well--not all want it.  For now
    //    we conflate Just_Use_Out with the intent of keepalive on throw.

    if (r == BOUNCE_THROWN) {
      thrown:

        assert(FRAME == TOP_FRAME);  // Action_Executor() helps, drops inerts

        /*assert(not IS_CFUNC_TRASH_DEBUG(Executor*, FRAME->executor));*/
        TRASH_CFUNC_IF_DEBUG(Executor*, FRAME->executor);

        if (Get_Frame_Flag(FRAME, ABRUPT_FAILURE)) {
            //
            // They had their chance to clean up.
            // Fail again as definitional error, but this time don't notify.
            //
            assert(Get_Frame_Flag(FRAME, NOTIFY_ON_ABRUPT_FAILURE));
            Clear_Frame_Flag(FRAME, NOTIFY_ON_ABRUPT_FAILURE);
            Clear_Frame_Flag(FRAME, ABRUPT_FAILURE);
            assert(Is_Meta_Of_Raised(VAL_THROWN_LABEL(FRAME)));
            Context(*) ctx = VAL_CONTEXT(VAL_THROWN_LABEL(FRAME));
            CATCH_THROWN(SPARE, FRAME);
            fail (ctx);
        }

        const REBVAL *label = VAL_THROWN_LABEL(FRAME);  // unwind, see [1]
        if (
            IS_ACTION(label)
            and VAL_ACTION(label) == VAL_ACTION(Lib(UNWIND))
            and TG_Unwind_Frame == FRAME  // may be inaccessible, see [2]
        ){
            if (Is_Void(&TG_Thrown_Arg)) {
                CATCH_THROWN(SPARE, FRAME);  // act invisibily
                Mark_Eval_Out_Voided(OUT);
            }
            else
                CATCH_THROWN(OUT, FRAME);  // overwrite output

            goto result_in_out;
        }

        if (Get_Frame_Flag(FRAME, ROOT_FRAME)) {  // don't abort top
            assert(Not_Frame_Flag(TOP_FRAME, TRAMPOLINE_KEEPALIVE));
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
            return BOUNCE_THROWN;
        }

        Drop_Frame(FRAME);  // restores to baseline
        FRAME = TOP_FRAME;

        if (FRAME->executor == &Just_Use_Out_Executor) {
            if (Get_Frame_Flag(FRAME, TRAMPOLINE_KEEPALIVE))
                FRAME = FRAME->prior;  // hack, don't let it be aborted, see [3]
        }

        goto bounce_on_trampoline;  // executor will see the throw
    }

    assert(!"executor(f) not OUT, BOUNCE_THROWN, or BOUNCE_CONTINUE");
    panic (cast(void*, r));

} ON_ABRUPT_FAILURE(Context(*) e) {  ////////////////////////////////////////////

  // 1. An abrupt fail(...) is treated as a "thrown error", which can not be
  //    intercepted in the same way as a definitional error can be.
  //
  //   (It was wondered if since we know what FRAME was "in control" when a
  //    fail() occurred, if this code should put a Raised() in the output
  //    slot...as if a `return RAISE(xxx)` occurred.  But just because we know
  //    what FRAME the trampoline last called does not mean every incidental
  //    error should be attributed to being "from" that frame.  That would
  //    promote any incidental error--like "out of memory", that could come
  //    from any nested library call--to being definitional.)
  //
  // 2. A frame that asked to be notified about abrupt failures will get the
  //    failure thrown.  The default behavior of staying in the thrown state
  //    will be to convert it to a definitional failure on return.
  //
  //    e.g. ABRUPT_FAILURE + NOTIFIY_ON_ABRUPT_FAILURE + return BOUNCE_THROWN
  //    will act as if there had never been a NOTIFY_ON_ABRUPT_FAILURE

    ASSERT_CONTEXT(e);
    assert(CTX_TYPE(e) == REB_ERROR);

    Set_Frame_Flag(FRAME, ABRUPT_FAILURE);

    Clear_Feed_Flag(FRAME->feed, NEXT_ARG_FROM_OUT);  // !!! stops asserts

    while (TOP_FRAME != FRAME) {  // drop idle frames above the fail
        assert(Not_Frame_Flag(TOP_FRAME, NOTIFY_ON_ABRUPT_FAILURE));
        assert(Not_Frame_Flag(TOP_FRAME, ROOT_FRAME));

        if (Is_Action_Frame(TOP_FRAME)) {
            assert(Not_Executor_Flag(ACTION, TOP_FRAME, DISPATCHER_CATCHES));
            assert(not Is_Action_Frame_Fulfilling(TOP_FRAME));
            Drop_Action(TOP_FRAME);
        }

        Drop_Frame(TOP_FRAME);  // will call va_end() if variadic frame
    }

    Init_Thrown_With_Label(  // Error is non-definitional, see [1]
        FRAME,
        Lib(NULL),  // no "thrown value"
        Quasify(Copy_Cell(SPARE, CTX_ARCHETYPE(e)))  // label w/quasified ERROR!
    );

    if (Not_Frame_Flag(FRAME, NOTIFY_ON_ABRUPT_FAILURE)) {
        if (Get_Frame_Flag(FRAME, ROOT_FRAME)) {
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
            return BOUNCE_THROWN;
        }

        Drop_Frame(FRAME);
    }

    CLEANUP_BEFORE_EXITING_TRAP_BLOCK;  /* Note: changes FRAME */
    // (TOP_FRAME will become FRAME again after push_trap)
    goto bounce_on_trampoline_with_trap;  // after exception, need new trap
}}


//
//  Trampoline_With_Top_As_Root_Throws: C
//
bool Trampoline_With_Top_As_Root_Throws(void)
{
    Frame(*) root = TOP_FRAME;

  #if !defined(NDEBUG)
    struct Reb_Jump *check = TG_Jump_List;
    assert(Not_Frame_Flag(root, ROOT_FRAME));
  #endif

    // !!! More efficient if caller sets this, but set it ourselves for now.
    //
    Set_Frame_Flag(root, ROOT_FRAME);  // can't unwind across, see [1]

    Bounce r = Trampoline_From_Top_Maybe_Root();

  #if !defined(NDEBUG)
    assert(check == TG_Jump_List);  // must CLEANUP_BEFORE_EXITING_TRAP_BLOCK
    assert(TOP_FRAME == root);
    assert(Get_Frame_Flag(root, ROOT_FRAME));
  #endif

    Clear_Frame_Flag(root, ROOT_FRAME);

    if (r == BOUNCE_THROWN)
        return true;
    if (r == root->out)
        return false;

  #if DEBUG_FANCY_PANIC
    Dump_Stack(root);
  #endif

    fail ("Cannot interpret Trampoline result");
}


//
//  Trampoline_Throws: C
//
bool Trampoline_Throws(REBVAL *out, Frame(*) root)
{
    Push_Frame(out, root);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Frame(root);
    return threw;
}
