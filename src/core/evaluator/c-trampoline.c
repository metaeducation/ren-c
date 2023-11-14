//
//  File: %c-trampoline.c
//  Summary: "Central Interpreter Loop for 'Stackless' Evaluation"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2023 Ren-C Open Source Contributors
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
// 1. Trampoline stack levels are called "Levels" and not "Frames", in order
//    to avoid confusion with the usermode FRAME! datatype's implementation.
//    (This was a major renaming effort, so some comments might be out of
//    sync and use the wrong term.)
//
// 2. The instigating call to the Trampoline cannot be unwound across, as it
//    represents a "stackful" invocation of the evaluator.  Functions like
//    YIELD must know the passed-in level is uncrossable, so that it can raise
//    an error if you try to unwind across a top-level Trampoline call.
//
//    !!! Instead of returning just `bool`, the Trampoline could theoretically
//    offer the option of returning a level stack to the caller that it could
//    wrap up in a Promise.  This would be an alternative to raising errors.
//
// 3. In theory, a Trampoline caller could push several levels to be evaluated,
//    and the passed in `root` would just be where evaluation should *stop*.
//    No cases of this exist yet, so asserting you only pass in the topmost
//    level is conservative for now.
//
// 3. A fail() can happen at any moment--even due to something like a failed
//    memory allocation requested by an executor itself.  These are called
//    "abrupt failures" (see LEVEL_FLAG_ABRUPT_FAILURE).  The executor which
//    was active when that failure occurred is offered a chance to clean up.
//    But any stacks that it pushed which were not running will be discarded.
//
// 4. When fails occur, any levels which have been pushed that the trampoline
//    is not running currently will be above LEVEL.  These are not offered the
//    chance to handle or trap the error.
//
//    (Example: When something like ALL is "between steps", the level it
//     pushed to process its block will be above it on the stack.  If the ALL
//     decides to call fail(), the non-running stack level can be "TOP_LEVEL"
//     above the ALL's "LEVEL".)
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
// The "Just_Use_Out_Executor()" is never actually called, but it's a state
// for the trampoline to check that's more obvious than `nullptr` in the
// executor slot.
//
// Optimized builds could use nullptr instead.
//
Bounce Just_Use_Out_Executor(Level(*) L)
  { panic (L->out); }


//
//  Delegated_Executor: C
//
// Similar to the Just_Use_Out_Executor, this is a special executor which is
// replaced as the executor when DELEGATE() is used outside of Action_Executor.
// (When actions run, it wants a call back to check the return type.)
//
Bounce Delegated_Executor(Level(*) L)
{
    if (Is_Throwing(L))
        return BOUNCE_THROWN;
    return L->out;
}


// This gives us access to macros related to the idea of the current level,
// like OUT and SPARE.  It also hooks up to `#define LEVEL level_` as a way
// of getting at the jump list's notion of which level currently has "control"
//
#define level_ (TG_Jump_List->level)


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

  bounce_on_trampoline_skip_just_use_out:

    while (LEVEL->executor == &Just_Use_Out_Executor)
        LEVEL = LEVEL->prior;  // fast skip, allow Is_Fresh() output

  bounce_on_trampoline:

  // 1. The Just_Use_Out_Executor() exists vs. using something like nullptr
  //    for the executor just to make it more obvously intentional that a
  //    passthru is intended.  (Review in light of use of nonzero for GC
  //    and bookkeeping purposes; e.g. could STATE of 255 mean Just_Use_Out?)
  //
  // 2. Stale voids are allowed because they are used by Alloc_Value() and
  //    DECLARE_LOCAL, in order to help signal an uninitialized value should
  //    not be read from (beyond the usual taboo of looking at voids)

    ASSERT_NO_DATA_STACK_POINTERS_EXTANT();

    assert(LEVEL->executor != &Just_Use_Out_Executor);  // drops skip, see [1]

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
        if (Do_Signals_Throws(LEVEL)) {
            r = BOUNCE_THROWN;
            goto thrown;
        }
    }

    if (Get_Level_Flag(LEVEL, ABRUPT_FAILURE)) {
        assert(Get_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE));
        assert(Is_Throwing(LEVEL));
    }
    else if (STATE == STATE_0) {  // can't read STATE if ABRUPT_FAILURE
        FRESHEN(OUT);
    }

{ //=//// CALL THE EXECUTOR ///////////////////////////////////////////////=//

    // The executor may push more levels or change the executor of the level
    // it receives.  The LEVEL may not match TOP_LEVEL at this moment.

  #if !defined(NDEBUG)
    Level(*) check = LEVEL;  // make sure LEVEL doesn't change during executor
  #endif

    UPDATE_TICK_DEBUG(nullptr);

    // v-- This is the TG_break_at_tick or C-DEBUG-BREAK landing spot --v
                      r = (LEVEL->executor)(LEVEL);
    // ^-- **STEP IN** to this call using the debugger to debug it!!! --^

  #if !defined(NDEBUG)
    assert(LEVEL == check);  // R is relative to the OUT of LEVEL we executed
  #endif

} //=//// HANDLE FINISHED RESULTS /////////////////////////////////////////=//

    if (Get_Level_Flag(LEVEL, ABRUPT_FAILURE)) {
        assert(Get_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE));
        assert(r == BOUNCE_THROWN);
        assert(IS_ERROR(VAL_THROWN_LABEL(LEVEL)));
    }

    if (r == OUT) {
      result_in_out:
        assert(not Is_Fresh(OUT));
        assert(IS_SPECIFIC(cast(Cell(*), OUT)));

        if (Get_Level_Flag(LEVEL, META_RESULT)) {
            Meta_Quotify(OUT);
        }
        else if (Is_Raised(OUT)) {
            if (Not_Level_Flag(LEVEL, FAILURE_RESULT_OK)) {
                //
                // treat any failure as if it could have been thrown from
                // anywhere, so it is bubbled up as a throw.
                //
                mutable_QUOTE_BYTE(OUT) = UNQUOTED_1;
                Init_Thrown_Error(LEVEL, stable_OUT);
                goto thrown;
            }
        }
        else if (Get_Level_Flag(LEVEL, BRANCH)) {
            Debranch_Output(OUT);  // make heavy voids, clear ELSE/THEN methods
        }

        if (Get_Level_Flag(LEVEL, ROOT_LEVEL)) {  // may keepalive sublevels
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;  // switches LEVEL pointer...
            return TOP_LEVEL->out;  // ...so return what's now TOP_LEVEL->out
        }

        // Some natives and executors want to be able to leave a pushed level
        // intact as the "top of stack" even when it has completed.  This
        // means that when those executors run, their level parameter is
        // not the technical top of the stack.
        //
        if (Get_Level_Flag(LEVEL, TRAMPOLINE_KEEPALIVE)) {
            LEVEL = LEVEL->prior;
            assert(LEVEL != TOP_LEVEL);  // sanity check (*not* top of stack)
        }
        else {
            assert(LEVEL == TOP_LEVEL);  // sanity check (is top of the stack)
            Drop_Level(LEVEL);
            LEVEL = TOP_LEVEL;
        }

        // some pending level now has a result

        goto bounce_on_trampoline_skip_just_use_out;
    }

  //=//// HANDLE CONTINUATIONS ////////////////////////////////////////////=//
  //
  // 1. It's legal for a level to implement itself in terms of another level
  //    that is compatible.  This could have a separate signal, but for now
  //    it's done as BOUNCE_CONTINUE.  Since that delegation may be to an
  //    INITIAL_ENTRY state, the zero STATE_0 needs to be allowed.
  //
  // 2. If a level besides the one that we ran is above on the stack, then
  //    the level is using that continuation to get a result it is interested
  //    in.  It needs to know it did a push, so the state must be nonzero.
  //
  //    (Technically there could be some other level field modified to let it
  //    know there was an effect, but we enforce the nonzero rule because it
  //    also helps with bookkeeping and GC features, allowing the zero value
  //    to be reserved to mean something else.)

    if (r == BOUNCE_CONTINUE) {
        if (LEVEL != TOP_LEVEL)  // continuing self ok, see [1]
            assert(STATE != 0);  // otherwise state enforced nonzero, see [2]

        LEVEL = TOP_LEVEL;
        goto bounce_on_trampoline_skip_just_use_out;
    }

    if (r == BOUNCE_DELEGATE) {
        //
        // We could unhook the level from the stack here, but leaving it in
        // provides clarity in the stack.   Hence this should not be used in
        // tail call situations.
        //
        STATE = DELEGATE_255;  // maintain non-zero invariant
        LEVEL->executor = &Delegated_Executor;

        LEVEL = TOP_LEVEL;
        goto bounce_on_trampoline;
    }

    if (r == BOUNCE_SUSPEND) {  // just to get emscripten started w/o Asyncify
        CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
        return BOUNCE_SUSPEND;
    }


  //=//// HANDLE THROWS, INCLUDING (NON-ABRUPT) ERRORS ////////////////////=//

    // 1. Having handling of UNWIND be in the trampoline means that any level
    //    can be "teleported to" with a result, not just action levels.  It
    //    has a notable use by RETURN from a FUNC, which considers its type
    //    checking to be finished so it can skip past the Action_Executor().
    //
    //    !!! Using BOUNCE_THROWN makes it possible for UNWIND to be offered to
    //    dispatchers that catch throws.  This is used for instance in MACRO,
    //    which intercepts the UNWIND issued by RETURN, because it doesn't want
    //    to actually return the block (it wants to splice it).  But that may
    //    suggest MACRO wants to use its own throw type in a definitional
    //    return, so that you could generically UNWIND to a macro level and
    //    overwrite the result verbatim.
    //
    // 2. Note LEVEL->varlist may be SERIES_FLAG_INACCESSIBLE here.  This can
    //    happen with RETURN during ENCLOSE.  So don't use CTX(LEVEL->varlist)
    //    here, as that would try to validate it as not being inaccessible.
    //
    // 3. Constructs like REDUCE-EACH keep a sublevel pushed to do evaluation,
    //    but then want to keep that state while doing another evaluation
    //    (e.g. the body block).  To "punch a hole" through the evaluation
    //    level it sets the executor to Just_Use_Out and can get the result
    //    without dropping the level.  But thrown values like CONTINUE lead
    //    to a problem of how to express wanting TRAMPOLINE_KEEPALIVE to be
    //    applicable to throw situations as well--not all want it.  For now
    //    we conflate Just_Use_Out with the intent of keepalive on throw.

    if (r == BOUNCE_THROWN) {
      thrown:

        assert(LEVEL == TOP_LEVEL);  // Action_Executor() helps, drops inerts

        /*assert(not IS_CFUNC_TRASH_DEBUG(Executor*, LEVEL->executor));*/
        Trash_Cfunc_If_Debug(Executor*, LEVEL->executor);

        if (Get_Level_Flag(LEVEL, ABRUPT_FAILURE)) {
            //
            // They had their chance to clean up.
            // Fail again as definitional error, but this time don't notify.
            //
            assert(Get_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE));
            Clear_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE);
            Clear_Level_Flag(LEVEL, ABRUPT_FAILURE);
            assert(IS_ERROR(VAL_THROWN_LABEL(LEVEL)));
            Context(*) ctx = VAL_CONTEXT(VAL_THROWN_LABEL(LEVEL));
            CATCH_THROWN(SPARE, LEVEL);
            fail (ctx);
        }

        const REBVAL *label = VAL_THROWN_LABEL(LEVEL);  // unwind, see [1]
        if (
            IS_FRAME(label)
            and VAL_ACTION(label) == VAL_ACTION(Lib(UNWIND))
            and TG_Unwind_Level == LEVEL  // may be inaccessible, see [2]
        ){
            CATCH_THROWN(OUT, LEVEL);
            goto result_in_out;
        }

        if (Get_Level_Flag(LEVEL, ROOT_LEVEL)) {  // don't abort top
            assert(Not_Level_Flag(TOP_LEVEL, TRAMPOLINE_KEEPALIVE));
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
            return BOUNCE_THROWN;
        }

        Drop_Level(LEVEL);  // restores to baseline
        LEVEL = TOP_LEVEL;

        if (LEVEL->executor == &Just_Use_Out_Executor) {
            if (Get_Level_Flag(LEVEL, TRAMPOLINE_KEEPALIVE))
                LEVEL = LEVEL->prior;  // don't let it be aborted, see [3]
        }

        goto bounce_on_trampoline;  // executor will see the throw
    }

    assert(!"executor(L) not OUT, BOUNCE_THROWN, or BOUNCE_CONTINUE");
    panic (cast(void*, r));

} ON_ABRUPT_FAILURE(Context(*) e) {  /////////////////////////////////////////

  // 1. An abrupt fail(...) is treated as a "thrown error", which can not be
  //    intercepted in the same way as a definitional error can be.
  //
  //   (It was wondered if since we know what LEVEL was "in control" when a
  //    fail() occurred, if this code should put a Raised() in the output
  //    slot...as if a `return RAISE(xxx)` occurred.  But just because we know
  //    what LEVEL the trampoline last called does not mean every incidental
  //    error should be attributed to being "from" that level.  That would
  //    promote any incidental error--like "out of memory", that could come
  //    from any nested library call--to being definitional.)
  //
  // 2. A level that asked to be notified about abrupt failures will get the
  //    failure thrown.  The default behavior of staying in the thrown state
  //    will be to convert it to a definitional failure on return.
  //
  //    e.g. ABRUPT_FAILURE + NOTIFIY_ON_ABRUPT_FAILURE + return BOUNCE_THROWN
  //    will act as if there had never been a NOTIFY_ON_ABRUPT_FAILURE

    ASSERT_CONTEXT(e);
    assert(CTX_TYPE(e) == REB_ERROR);

    Set_Level_Flag(LEVEL, ABRUPT_FAILURE);

    FRESHEN_MOVED_CELL_EVIL_MACRO(OUT);  // avoid sweep error under rug assert
    Init_Thrown_Error(LEVEL, CTX_ARCHETYPE(e));  // non-definitional, see [1]

    while (TOP_LEVEL != LEVEL) {  // drop idle levels above the fail
        assert(Not_Level_Flag(TOP_LEVEL, NOTIFY_ON_ABRUPT_FAILURE));
        assert(Not_Level_Flag(TOP_LEVEL, ROOT_LEVEL));

        if (Is_Action_Level(TOP_LEVEL)) {
            assert(Not_Executor_Flag(ACTION, TOP_LEVEL, DISPATCHER_CATCHES));
            assert(not Is_Level_Fulfilling(TOP_LEVEL));
            Drop_Action(TOP_LEVEL);
        }

        Drop_Level(TOP_LEVEL);  // will call va_end() if variadic level
    }

    if (Not_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE)) {
        if (Get_Level_Flag(LEVEL, ROOT_LEVEL)) {
            CLEANUP_BEFORE_EXITING_TRAP_BLOCK;
            return BOUNCE_THROWN;
        }

        Drop_Level(LEVEL);
    }

    CLEANUP_BEFORE_EXITING_TRAP_BLOCK;  /* Note: changes LEVEL */
    // (TOP_LEVEL will become LEVEL again after push_trap)
    goto bounce_on_trampoline_with_trap;  // after exception, need new trap
}}


//
//  Trampoline_With_Top_As_Root_Throws: C
//
bool Trampoline_With_Top_As_Root_Throws(void)
{
    Level(*) root = TOP_LEVEL;

  #if !defined(NDEBUG)
    struct Reb_Jump *check = TG_Jump_List;
    assert(Not_Level_Flag(root, ROOT_LEVEL));
  #endif

    // !!! More efficient if caller sets this, but set it ourselves for now.
    //
    Set_Level_Flag(root, ROOT_LEVEL);  // can't unwind across, see [1]

    Bounce r = Trampoline_From_Top_Maybe_Root();

  #if !defined(NDEBUG)
    assert(check == TG_Jump_List);  // must CLEANUP_BEFORE_EXITING_TRAP_BLOCK
    assert(TOP_LEVEL == root);
    assert(Get_Level_Flag(root, ROOT_LEVEL));
  #endif

    Clear_Level_Flag(root, ROOT_LEVEL);

    if (r == BOUNCE_THROWN)
        return true;
    if (r == root->out)
        return false;

  #if DEBUG_FANCY_PANIC
    if (r == BOUNCE_CONTINUE)
        printf("R is BOUNCE_CONTINUE\n");
    else if (r == BOUNCE_DELEGATE)
        printf("R is BOUNCE_DELEGATE\n");
    else if (r == BOUNCE_REDO_CHECKED)
        printf("R is BOUNCE_REDO_CHECKED\n");
    else if (r == BOUNCE_REDO_UNCHECKED)
        printf("R is BOUNCE_REDO_UNCHECKED\n");
    else if (r == BOUNCE_SUSPEND)
        printf("R is BOUNCE_SUSPEND\n");
    else
        printf("R is something unknown\n");

    Dump_Stack(root);
  #endif

    fail ("Cannot interpret Trampoline result");
}


//
//  Trampoline_Throws: C
//
bool Trampoline_Throws(Atom(*) out, Level(*) root)
{
    Push_Level(out, root);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(root);
    return threw;
}
