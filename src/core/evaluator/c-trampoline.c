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


#include "sys-core.h"


#if DEBUG_COUNT_TICKS  // <-- EXTREMELY USEFUL!  SEE Maybe_DebugBreak_On_Tick()

    //      *** DON'T COMMIT THIS v-- KEEP IT AT ZERO! ***
    Tick g_break_at_tick =         0;
    //      *** DON'T COMMIT THIS --^ KEEP IT AT ZERO! ***

#endif  // ^-- SERIOUSLY: READ ABOUT C-DEBUG-BREAK AND PLACES TICKS ARE STORED

#define EVAL_DOSE 10000


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
#define level_ (g_ts.jump_list->level)


//
//  Trampoline_From_Top_Maybe_Root: C
//
Bounce Trampoline_From_Top_Maybe_Root(void)
{
  #if DEBUG && CPLUSPLUS_11  // reference capture for easy view in watchlist
    Level(*) & L = LEVEL;
    Tick & tick = TG_tick;
    USED(L);
    USED(tick);
  #endif

  bounce_on_trampoline_with_rescue:

  // RESCUE_SCOPE is an abstraction of `try {} catch(...) {}` which can also
  // work in plain C using setjmp/longjmp().  It's considered desirable to
  // support both approaches: plain C compilation (e.g. with TCC) runs on many
  // legacy/embedded platforms, but structured exception handling has support
  // on other systems like some WebAssembly--where longjmp may not be.
  //
  // Regardless of which implementation you are using, once an "exception" has
  // occurred you must jump up above the block to re-enable the "catching".
  // C++ does not allow a `goto` from outside of `try {}` block into it:
  //
  //    "A goto or switch statement shall not be used to transfer control
  //     into a try block or into a handler. "
  //
  // In the case of a C setjmp()/longjmp(), we have "used up" the jump buffer
  // after a longjmp() occurs, so obviously it needs to be re-setjmp'd.
  //
  // So either way, we can only jump to `bounce_on_trampoline` if no abrupt
  // fail() has occurred.  Else jump to `bounce_on_trampoline_with_rescue`
  // to put the rescue back into effect.

  RESCUE_SCOPE_IN_CASE_OF_ABRUPT_FAILURE {  //////////////////////////////////

  bounce_on_trampoline_skip_just_use_out:

    while (LEVEL->executor == &Just_Use_Out_Executor)
        LEVEL = LEVEL->prior;  // fast skip, allow Is_Fresh() output

  bounce_on_trampoline:

  // 1. The Just_Use_Out_Executor() exists vs. using something like nullptr
  //    for the executor just to make it more obvously intentional that a
  //    passthru is intended.  (Review in light of use of nonzero for GC
  //    and bookkeeping purposes; e.g. could STATE of 255 mean Just_Use_Out?)
  //
  // 2. In R3-Alpha, micro-optimizations were stylized so that it would set
  //    a counter for how many cycles would pass before it automatically
  //    triggered garbage collection.  It would decrement that counter
  //    looking for zero, and when zero was reached it would add the number of
  //    cycles it had been counting down from to the total.  This avoided
  //    needing to do math on multiple counters on every eval step...limiting
  //    it to a periodic reconciliation when GCs occurred.  Ren-C keeps this,
  //    but the debug build double checks that whatever magic is done reflects
  //    the real count.

    Assert_No_DataStack_Pointers_Extant();

    assert(LEVEL->executor != &Just_Use_Out_Executor);  // drops skip, see [1]

    Bounce bounce;

    Update_Tick_If_Enabled();

    if (--g_ts.eval_countdown <= 0) {  // defer total_eval_cycles update, [2]
        //
        // Doing signals covers several things that may cause interruptions:
        //
        //  * Running the garbage collector
        //  * Noticing when a HALT was requested
        //  * (future?) Allowing a break into an interactive debugger
        //
        if (Do_Signals_Throws(LEVEL)) {
            bounce = BOUNCE_THROWN;
            goto handle_thrown;
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

  #if DEBUG
    Level(*) check = LEVEL;  // make sure LEVEL doesn't change during executor
  #endif

    Maybe_DebugBreak_On_Tick();

    // v-- This is the g_break_at_tick or C-DEBUG-BREAK landing spot --v
                    bounce = (LEVEL->executor)(LEVEL);
    // ^-- **STEP IN** to this call using the debugger to debug it!!! --^

  #if DEBUG
    assert(LEVEL == check);  // R is relative to the OUT of LEVEL we executed
  #endif

} //=//// HANDLE FINISHED RESULTS /////////////////////////////////////////=//

    if (Get_Level_Flag(LEVEL, ABRUPT_FAILURE)) {
        assert(Get_Level_Flag(LEVEL, NOTIFY_ON_ABRUPT_FAILURE));
        assert(bounce == BOUNCE_THROWN);
        assert(IS_ERROR(VAL_THROWN_LABEL(LEVEL)));
    }

    if (bounce == OUT) {
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
                Init_Thrown_Failure(LEVEL, stable_OUT);
                goto handle_thrown;
            }
        }
        else if (Get_Level_Flag(LEVEL, BRANCH)) {
            Debranch_Output(OUT);  // make heavy voids, clear ELSE/THEN methods
        }

        if (Get_Level_Flag(LEVEL, ROOT_LEVEL)) {  // may keepalive sublevels
            CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;  // switches LEVEL pointer...
            return TOP_LEVEL->out;  // ...so return new top_level->out
        }

        // Some natives and executors want to be able to leave a pushed level
        // intact as the "top of stack" even when it has completed.  This
        // means that when those executors run, their level parameter is
        // not the technical top of the stack.
        //
        if (Get_Level_Flag(LEVEL, TRAMPOLINE_KEEPALIVE)) {
            LEVEL = LEVEL->prior;
            assert(LEVEL != TOP_LEVEL);  // should *not* be top of stack
        }
        else {
            assert(LEVEL == TOP_LEVEL);  // should be top of the stack
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

    if (bounce == BOUNCE_CONTINUE) {
        if (LEVEL != TOP_LEVEL)  // continuing self ok, see [1]
            assert(STATE != 0);  // otherwise state enforced nonzero, see [2]

        LEVEL = TOP_LEVEL;
        goto bounce_on_trampoline_skip_just_use_out;
    }

    if (bounce == BOUNCE_DELEGATE) {
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

    if (bounce == BOUNCE_SUSPEND) {  // to get emscripten started w/o Asyncify
        CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
        return BOUNCE_SUSPEND;
    }


  //=//// HANDLE THROWS, INCLUDING (NON-ABRUPT) FAILURES //////////////////=//

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

    if (bounce == BOUNCE_THROWN) {
      handle_thrown:

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
            and g_ts.unwind_level == LEVEL  // may be inaccessible, see [2]
        ){
            CATCH_THROWN(OUT, LEVEL);
            goto result_in_out;
        }

        if (Get_Level_Flag(LEVEL, ROOT_LEVEL)) {  // don't abort top
            assert(Not_Level_Flag(TOP_LEVEL, TRAMPOLINE_KEEPALIVE));
            CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
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
    panic (cast(void*, bounce));

} ON_ABRUPT_FAILURE(Context(*) e) {  /////////////////////////////////////////

  // A fail() can happen at any moment--even due to something like a failed
  // memory allocation requested by an executor itself.  These are called
  // "abrupt failures" (see LEVEL_FLAG_ABRUPT_FAILURE).  They cannot be
  // TRAP'd or TRY'd in the same way a raised error can be.
  //
  // 1. Just because we know what LEVEL was "in control" when a fail()
  //    occurred, we don't put a raised error in the output slot...as if a
  //    `return RAISE(xxx)` occurred.  That would promote any incidental error
  //    like "out of memory"--that could come from any nested library call--to
  //    being definitional.  This doesn't align with raised errors being a
  //    limited set of contractual return values for the routine.
  //
  // 2. When fails occur, any levels which have pushed that the trampoline
  //    is not running currently will be above LEVEL.  These are not offered
  //    the chance to handle or rescue the error.
  //
  //    (Example: When something like ALL is "between steps", the level it
  //     pushed to process its block will be above it on the stack.  If the ALL
  //     decides to call fail(), the non-running stack level can be "TOP_LEVEL"
  //     above the ALL's "LEVEL".)

    ASSERT_CONTEXT(e);
    assert(CTX_TYPE(e) == REB_ERROR);

    Set_Level_Flag(LEVEL, ABRUPT_FAILURE);

    FRESHEN_MOVED_CELL_EVIL_MACRO(OUT);  // avoid sweep error under rug assert
    Init_Thrown_Failure(LEVEL, CTX_ARCHETYPE(e));  // non-definitional, see [1]

    while (TOP_LEVEL != LEVEL) {  // drop idle levels above the fail, see [2]
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
            CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
            return BOUNCE_THROWN;
        }

        Drop_Level(LEVEL);
    }

    CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;  /* Note: changes LEVEL */
    // (TOP_LEVEL will become LEVEL again after RESCUE_SCOPE)
    goto bounce_on_trampoline_with_rescue;  // after exception, need new rescue
}}


//
//  Trampoline_With_Top_As_Root_Throws: C
//
bool Trampoline_With_Top_As_Root_Throws(void)
{
    Level(*) root = TOP_LEVEL;

  #if !defined(NDEBUG)
    Jump* check = g_ts.jump_list;
    assert(Not_Level_Flag(root, ROOT_LEVEL));
  #endif

    // !!! More efficient if caller sets this, but set it ourselves for now.
    //
    Set_Level_Flag(root, ROOT_LEVEL);  // can't unwind across, see [1]

    Bounce r = Trampoline_From_Top_Maybe_Root();

  #if !defined(NDEBUG)
    assert(check == g_ts.jump_list);  // see CLEANUP_BEFORE_EXITING_RESCUE_SCOPE
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


//
//  Startup_Signals: C
//
// When allocations are performed, they may set flags for signaling the need
// for a recycle/etc.  Therefore the bits of trampoline state related to
// evaluation counting and signal management must be initialized very early.
//
void Startup_Signals(void)
{
    g_ts.eval_signals = 0;
    g_ts.eval_sigmask = ALL_BITS;
    g_ts.eval_dose = EVAL_DOSE;
    g_ts.eval_countdown = g_ts.eval_dose;
    g_ts.total_eval_cycles = 0;
    g_ts.eval_cycles_limit = 0;
}


//
//  Startup_Trampoline: C
//
// 1. We always push one unused level at the bottom of the stack.  This way, it
//    is not necessary for used levels to check if `L->prior` is null; it
//    may be assumed that it never is.
//
// 2. Also: since levels are needed to track API handles, this permits making
//    API handles for things that come into existence at boot and aren't freed
//    until shutdown, as they attach to this level.
//
void Startup_Trampoline(void)
{
    assert(TOP_LEVEL == nullptr);
    assert(BOTTOM_LEVEL == nullptr);

    Level(*) L = Make_End_Level(LEVEL_MASK_NONE);  // ensure L->prior, see [1]
    Push_Level(nullptr, L);  // global API handles attach here, see [2]

    Trash_Pointer_If_Debug(L->prior);  // catches enumeration past bottom_level
    g_ts.bottom_level = L;

    assert(TOP_LEVEL == L and BOTTOM_LEVEL == L);

    assert(g_ts.jump_list == nullptr);

    // The thrown arg is not intended to ever be around long enough to be
    // seen by the GC.
    //
    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    assert(Is_Cell_Erased(&g_ts.thrown_label));

    assert(g_ts.unwind_level == nullptr);
}


//
//  Shutdown_Trampoline: C
//
// 1. To stop enumerations from using nullptr to stop the walk, and not count
//    the bottom level as a "real stack level", it had a trash pointer put
//    in the debug build.  Restore it to a typical null before the drop.
//
// 2. There's a Catch-22 on checking the balanced state for outstanding
//    manual series allocations, e.g. it can't check *before* the mold buffer
//    is freed because it would look like it was a leaked series, but it
//    can't check *after* because the mold buffer balance check would crash.
//
void Shutdown_Trampoline(void)
{
    assert(TOP_LEVEL == BOTTOM_LEVEL);

    assert(Is_Pointer_Trash_Debug(BOTTOM_LEVEL->prior));  // trash, see [1]
    BOTTOM_LEVEL->prior = nullptr;

  blockscope {
    Level(*) L = TOP_LEVEL;
    Drop_Level_Core(L);  // can't Drop_Level()/Drop_Level_Unbalanced(), see [2]
    assert(not TOP_LEVEL);
  }

    g_ts.top_level = nullptr;
    g_ts.bottom_level = nullptr;

  #if !defined(NDEBUG)
  blockscope {
    Segment* seg = g_mem.pools[LEVEL_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[LEVEL_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[LEVEL_POOL].wide) {
            Level(*) L = cast(Level(*), unit);  // ^-- pool size may round up
            if (Is_Free_Node(L))
                continue;
          #if DEBUG_COUNT_TICKS
            printf(
                "** FRAME LEAKED at tick %lu\n",
                cast(unsigned long, L->tick)
            );
          #else
            assert(!"** FRAME LEAKED but DEBUG_COUNT_TICKS not enabled");
          #endif
        }
    }
  }
  #endif

  #if !defined(NDEBUG)
  blockscope {
    Segment* seg = g_mem.pools[FEED_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        REBLEN n = g_mem.pools[FEED_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[FEED_POOL].wide) {
            Feed(*) feed = cast(Feed(*), unit);
            if (Is_Free_Node(feed))
                continue;
          #if DEBUG_COUNT_TICKS
            printf(
                "** FEED LEAKED at tick %lu\n",
                cast(unsigned long, feed->tick)
            );
          #else
            assert(!"** FEED LEAKED but no DEBUG_COUNT_TICKS enabled\n");
          #endif
        }
    }
  }
  #endif
}
