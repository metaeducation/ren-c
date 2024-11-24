//
//  File: %c-trampoline.c
//  Summary: "Central Interpreter Loop for 'Stackless' Evaluation"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2024 Ren-C Open Source Contributors
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


#if TRAMPOLINE_COUNTS_TICKS  // <-- USEFUL!  SEE Maybe_Debug_Break_On_Tick()

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
Bounce Just_Use_Out_Executor(Level* L)
  { panic (L->out); }


//
//  Trampoline_From_Top_Maybe_Root: C
//
Bounce Trampoline_From_Top_Maybe_Root(void)
{
  #if RUNTIME_CHECKS && CPLUSPLUS_11  // capture to easily view in watchlist
    Tick& tick = g_tick;  // C++ reference always reflects current value
    USED(tick);
  #endif

    Level* L = TOP_LEVEL;  // Current level changes, and isn't always top...

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

    // 1. The Just_Use_Out_Executor() exists vs. using something like nullptr
    //    for the executor just to make it more obvously intentional that a
    //    passthru is intended.  (Review in light of use of nonzero for GC
    //    and bookkeeping purposes; could STATE of 255 mean Just_Use_Out?)
    //
    // 2. The rule for Levels that are continuations/delegations is that they
    //    cannot be in STATE_0.  But an additional constraint is that if a
    //    Level is in STATE_0 that its output cell is erased.  It's something
    //    that helps avoid leaking values into an evaluation, and also makes
    //    sure that Executors and Dispatchers write something to the output
    //    before returning it.  Plus, it gives Executors and Dispatchers a
    //    reliable test for whether they've written the output or not--which
    //    can be a useful implicit "flag".  So there's a lot of benefits.
    //
    //    (At one point the Trampoline itself did an Erase_Cell() here, but
    //    that meant every trampoline bounce in the release build would have
    //    to test the state byte...and sometimes cells were getting doubly
    //    erased.  So the responsbility was shifted to Push_Level() and
    //    cases that reuse levels, e.g. Reset_Evaluator_Erase_Out())

  bounce_on_trampoline_skip_just_use_out:

    while (L->executor == &Just_Use_Out_Executor)
        L = L->prior;  // fast skip, allow Is_Cell_Erased() output

  bounce_on_trampoline:

    Assert_No_DataStack_Pointers_Extant();

    assert(L->executor != &Just_Use_Out_Executor);  // drops skip [1]

    if (LEVEL_STATE_BYTE(L) == STATE_0)
        assert(Is_Cell_Erased(L->out));  // useful invariant for STATE_0 [2]

    possibly(L != TOP_LEVEL);  // e.g. REDUCE keeps an evaluator pushed

    Maybe_Debug_Break_On_Tick(L);  // C-DEBUG-BREAK native calls land here

  //=//// CALL THE EXECUTOR ///////////////////////////////////////////////=//

    // Note that the executor may push more levels, or change the executor of
    // the level it receives.
    //
    // **STEP IN** if you want to debug the next evaluation...!

    Bounce bounce = ((( L->executor )))((( L )));

  //=//// PROCESS SIGNALS (RECYCLE, HALT, ETC.) ///////////////////////////=//

    // Doing signals covers several things that may cause interruptions:
    //
    //  * Running the garbage collector
    //  * Noticing when a HALT was requested
    //  * (future?) Allowing a break into an interactive debugger
    //
    // 1. We could increment total_eval_cycles here so it's always up-to-date.
    //    But we keep a micro-optimization from R3-Alpha where we only adjust
    //    one counter (the `eval_countdown`) each time through the loop.  Then
    //    we reconcile total_eval_cycles in Do_Signals_Throws() only when the
    //    countdown reaches zero.
    //
    // 2. Garbage collection has to be *after* the Level's Executor is run,
    //    and not before.  This is for several reasons, but one is that code
    //    may depend on the Level being on the stack to guard its OUT slot,
    //    on a Cell that would otherwise not be guarded.

    Update_Tick_If_Enabled();  // Do_Signals_Throws() expects tick in sync

    if (--g_ts.eval_countdown <= 0) {  // defer total_eval_cycles update, [1]
        if (Do_Signals_Throws(L))  // garbage collection *after* executor [2]
            goto handle_thrown;
    }

  //=//// HANDLE FINISHED RESULTS ////////////////////////////////////////=//

    if (bounce == L->out) {
      result_in_out:
        UNUSED(bounce);

        assert(Is_Cell_Readable(L->out));

        if (Get_Level_Flag(L, META_RESULT)) {
            Meta_Quotify(L->out);
        }
        else if (Is_Raised(L->out)) {
            if (Not_Level_Flag(L, RAISED_RESULT_OK)) {
                //
                // treat any failure as if it could have been thrown from
                // anywhere, so it is bubbled up as a throw.
                //
                Init_Thrown_Failure(TOP_LEVEL, Cell_Error(L->out));
                L = TOP_LEVEL;
                goto bounce_on_trampoline;
            }
        }
        else if (Get_Level_Flag(L, BRANCH)) {
            Debranch_Output(L->out);  // heavy voids/nulls
        }

        if (Get_Level_Flag(L, ROOT_LEVEL)) {
            assert(L == TOP_LEVEL);
            CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
            return L->out;
        }

        assert(TOP_LEVEL == Adjust_Level_For_Downshift(L));

        L = TOP_LEVEL->prior;

        if (Not_Level_Flag(TOP_LEVEL, TRAMPOLINE_KEEPALIVE))
            Drop_Level(TOP_LEVEL);

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
        if (L != TOP_LEVEL)  // continuing self ok [1]
            assert(LEVEL_STATE_BYTE(L) != 0);  // else state nonzero [2]

        L = TOP_LEVEL;
        goto bounce_on_trampoline_skip_just_use_out;
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
  // 2. Note L->varlist may be garbage here.  This can happen in RETURN during
  //    an ENCLOSE.  Don't cast(VarList*, L->varlist) here, as that would try
  //    to validate it in the DEBUG_CHECK_CASTS build.
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
        UNUSED(bounce);  // ignore, as whatever jumped here wants to throw

        L = Adjust_Level_For_Downshift(L);

        possibly(Is_Cell_Erased(L->out));  // not completely enforced ATM

        // Corrupting the pointer here was well-intentioned, but Drop_Level()
        // needs to know if it is an Action_Executor to drop a stack cell.
        //
        /*assert(not Is_Pointer_Corrupt_Debug(L->executor));
        Corrupt_Pointer_If_Debug(L->executor);*/

        const Value* label = VAL_THROWN_LABEL(L);  // unwind [1]
        if (
            Is_Frame(label)
            and VAL_ACTION(label) == VAL_ACTION(LIB(UNWIND))
            and g_ts.unwind_level == L  // may be inaccessible [2]
        ){
            CATCH_THROWN(L->out, L);
            goto result_in_out;
        }

        if (Get_Level_Flag(L, ROOT_LEVEL)) {  // don't abort top
            assert(Not_Level_Flag(TOP_LEVEL, TRAMPOLINE_KEEPALIVE));
            CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;
            return BOUNCE_THROWN;
        }

        Drop_Level(L);  // restores to baseline
        L = TOP_LEVEL;

        if (L->executor == &Just_Use_Out_Executor) {
            if (Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE))
                L = L->prior;  // don't let it be aborted [3]
        }

        goto bounce_on_trampoline;  // executor will see the throw
    }

  //=//// HANDLE `return FAIL()` CASE /////////////////////////////////////=//

    // When you do `return FAIL(...)` in an executor or dispatcher, that is
    // a "cooperative abrupt failure".  These should be preferred to the
    // calling `fail (...)` (which is based on longjmp() or C++ exceptions).
    // In addition to being more efficient, the `return FAIL(...)` mechanics
    // work on platforms without longjmp() or C++ exceptions.  Otherwise, all
    // they can do in response to a fail() is crash.
    //
    // Cooperative abrupt failures offer themselves back to the executor
    // that was running when they were raised.  This means they get a chance
    // to do cleanup (just as they have for the longjmp() and C++ exception
    // cases).

    if (bounce == BOUNCE_FAIL) {
        assert(Is_Throwing_Failure(TOP_LEVEL));
        L = TOP_LEVEL;
        goto bounce_on_trampoline;
    }

    assert(!"executor(L) not OUT, BOUNCE_THROWN, or BOUNCE_CONTINUE");
    panic (cast(void*, bounce));

} ON_ABRUPT_FAILURE(Error* e) {  ///////////////////////////////////////////

    // A fail() can happen at any moment--even due to something like a failed
    // memory allocation requested by an executor itself.  These are called
    // "abrupt failures", and they cannot be TRAP'd or TRY'd in the same way
    // a raised error can be.
    //
    // 1. We don't really know *what* failed...we just know what level we were
    //    running (L) and there may be other levels on top of that.  All
    //    levels get a chance to clean up the state.
    //
    //    (Example: When something like ALL is "between steps", the level it
    //    pushed to process its block will be above it on the stack.  If the
    //    ALL decides to call fail(), the non-running stack level can be
    //    "TOP_LEVEL" above the ALL's "L" level.)

    Assert_Varlist(e);
    assert(CTX_TYPE(e) == REB_ERROR);

    possibly(L != TOP_LEVEL);  // we give pushed levels chance to clean up [1]
    Init_Thrown_Failure(TOP_LEVEL, e);

    L = TOP_LEVEL;

    possibly(Get_Level_Flag(L, DISPATCHING_INTRINSIC));  // fail in intrinsic
    Clear_Level_Flag(L, DISPATCHING_INTRINSIC);

    CLEANUP_BEFORE_EXITING_RESCUE_SCOPE;  // no way around it with longjmp :-(
    goto bounce_on_trampoline_with_rescue;  // abrupt failure "used up" rescue
}}


//
//  Trampoline_With_Top_As_Root_Throws: C
//
bool Trampoline_With_Top_As_Root_Throws(void)
{
    Level* root = TOP_LEVEL;

  #if RUNTIME_CHECKS
    Jump* check = g_ts.jump_list;
  #endif

    // !!! More efficient if caller sets this, but set it ourselves for now.
    //
    assert(Not_Level_Flag(root, ROOT_LEVEL));
    Set_Level_Flag(root, ROOT_LEVEL);  // can't unwind across [1]

    Bounce r = Trampoline_From_Top_Maybe_Root();

  #if DEBUG_FANCY_PANIC
    const char* name = "<<UNKNOWN>>";
    if (
        (r != BOUNCE_THROWN) and (r != root->out) and (
            (r == BOUNCE_CONTINUE and (name = "CONTINUE"))
            or (r == BOUNCE_DELEGATE and (name = "DELEGATE"))
            or (r == BOUNCE_REDO_CHECKED and (name = "REDO_CHECKED"))
            or (r == BOUNCE_REDO_UNCHECKED and (name = "REDO_UNCHECKED"))
            or (r == BOUNCE_SUSPEND and (name = "SUSPEND"))
            or (name = "<<UNKNOWN>>")
        )
    ){
        printf("Trampoline_With_Top_As_Root_Throws() got BOUNCE_%s\n", name);
        Dump_Stack(root);
        fail ("Cannot interpret Trampoline result");
    }
  #endif

  #if RUNTIME_CHECKS
    assert(check == g_ts.jump_list);  // see CLEANUP_BEFORE_EXITING_RESCUE_SCOPE
  #endif

    assert(TOP_LEVEL == root);
    assert(Get_Level_Flag(root, ROOT_LEVEL));
    Clear_Level_Flag(root, ROOT_LEVEL);

    if (r == BOUNCE_THROWN)
        return true;

    assert(r == root->out);
    return false;
}


//
//  Trampoline_Throws: C
//
bool Trampoline_Throws(Atom* out, Level* root)
{
    Push_Level_Erase_Out_If_State_0(out, root);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(root);
    return threw;
}


//
//  Startup_Signals: C
//
// When allocations are performed, they may set flags for signaling the need
// for a recycle etc.  Therefore the bits of trampoline state related to
// evaluation counting and signal management must be initialized very early.
//
void Startup_Signals(void)
{
  #if TRAMPOLINE_COUNTS_TICKS
    assert(g_tick == 0);
    g_tick = 1;  // this way tick 0 helps signify no TRAMPOLINE_COUNTS_TICKS
  #endif

    g_ts.signal_flags = 0;
    g_ts.signal_mask = (~ cast(Flags, 0));  // heed all flags by default
    g_ts.eval_dose = EVAL_DOSE;
    g_ts.eval_countdown = g_ts.eval_dose;
    g_ts.total_eval_cycles = 1;  // to match TICK when TRAMPOLINE_COUNTS_TICKS
    g_ts.eval_cycles_limit = 0;
}


//
//  Startup_Trampoline: C
//
// 1. We always push one unused level at the bottom of the stack.  This way, it
//    is not necessary for used levels to check if `L->prior` is null; it
//    may be assumed that it never is.
//
// 2. You can't get a functional system by interrupting the evaluator while
//    it is starting up the environment.  Ctrl-C can only terminate the
//    process...but that is not the business of the interpreter.
//
// 3. Since levels are needed to track API handles, this permits making
//    API handles for things that come into existence at boot and aren't freed
//    until shutdown, as they attach to this level.
//
void Startup_Trampoline(void)
{
    assert(TOP_LEVEL == nullptr);
    assert(BOTTOM_LEVEL == nullptr);

    Level* L = Make_End_Level(  // ensure L->prior [1]
        &Stepper_Executor,  // executor is irrelevant (permit nullptr?)
        LEVEL_FLAG_UNINTERRUPTIBLE  // can't interrupt while initializing [2]
    );
    Push_Level_Dont_Inherit_Interruptibility(&g_erased_cell, L);  // API [3]

    Corrupt_Pointer_If_Debug(L->prior);  // catches enumeration past bottom_level
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
//    the bottom level as a "real stack level", it had a corrupt pointer put
//    in the checked build.  Restore it to a typical null before the drop.
//
// 2. There's a Catch-22 on checking the balanced state for outstanding
//    manual Flex allocations, e.g. it can't check *before* the mold buffer
//    is freed because it would look like it was a leaked Flex, but it
//    can't check *after* because the mold buffer balance check would crash.
//
void Shutdown_Trampoline(void)
{
    assert(TOP_LEVEL == BOTTOM_LEVEL);
    assert(Is_Pointer_Corrupt_Debug(BOTTOM_LEVEL->prior));  // corrupt [1]

    Drop_Level_Core(TOP_LEVEL);  // can't do balance check [2]

    g_ts.top_level = nullptr;
    g_ts.bottom_level = nullptr;

  #if RUNTIME_CHECKS
  blockscope {
    Segment* seg = g_mem.pools[LEVEL_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        Count n = g_mem.pools[LEVEL_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[LEVEL_POOL].wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            Level* L = cast(Level*, unit);  // ^-- pool size may round up
          #if TRAMPOLINE_COUNTS_TICKS
            printf(
                "** FRAME LEAKED at tick %lu\n",
                cast(unsigned long, L->tick)
            );
          #else
            assert(!"** FRAME LEAKED but TRAMPOLINE_COUNTS_TICKS not enabled");
          #endif
        }
    }
  }
  #endif

  #if RUNTIME_CHECKS
  blockscope {
    Segment* seg = g_mem.pools[FEED_POOL].segments;

    for (; seg != nullptr; seg = seg->next) {
        REBLEN n = g_mem.pools[FEED_POOL].num_units_per_segment;
        Byte* unit = cast(Byte*, seg + 1);

        for (; n > 0; --n, unit += g_mem.pools[FEED_POOL].wide) {
            if (unit[0] == FREE_POOLUNIT_BYTE)
                continue;

            Feed* feed = cast(Feed*, unit);
          #if TRAMPOLINE_COUNTS_TICKS
            printf(
                "** FEED LEAKED at tick %lu\n",
                cast(unsigned long, feed->tick)
            );
          #else
            assert(!"** FEED LEAKED but no TRAMPOLINE_COUNTS_TICKS enabled\n");
          #endif
        }
    }
  }
  #endif

  #if TRAMPOLINE_COUNTS_TICKS
    g_tick = 0;
  #endif
}


//
//  Drop_Level_Core: C
//
void Drop_Level_Core(Level* L) {
    possibly(L != TOP_LEVEL);  // e.g. called by Clean_Plug_Handle()

    if (
        Is_Throwing(L)
        or (L->out and not Is_Cell_Erased(L->out) and Is_Raised(L->out))
    ){
        // On normal completion with a return result, we do not allow API
        // handles attached to a level to leak--you are expected to release
        // everything.  But definitional failure and throw cases are exempt.
        //
        Node* n = L->alloc_value_list;
        while (n != L) {
            Stub* s = cast(Stub*, n);
            n = LINK(ApiNext, s);
            Force_Poison_Cell(Stub_Cell(s));  // lose NODE_FLAG_ROOT
            s->leader.bits = STUB_MASK_NON_CANON_UNREADABLE;
            GC_Kill_Stub(s);
        }
        Corrupt_Pointer_If_Debug(L->alloc_value_list);

        // There could be outstanding values on the data stack, or data in the
        // mold buffer...we clean it up automatically in these cases.
        //
        Rollback_Globals_To_State(&L->baseline);
    }
    else {
      #if RUNTIME_CHECKS
        Node* n = L->alloc_value_list;
        while (n != L) {
            Stub* stub = cast(Stub*, n);
            printf("API handle was allocated but not freed, panic'ing leak\n");
            panic (stub);
        }
        Corrupt_Pointer_If_Debug(L->alloc_value_list);
      #endif
    }

    // Note: Free_Feed() will handle feeding a feed through to its end (which
    // may release handles/etc), so no requirement Level_At(L) be at END.

    Free_Level_Internal(L);
}
