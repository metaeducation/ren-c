//
//  file: %sys-eval.h
//  summary: "Low-Level Internal Evaluator API"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//     >> [pos value]: evaluate:step [1 + 2 10 + 20]  ; one step of evaluation
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
//   pass pointers as Element* to comma-separated input at the source level.
//
//   To provide even greater flexibility, it allows the very first element's
//   pointer in an evaluation to come from an arbitrary source.  It doesn't
//   have to be resident in the same sequence from which ensuing values are
//   pulled, allowing a free head value (such as an ACTION! cell in a local
//   C variable) to be evaluated in combination from another source (like a
//   va_list or Array representing the arguments.)  This avoids the cost and
//   complexity of allocating an Array to combine the values together.
//


// !!! This is for historical non-stackless code, which needs a place to write
// output for a stepper that has a lifetime at least as long as the Level.
// e.g. this is illegal:
//
//      DECLARE_VALUE (result);  // stack-declared Cell
//      Level* L = Make_Level_At(
//          &Stepper_Executor, spec, LEVEL_FLAG_TRAMPOLINE_KEEPALIVE
//      );
//      Push_Level_Erase_Out_If_State_0(result, L);
//      panic ("This throws a level to the trampoline where result is dead");
//
// Simply put, when the Trampoline gets control after a longjmp() or throw, the
// Level's L->out pointer will be corrupt...the stack-declared result is gone.
//
// Instead of DECLARE_VALUE, use Stepper_Primed_Value(L).  It takes advantage
// of the fact that there's a cell's worth of spare space which a stepper
// that is not called by Evaluator_Executor() does not use.
//
INLINE Cell* Stepper_Primed_Value_Untracked(Level* L) {
    assert(L->executor == &Stepper_Executor);
    return &L->u.eval.primed;
}

#define Stepper_Primed_Value(L) \
    u_cast(Sink(Value), FORCE_TRACK_0(Stepper_Primed_Value_Untracked(L)))


// If you PUSH() values to the stack, you need to make sure this accrual is
// accounted for if taking multiple eval steps and reusing a stepper.
//
// (This makes you pass in the level just so you can confirm that the level
// you *think* is the top is actually the top, without needing to assert
// that separately yourself.)
//
#define Sync_Toplevel_Baseline_After_Pushes(L) do { \
    assert((L) == TOP_LEVEL); \
    TOP_LEVEL->baseline.stack_base = TOP_INDEX; \
} while (0)


// Does not check for ST_STEPPER_LEVEL_FINISHED, because generalized loops
// may skip items and never actually call the executor.
//
// Note: At one time the trampoline looked for STATE_0 and automatically
// freshened the cell, and the Stepper_Executor() automatically zeroed its
// state on exit.  But that added a branch on every trampoline bounce to
// check the Level's state, and led to erasing cells redundantly.
//
// 1. To stop leaking values between evaluations, it's been the case that the
//    spare and scratch are guaranteed erased on entry to the stepper.  This
//    is not the case with other state bytes.  But since the first thing the
//    stepper does in STATE_0 is overwrite L->scratch for current, tolerate
//    not erasing it (also helps preload for ST_STEPPER_REEVALUATING)
//
INLINE void Reset_Stepper_Erase_Out(Level* L) {
    assert(
        L->executor == &Stepper_Executor
        or L->executor == &Inert_Stepper_Executor
        or L->executor == &Evaluator_Executor
    );

    if (L->baseline.stack_base != TOP_INDEX)
        assert(!"PUSH() without Sync_Toplevel_Baseline_After_Pushes()");

    L->flags.bits &= (~ (
        FLAG_STATE_BYTE(255)  // set state byte to 0
            | EVAL_EXECUTOR_FLAG_OUT_IS_DISCARDABLE  // what else?
    ));
    Erase_Cell(Level_Out(L));
    Erase_Cell(&L->spare);
    dont(Erase_Cell(&L->scratch));  // stepper tolerant of not erasing [1]
}

#define Init_Pushed_Refinement(out,symbol) \
    Add_Cell_Sigil(Init_Word((out), (symbol)), SIGIL_PIN)

INLINE Element* Init_Pushable_Refinement_Bound(
    Sink(Element) out,
    const Symbol* symbol,
    Context* context,
    Index index
){
    Add_Cell_Sigil(Init_Word_Bound(out, symbol, context), SIGIL_PIN);
    CELL_WORD_INDEX_I32(out) = index;
    return out;
}

#define Is_Pushed_Refinement(v)  Is_Pinned_Form_Of(WORD, (v))

INLINE Result(Element*) Refinify_Pushed_Refinement(Element* e) {
    assert(Is_Pushed_Refinement(e));
    return Refinify(Clear_Cell_Sigil(e));
}


// !!! This is a non-stackless invocation of the evaluator to perform one
// evaluation step.  Callsites that use it should be rewritten to yield to
// the trampoline.
//
INLINE bool Eval_Step_Throws(Init(Value) out, Level* L) {
    assert(
        L->executor == &Stepper_Executor
        or L->executor == &Inert_Stepper_Executor
    );

    L->target = out;
    assert(L->baseline.stack_base == TOP_INDEX);

    assert(L == TOP_LEVEL);  // should already be pushed, use core trampoline

    return Trampoline_With_Top_As_Root_Throws();
}


// !!! This is a non-stackless invocation of the evaluator to perform a full
// evaluation.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Any_List_At_Core_Throws(
    Init(Value) out,
    Flags flags,
    const Element* list,
    Context* context
){
    require (
      Level* L = Make_Level_At_Core(
        &Evaluator_Executor,
        list,
        context,
        flags
    ));

    Push_Level(out, L);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(L);
    return threw;
}

#define Eval_Any_List_At_Throws(out,list,binding) \
    Eval_Any_List_At_Core_Throws(out, LEVEL_FLAG_VANISHABLE_VOIDS_ONLY, \
        (list), (binding))


// !!! This is a non-stackless invocation of the evaluator that evaluates a
// single value.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Element_Core_Throws(
    Init(Value) out,
    Flags flags,
    const Element* value,  // e.g. a BLOCK! here would just evaluate to itself!
    Context* context
){
    if (Any_Inert(value)) {
        Copy_Cell(out, value);
        return false;  // fast things that don't need levels (should inline)
    }

    require (
      Feed* feed = Prep_Array_Feed(
        Alloc_Feed(),
        value,  // first--in this case, the only value in the feed...
        EMPTY_ARRAY,  // ...because we're using the empty array after that
        0,  // ...at index 0
        context,
        FEED_MASK_DEFAULT | (value->header.bits & FEED_FLAG_CONST)
    ));
    require (
      Level* L = Make_Level(&Stepper_Executor, feed, flags)
    );

    Push_Level(out, L);
    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(L);

    return threw;
}

#define Eval_Value_Throws(out,value,context) \
    Eval_Element_Core_Throws(out, LEVEL_MASK_NONE, (value), (context))


// !!! This is a non-stackless invocation of the evaluator that evaluates a
// single value.  Callsites that use it should be rewritten to yield to the
// trampoline.
//
INLINE bool Eval_Branch_Throws(
    Value* out,
    const Element* branch
){
    if (not Pushed_Continuation(
        LEVEL_FLAG_FORCE_HEAVY_BRANCH | LEVEL_FLAG_TRAMPOLINE_KEEPALIVE,
        SPECIFIED, branch,
        nullptr
    )){
        return false;
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();
    if (not threw)
        Copy_Cell(out, Level_Out(TOP_LEVEL));

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


// The Frame_Filler() code is shared between SPECIALIZE and APPLY.
//
enum {
    // skip STATE_0 so callers can use it without overlapping
    ST_FRAME_FILLER_MIN = STATE_1,
    ST_FRAME_FILLER_INITIALIZED_ITERATOR,
    ST_FRAME_FILLER_LABELED_EVAL_STEP,
    ST_FRAME_FILLER_UNLABELED_EVAL_STEP,
    ST_FRAME_FILLER_MAX = ST_FRAME_FILLER_UNLABELED_EVAL_STEP
};

#define BOUNCE_FRAME_FILLER_FINISHED  BOUNCE_OKAY
