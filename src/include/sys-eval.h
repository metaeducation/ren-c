//
//  File: %sys-eval.h
//  Summary: {Low-Level Internal Evaluator API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2022 Ren-C Open Source Contributors
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
// The routine that powers a single EVAL or EVALUATE step is Eval_Core().
// It takes one parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack...and fail() is
// written such that a longjmp up to a failure handler above it can run
// safely and clean up even though intermediate stacks have vanished.
//
// Ren-C can run the evaluator across an Array*-style input series based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as Value* to comma-separated input at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! cell in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
//


#if DEBUG && DEBUG_COUNT_TICKS  // <-- THIS IS VERY USEFUL, READ THIS SECTION!
    //
    // The evaluator `tick` should be visible in the C debugger watchlist as a
    // local variable on each evaluator stack level.  So if a fail() happens
    // at a deterministic moment in a run, capture the number from the level
    // of interest and recompile for a breakpoint at that tick.
    //
    // If the tick is AFTER command line processing is done, you can request
    // a tick breakpoint that way with `--breakpoint NNN`
    //
    // The debug build carries ticks many other places.  Series contain the
    // `Series.tick` where they were created, levels have a `Level.tick`,
    // and the DEBUG_TRACK_EXTEND_CELLS switch will double the size of cells
    // so they can carry the tick, file, and line where they were initialized.
    //
    // For custom updating of stored ticks to help debugging some scenarios,
    // see TOUCH_SERIES() and TOUCH_CELL().  Note also that BREAK_NOW() can be
    // called to pause and dump state at any moment.

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


// A "set friendly" antiform is one that allows assignment via SET-WORD!
// without any special considerations.  The allowance of antiform WORD! started
// so that ~true~ and ~false~ could be implemented as antiforms, but a decision
// to also permit the unset state to assign easily was made as well--so that
// a variable could easily be unset with (var: ~).  So this shifted to say
// that all stable antiforms are set friendly.
//
INLINE bool Is_Antiform_Set_Friendly(const Value* v) {
    assert(QUOTE_BYTE(v) == ANTIFORM_0);
    UNUSED(v);
    return true;
}

INLINE void Restart_Evaluator_Level(Level* L) {
    assert(L->executor == &Evaluator_Executor);
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


// This is a very light wrapper over Eval_Core(), which is used with
// operations like ANY or REDUCE that wish to perform several successive
// operations on an array, without creating a new level each time.
//
INLINE bool Eval_Step_Throws(
    Atom* out,
    Level* L
){
    assert(Not_Feed_Flag(L->feed, NO_LOOKAHEAD));

    assert(L->executor == &Evaluator_Executor);

    L->out = out;
    assert(L->baseline.stack_base == TOP_INDEX);

    assert(L == TOP_LEVEL);  // should already be pushed, use core trampoline

    return Trampoline_With_Top_As_Root_Throws();
}


// It should not be necessary to use a sublevel unless there is meaningful
// state which would be overwritten in the parent level.  For the moment,
// that only happens if a function call is in effect -or- if a SET-WORD! or
// SET-PATH! are running with an expiring `current` in effect.
//
INLINE bool Eval_Step_In_Sublevel_Throws(
    Atom* out,
    Level* L,
    Flags flags
){
    Level* sub = Make_Level(L->feed, flags);

    return Trampoline_Throws(out, sub);
}


INLINE bool Reevaluate_In_Sublevel_Throws(
    Atom* out,
    Level* L,
    const Element* reval,
    Flags flags,
    bool enfix
){
    assert(State_Byte_From_Flags(flags) == 0);
    flags |= FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING);

    Level* sub = Make_Level(L->feed, flags);
    Copy_Cell(&sub->u.eval.current, reval);
    sub->u.eval.current_gotten = nullptr;
    sub->u.eval.enfix_reevaluate = enfix ? 'Y' : 'N';

    return Trampoline_Throws(out, sub);
}


INLINE bool Eval_Step_In_Any_Array_At_Throws(
    Atom* out,
    REBLEN *index_out,
    const Cell* any_array,  // Note: legal to have any_array = out
    Specifier* specifier,
    Flags flags
){
    assert(Is_Cell_Erased(out));

    Feed* feed = Make_At_Feed_Core(any_array, specifier);

    if (Is_Feed_At_End(feed)) {
        *index_out = 0xDECAFBAD;  // avoid compiler warning
        return false;
    }

    Level* L = Make_Level(feed, flags);
    Push_Level(out, L);

    if (Trampoline_With_Top_As_Root_Throws()) {
        *index_out = CORRUPT_INDEX;
        Drop_Level(L);
        return true;
    }

    *index_out = Level_Array_Index(L);
    Drop_Level(L);
    return false;
}


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

    Level* L = Make_Level(feed, flags);

    return Trampoline_Throws(out, L);
}

#define Eval_Value_Throws(out,value,specifier) \
    Eval_Value_Core_Throws(out, LEVEL_MASK_NONE, (value), (specifier))
