//
//  file: %c-trampoline.c
//  summary: "Central Interpreter Loop for 'Stackless' Evaluation"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//    YIELD must know the passed-in level is uncrossable, so that it can do
//    a panic if you try to unwind across a top-level Trampoline call.
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


#if TRAMPOLINE_COUNTS_TICKS  // <-- VERY USEFUL! SEE %sys-tick.h FOR MORE INFO

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
  { crash (L->out); }


//
//  Trampoline_From_Top_Maybe_Root: C
//
Bounce Trampoline_From_Top_Maybe_Root(void)
{
  #if RUNTIME_CHECKS && CPLUSPLUS_11  // capture to easily view in watchlist
    Tick& tick = g_tick;  // C++ reference always reflects current value
    USED(tick);
  #endif

  bounce_on_trampoline_with_recover:

  // RECOVER_SCOPE is an abstraction of `try {} catch(...) {}` which can also
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
  // panic() has occurred.  Else jump to `bounce_on_trampoline_with_recover`
  // to put the rescue back into effect.

  RECOVER_SCOPE_CLOBBERS_ABOVE_LOCALS_IF_MODIFIED {  /////////////////////////

    Level* L = TOP_LEVEL;  // Current level changes, and isn't always top...

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

    Maybe_Trampoline_Break_On_Tick(L);  // C-DEBUG-BREAK native calls land here

  //=//// CALL THE EXECUTOR ///////////////////////////////////////////////=//

    // Note that the executor may push more levels, or change the executor of
    // the level it receives.

    // v-- **STEP IN** --v  ...if you want to debug the next evaluation...!

    Bounce bounce = (( Apply_Cfunc(L->executor, L) ));

    // ^-- **STEP IN** --^  ...if you want to debug the next evaluation...!

  //=//// HANDLE THROWS, INCLUDING (NON-ABRUPT) FAILURES //////////////////=//

  // We have to handle throws first (e.g. before recycling)...because it is
  // possible to do things like Push_Lifeguard() on a stack cell.  That state
  // has to be rolled back before you do something like try to mark the
  // guarded list.
  //
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
        Corrupt_If_Needful(L->executor);*/

        const Value* label = VAL_THROWN_LABEL(L);  // unwind [1]
        if (
            Is_Frame(label)
            and Frame_Phase(label) == Frame_Phase(LIB(UNWIND))
            and g_ts.unwind_level == L  // may be inaccessible [2]
        ){
            CATCH_THROWN(L->out, L);
            goto result_in_out;
        }

        if (Get_Level_Flag(L, ROOT_LEVEL)) {  // don't abort top
            assert(Not_Level_Flag(TOP_LEVEL, TRAMPOLINE_KEEPALIVE));
            CLEANUP_BEFORE_EXITING_RECOVER_SCOPE;
            return BOUNCE_THROWN;
        }

        Rollback_Level(L);  // restores to baseline
        Drop_Level(L);
        L = TOP_LEVEL;

        if (L->executor == &Just_Use_Out_Executor) {
            if (Get_Level_Flag(L, TRAMPOLINE_KEEPALIVE))
                L = L->prior;  // don't let it be aborted [3]
        }

        goto bounce_on_trampoline;  // executor will see the throw
    }

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

        if (Get_Level_Flag(L, FORCE_HEAVY_NULLS)) {
            assert(Get_Level_Flag(L, FORCE_SURPRISING));  // true as of now
            if (Is_Ghost(L->out))
                Init_Void(L->out);  // !!! should this be a separate flag?
            else if (Is_Light_Null(L->out))
                Init_Heavy_Null(L->out);
        }

        if (Get_Level_Flag(L, FORCE_SURPRISING))
            Clear_Cell_Flag(L->out, OUT_HINT_UNSURPRISING);

        if (Get_Level_Flag(L, ROOT_LEVEL)) {
            assert(L == TOP_LEVEL);
            CLEANUP_BEFORE_EXITING_RECOVER_SCOPE;
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
        CLEANUP_BEFORE_EXITING_RECOVER_SCOPE;
        return BOUNCE_SUSPEND;
    }

  //=//// HANDLE `panic ()` CASE /////////////////////////////////////=//

    // When you do `panic (...)` in an executor or dispatcher, that is
    // a "cooperative panic".  These are preferred to calling `panic (...)`
    // (which is based on longjmp() or C++ exceptions).  In addition to being
    // more efficient, the `panic (...)` mechanics work on platforms
    // without longjmp() or C++ exceptions.  Otherwise, all they can do in
    // response to a panic() is crash.
    //
    // Cooperative panics offer themselves back to the executor that was
    // running when they were raised.  This means they get a chance to do
    // cleanup (just as they have for the longjmp() and C++ exception cases).
    //
    // 1. STATE_BYTE() won't allow reads if you are DISPATCHING_INTRINSIC,
    //    since the intrinsic does not own the state byte.  But the flag has
    //    to be set for (panic (...)) in order to blame the right call
    //    (e.g. Native_Panic_Result() is DISPATCHING_INTRINSIC-aware).  It
    //    would be a burden for the Executor to have to clear the flag between
    //    generating the error blame and returning, so just clear flag here.

    bounce = opt Irreducible_Bounce(TOP_LEVEL, bounce);

    if (bounce == BOUNCE_THROWN) {
        assert(Is_Throwing_Panic(TOP_LEVEL));
        Clear_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC);  // convenience [1]
        L = TOP_LEVEL;
        goto bounce_on_trampoline;
    }

    if (not bounce)  // was API value, fail ERROR!, etc.
        goto result_in_out;

    assert(!"executor(L) not OUT, BOUNCE_THROWN, or BOUNCE_CONTINUE");
    crash (bounce);

} ON_ABRUPT_PANIC (Error* e) {  //////////////////////////////////////////////

  // A panic() can happen at any moment--even due to something like a memory
  // allocation requested by an executor itself.  These are "abrupt panics",
  // and they can't be RESCUE'd, TRY'd, or EXCEPT'd like an ERROR! can be.
  //
  // 1. We don't really know *what* panicked...all levels get a chance to
  //    clean up the state.
  //
  //    (Example: When something like ALL is "between steps", the level it
  //    pushed to process its block will be above it on the stack.  If the
  //    ALL decides to call panic(), the non-running stack level can be
  //    "TOP_LEVEL" above the ALL's level whose executor was pushed.)
  //
  // 2. If a native protected the output Cell as a sanity check in the
  //    debug build, it won't run the code path to clean that up here.

    Level* L = TOP_LEVEL;  // may not be same as L whose executor() called [1]

    Assert_Varlist(e);
    assert(CTX_TYPE(e) == TYPE_WARNING);

    Clear_Lingering_Out_Cell_Protect_If_Debug(L);  // abrupt skips cleanup [2]
    Init_Thrown_Panic(L, e);

    possibly(Get_Level_Flag(L, DISPATCHING_INTRINSIC));  // panic in intrinsic
    Clear_Level_Flag(L, DISPATCHING_INTRINSIC);

    goto bounce_on_trampoline_with_recover;  // abrupt panic "used up" rescue

} DEAD_END; }


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

  #if DEBUG_FANCY_CRASH
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
        panic ("Cannot interpret Trampoline result");
    }
  #endif

  #if RUNTIME_CHECKS
    assert(check == g_ts.jump_list);  // CLEANUP_BEFORE_EXITING_RECOVER_SCOPE
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
bool Trampoline_Throws(Need(Atom*) out, Level* root)
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

    require (
      Level* L = Make_End_Level(  // ensure L->prior [1]
        &Stepper_Executor,  // executor is irrelevant (permit nullptr?)
        LEVEL_FLAG_UNINTERRUPTIBLE  // can't interrupt while initializing [2]
    ));
    Push_Level_Dont_Inherit_Interruptibility(  // to attach API handles to [3]
        u_cast(Atom*, &g_erased_cell),
        L
    );

    Corrupt_If_Needful(L->prior);  // catches enumeration past bottom_level
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
    Assert_Corrupted_If_Needful(TOP_LEVEL->prior);  // corrupt from start [1]

    Drop_Level_Unbalanced(TOP_LEVEL);  // can't do balance check [2]

    g_ts.top_level = nullptr;
    g_ts.bottom_level = nullptr;

  #if RUNTIME_CHECKS
    Check_Level_Pool_For_Leaks();
    Check_Feed_Pool_For_Leaks();
  #endif

  #if TRAMPOLINE_COUNTS_TICKS
    g_tick = 0;
  #endif
}


//
//  Rollback_Level: C
//
// 1. On normal completion with a return result, we do not allow API handles
//    attached to a level to leak--you are expected to release everything.
//    But panic () or return fail () cases are exempt.
//
//    !!! This may be reviewed in light of wanting to make API programming
//    easier, especially for JavaScript.
//
// 2. There could be outstanding values on the data stack, or data in the
//    mold buffer...we clean it up automatically in these cases.
//
void Rollback_Level(Level* L) {
    Base* b = L->alloc_value_list;
    while (b != L) {
        Stub* s = cast(Stub*, b);
        b = LINK_API_STUB_NEXT(s);
        Force_Poison_Cell(Stub_Cell(s));  // lose BASE_FLAG_ROOT
        s->header.bits = STUB_MASK_NON_CANON_UNREADABLE;
        GC_Kill_Stub(s);
    }
    L->alloc_value_list = L;  // circularly linked list (terminates in L)

    Rollback_Globals_To_State(&L->baseline);  // values on data stack, etc. [2]
}
