//
//  File: %sys-eval.h
//  Summary: {Low-Level Internal Evaluator API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2024 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
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
// "Evaluation" refers to the general concept of processing an ANY-LIST! in
// the Rebol language:
//
//     >> [pos value]: evaluate/step [1 + 2 10 + 20]  ; one step of evaluation
//     == [10 + 20]  ; next position
//
//     >> value
//     == 3  ; synthesized result
//
//     >> evaluate [1 + 2 10 + 20]  ; run to end, discard intermediate results
//     == 30
//
// In historical Redbol, this was often done with "DO".  But Ren-C uses DO as
// a more generic tool, which can run other languages (do %some-file.js) and
// dialects.  (It also does not offer a /NEXT facility for stepping.)
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
 // * Ren-C can run the evaluator across an Array*-style input based on index.
//   It can also enumerate through C's `va_list`, providing the ability to
//   pass pointers as Value* to comma-separated input at the source level.
//
//   To provide even greater flexibility, it allows the very first element's
//   pointer in an evaluation to come from an arbitrary source.  It doesn't
//   have to be resident in the same sequence from which ensuing values are
//   pulled, allowing a free head value (such as an ACTION! cell in a local
//   C variable) to be evaluated in combination from another source (like a
//   va_list or Array representing the arguments.)  This avoids the cost and
//   complexity of allocating an Array to combine the values together.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//     !!! EVALUATOR TICK COUNT - VERY USEFUL - READ THIS SECTION !!!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The evaluator `tick` should be visible in the C debugger watchlist as a
// local variable on each evaluator stack level.  So if a fail() happens at a
// deterministic moment in a run, capture the number from the level of interest
// and recompile for a breakpoint at that tick.
//
// If the tick is AFTER command line processing is done, you can request a tick
// breakpoint that way with `--breakpoint NNN`
//
// The debug build carries ticks many other places.  Stubs contain `Stub.tick`
// when created, levels have a `Level.tick`, and the DEBUG_TRACK_EXTEND_CELLS
// switch will double the size of cells so they can carry the tick, file, and
// line where they were initialized.
//
// For custom updating of stored ticks to help debugging some scenarios, see
// Touch_Stub() and Touch_Cell().  Note also that BREAK_NOW() can be called to
// pause and dump state at any moment.
//
#if DEBUG && DEBUG_COUNT_TICKS
    #define Update_Tick_If_Enabled() \
        do { \
            if (TG_tick < UINTPTR_MAX) /* avoid rollover */ \
                TG_tick += 1; /* never zero for g_break_at_tick check */ \
        } while (false)  // macro so that breakpoint is at right stack level!

    #define Maybe_DebugBreak_On_Tick() \
        do { \
            if ( \
                g_break_at_tick != 0 and TG_tick >= g_break_at_tick \
            ){ \
                printf("BREAK AT TICK %lu\n", cast(unsigned long, TG_tick)); \
                Dump_Level_Location(level_); \
                debug_break(); /* see %debug_break.h */ \
                g_break_at_tick = 0; \
            } \
        } while (false)  // macro so that breakpoint is at right stack level!
#else
    #define Update_Tick_If_Enabled() NOOP
    #define Maybe_DebugBreak_On_Tick() NOOP
#endif


// See Evaluator_Executor().  This helps document the places where the primed
// result is being pushed, and gives a breakpoint opportunity for it.
//
INLINE Atom* Alloc_Evaluator_Primed_Result() {
    return atom_PUSH();
}

INLINE void Restart_Stepper_Level(Level* L) {
    assert(L->executor == &Stepper_Executor);
    Level_State_Byte(L) = STATE_0;
}

#define Init_Pushed_Refinement(out,symbol) \
    Init_Any_Word((out), REB_THE_WORD, symbol)

#define Init_Pushable_Refinement_Bound(out,symbol,context,index) \
    Init_Any_Word_Bound((out), REB_THE_WORD, (symbol), (context), (index))

#define Is_Pushed_Refinement Is_The_Word

INLINE Value* Refinify_Pushed_Refinement(Value* v) {
    assert(Is_Pushed_Refinement(v));
    return Refinify(Plainify(v));
}


// !!! This is a non-stackless invocation of the evaluator to perform one
// evaluation step.  Callsites that use it should be rewritten to yield to
// the trampoline.
//
INLINE bool Eval_Step_Throws(Atom* out, Level* L) {
    assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));

    assert(L->executor == &Stepper_Executor);

    L->out = out;
    assert(L->baseline.stack_base == TOP_INDEX);

    assert(L == TOP_LEVEL);  // should already be pushed, use core trampoline

    return Trampoline_With_Top_As_Root_Throws();
}


// !!! This is a non-stackless invocation of the evaluator to perform a full
// evaluation.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Any_List_At_Core_Throws(
    Atom* out,
    Flags flags,
    const Cell* list,
    Specifier* specifier
){
    Init_Void(Alloc_Evaluator_Primed_Result());
    Level* L = Make_Level_At_Core(
        &Evaluator_Executor,
        list, specifier,
        flags
    );

    return Trampoline_Throws(out, L);
}

#define Eval_Any_List_At_Throws(out,list,specifier) \
    Eval_Any_List_At_Core_Throws(out, LEVEL_MASK_NONE, (list), (specifier))


// !!! This is a non-stackless invocation of the evaluator that evaluates a
// single value.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Value_Core_Throws(
    Atom* out,
    Flags flags,
    const Element* value,  // e.g. a BLOCK! here would just evaluate to itself!
    Specifier* specifier
){
    if (Any_Inert(value)) {
        Copy_Cell(out, value);
        return false;  // fast things that don't need levels (should inline)
    }

    Feed* feed = Prep_Array_Feed(
        Alloc_Feed(),
        value,  // first--in this case, the only value in the feed...
        EMPTY_ARRAY,  // ...because we're using the empty array after that
        0,  // ...at index 0
        specifier,
        FEED_MASK_DEFAULT | (value->header.bits & FEED_FLAG_CONST)
    );

    Level* L = Make_Level(&Stepper_Executor, feed, flags);

    return Trampoline_Throws(out, L);
}

#define Eval_Value_Throws(out,value,specifier) \
    Eval_Value_Core_Throws(out, LEVEL_MASK_NONE, (value), (specifier))


// !!! This is a non-stackless invocation of the evaluator that evaluates a
// single value.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Branch_Throws(
    Atom* out,
    const Value* branch
){
    if (not Pushed_Continuation(
        out,
        LEVEL_FLAG_BRANCH,
        SPECIFIED, branch,
        nullptr
    )){
        return false;
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(TOP_LEVEL);
    return threw;
}


// !!! Review callsites for which ones should be interruptible and which ones
// should not.
//
#define rebRunThrows(out,...) \
    rebRunCoreThrows_internal( \
        (out), \
        EVAL_EXECUTOR_FLAG_NO_RESIDUE | LEVEL_FLAG_UNINTERRUPTIBLE, \
        __VA_ARGS__ \
    )

#define rebRunThrowsInterruptible(out,...) \
    rebRunCoreThrows_internal( \
        (out), \
        EVAL_EXECUTOR_FLAG_NO_RESIDUE, \
        __VA_ARGS__ \
    )
