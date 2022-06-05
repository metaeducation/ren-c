//
//  File: %sys-eval.h
//  Summary: {Low-Level Internal Evaluator API}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// Ren-C can run the evaluator across a REBARR-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as REBVAL* to comma-separated input at the source level.
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as an ACTION! REBVAL in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * The usermode EVALUATE action is able to avoid overwriting the previous
//   value if the final evaluation step has nothing in it.  That's based on
//   the ability exposed here through the "Maybe_Stale" variations of the
//   Eval_XXX() routines.
//
// * The usermode REEVAL function chooses to make `reeval comment "hi"` ~void~
//   rather than to raise an error.  However, the non-"Maybe_Stale" versions
//   of code here have another option...which is to give the result as END.
//   Currently this is what all the Eval_Step() routines which aren't stale
//   preserving do--but Eval_Value_Throws() will error.
//


#if DEBUG_COUNT_TICKS  // <-- THIS IS VERY USEFUL, READ THIS SECTION!
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
    // `REBSER.tick` where they were created, frames have a `REBFRM.tick`,
    // and the DEBUG_TRACK_EXTEND_CELLS switch will double the size of cells
    // so they can carry the tick, file, and line where they were initialized.
    // (Even without TRACK_EXTEND, cells that don't have their EXTRA() field
    // in use carry the tick--it's in end cells, nulls, blanks, and trash.)
    //
    // For custom updating of stored ticks to help debugging some scenarios,
    // see TOUCH_SERIES() and TOUCH_CELL().  Note also that BREAK_NOW() can be
    // called to pause and dump state at any moment.

    #define UPDATE_TICK_DEBUG(v) \
        do { \
            if (TG_Tick < INTPTR_MAX)  /* avoid rollover (may be 32-bit!) */ \
                ++TG_Tick; \
            if ( \
                TG_Break_At_Tick != 0 and TG_Tick >= TG_Break_At_Tick \
            ){ \
                printf("BREAKING AT TICK %u\n", cast(unsigned int, TG_Tick)); \
                Dump_Frame_Location((v), f); \
                debug_break();  /* see %debug_break.h */ \
                TG_Break_At_Tick = 0; \
            } \
        } while (false)  // macro so that breakpoint is at right stack level!
#else
    #define UPDATE_TICK_DEBUG(v) NOOP
#endif


// The evaluator publishes its internal states in this header file, so that
// a frame can be made with e.g. `FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING)`
// to start in various points of the evaluation process.  When doing so, be
// sure the expected frame variables for that state are initialized.
//
enum {
    ST_EVALUATOR_INITIAL_ENTRY = 0,
    ST_EVALUATOR_LOOKING_AHEAD,
    ST_EVALUATOR_REEVALUATING,
    ST_EVALUATOR_GET_WORD,
    ST_EVALUATOR_META_WORD,
    ST_EVALUATOR_GROUP,
    ST_EVALUATOR_META_GROUP,
    ST_EVALUATOR_PATH_OR_TUPLE,
    ST_EVALUATOR_META_PATH_OR_META_TUPLE
};


// In conditionals, branches that run are not allowed to be void or pure null.
// Both of these states trigger ELSE.  It's also the case that loop bodies
// are not allowed to return void or pure null either (void is reserved for
// the state of never having run the body, and null for BREAK).
//
// This function helps locate the cases that change the branch's actual result
// to either a none (~) isotope for void, or a null isotope for null.  In
// general this is triggered by EVAL_FLAG_BRANCH.
//
inline static REBVAL *Reify_Branch_Out(REBVAL *out) {
    if (Is_Void(out))
        return Init_None(out);

    if (IS_NULLED(out))
        return Init_Null_Isotope(out);

    return out;
}


// Even though ANY_INERT() is a quick test, you can't skip the cost of frame
// processing--due to enfix.  But a feed only looks ahead one unit at a time,
// so advancing the frame past an inert item to find an enfix function means
// you have to enter the frame specially with ST_EVALUATOR_LOOKING_AHEAD.
//
inline static bool Did_Init_Inert_Optimize_Complete(
    REBVAL *out,
    REBFED *feed,
    REBFLGS *flags
){
    assert(SECOND_BYTE(*flags) == 0);  // we might set the STATE_BYTE
    assert(not IS_END(feed->value));  // would be wasting time to call

    if (not ANY_INERT(feed->value))
        return false;  // general case evaluation requires a frame

    Literal_Next_In_Feed(out, feed);

    if (KIND3Q_BYTE_UNCHECKED(feed->value) == REB_WORD) {
        feed->gotten = Lookup_Word(feed->value, FEED_SPECIFIER(feed));
        if (
            not feed->gotten
            or not IS_ACTION(unwrap(feed->gotten))
        ){
            CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);
            return true;  // not action
        }

        if (GET_ACTION_FLAG(VAL_ACTION(unwrap(feed->gotten)), IS_BARRIER)) {
            SET_FEED_FLAG(feed, BARRIER_HIT);
            CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);
            return true;  // is barrier
        }

        if (NOT_ACTION_FLAG(VAL_ACTION(unwrap(feed->gotten)), ENFIXED)) {
            CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);
            return true;  // not enfixed
        }

        REBACT *action = VAL_ACTION(unwrap(feed->gotten));
        if (GET_ACTION_FLAG(action, QUOTES_FIRST)) {
            //
            // Quoting defeats NO_LOOKAHEAD but only on soft quotes.
            //
            if (NOT_FEED_FLAG(feed, NO_LOOKAHEAD)) {
                *flags |=
                    FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
                    | EVAL_FLAG_INERT_OPTIMIZATION;
                return false;
            }

            CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);

            // !!! Cache this test?
            //
            const REBPAR *first = First_Unspecialized_Param(nullptr, action);
            if (VAL_PARAM_CLASS(first) == PARAM_CLASS_SOFT)
                return true;  // don't look back, yield the lookahead

            *flags |=
                FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
                | EVAL_FLAG_INERT_OPTIMIZATION;
            return false;
        }

        if (GET_FEED_FLAG(feed, NO_LOOKAHEAD)) {
            CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);
            return true;   // we're done!
        }

        // ST_EVALUATOR_LOOKING_AHEAD assumes that if the first arg were
        // quoted and skippable, that the skip check has already been done.
        // So we have to do that check here.
        //
        if (GET_ACTION_FLAG(action, SKIPPABLE_FIRST)) {
            const REBPAR *first = First_Unspecialized_Param(nullptr, action);
            if (not TYPE_CHECK(first, KIND3Q_BYTE(out)))
                return true;  // didn't actually want this parameter type
        }

        *flags |=
            FLAG_STATE_BYTE(ST_EVALUATOR_LOOKING_AHEAD)
            | EVAL_FLAG_INERT_OPTIMIZATION;
        return false;  // do normal enfix handling
    }

    if (GET_FEED_FLAG(feed, NO_LOOKAHEAD)) {
        CLEAR_FEED_FLAG(feed, NO_LOOKAHEAD);
        return true;   // we're done!
    }

    return true;
}


// This is a very light wrapper over Eval_Core(), which is used with
// operations like ANY or REDUCE that wish to perform several successive
// operations on an array, without creating a new frame each time.
//
inline static bool Eval_Step_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(NOT_FEED_FLAG(f->feed, NO_LOOKAHEAD));

    f->out = out;
    return Eval_Core_Throws(f);  // should already be pushed
}


// It should not be necessary to use a subframe unless there is meaningful
// state which would be overwritten in the parent frame.  For the moment,
// that only happens if a function call is in effect -or- if a SET-WORD! or
// SET-PATH! are running with an expiring `current` in effect.
//
inline static bool Eval_Step_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *f,
    REBFLGS flags
){
    if (not (flags & EVAL_FLAG_MAYBE_STALE))
        assert(Is_Fresh(out));

    if (Did_Init_Inert_Optimize_Complete(out, f->feed, &flags))
        return false;  // If eval not hooked, ANY-INERT! may not need a frame

    // We need the MAYBE_STALE flag to get Eval_Core() to tolerate the
    // preload of inert data.  So we're responsible for clearing the flag if
    // the caller didn't actually want stale data.

    DECLARE_FRAME (subframe, f->feed, flags);

    Push_Frame(out, subframe);
    bool threw = Eval_Core_Throws(subframe);
    Drop_Frame(subframe);

    return threw;
}


inline static bool Reevaluate_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *f,
    const REBVAL *reval,
    REBFLGS flags,
    bool enfix
){
    assert(SECOND_BYTE(flags) == 0);
    flags |= FLAG_STATE_BYTE(ST_EVALUATOR_REEVALUATING);
    if (enfix)
        flags |= EVAL_FLAG_RUNNING_ENFIX;

    DECLARE_FRAME (subframe, f->feed, flags);
    subframe->u.reval.value = reval;

    Push_Frame(out, subframe);
    bool threw = Eval_Core_Throws(subframe);
    Drop_Frame(subframe);

    return threw;
}


inline static bool Eval_Step_In_Any_Array_At_Throws(
    REBVAL *out,
    REBLEN *index_out,
    const RELVAL *any_array,  // Note: legal to have any_array = out
    REBSPC *specifier,
    REBFLGS flags
){
    assert(Is_Fresh(out));

    DECLARE_FEED_AT_CORE (feed, any_array, specifier);

    if (IS_END(feed->value)) {
        *index_out = 0xDECAFBAD;  // avoid compiler warning
        return false;
    }

    DECLARE_FRAME (f, feed, flags | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(out, f);
    bool threw = Eval_Core_Throws(f);

    if (threw)
        *index_out = TRASHED_INDEX;
    else
        *index_out = FRM_INDEX(f);

    Drop_Frame(f);

    return threw;
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// The evaluator has a common means of fetching values out of both arrays
// and C va_lists via Fetch_Next_In_Frame(), so this code can behave the
// same as if the passed in values came from an array.  However, when values
// originate from C they often have been effectively evaluated already, so
// it's desired that WORD!s or PATH!s not execute as they typically would
// in a block.  So this is often used with EVAL_FLAG_EXPLICIT_EVALUATE.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
inline static bool Eval_Step_In_Va_Throws(
    REBVAL *out,  // must be initialized, won't change if all empty/invisible
    REBFLGS feed_flags,
    const void *p,
    va_list *vaptr,
    REBFLGS eval_flags
){
    DECLARE_VA_FEED (feed, p, vaptr, feed_flags);

    DECLARE_FRAME (
        f,
        feed,
        (eval_flags | EVAL_FLAG_ALLOCATED_FEED) & (~ EVAL_FLAG_BRANCH)
    );

    Push_Frame(out, f);
    bool threw = Eval_Core_Throws(f);

    bool too_many = (eval_flags & EVAL_FLAG_NO_RESIDUE)
        and NOT_END(feed->value);  // feed will be freed in Drop_Frame()

    Drop_Frame(f); // will va_end() if not reified during evaluation

    if (threw)
        return true;

    if (too_many)
        fail (Error_Apply_Too_Many_Raw());

    if (eval_flags & EVAL_FLAG_BRANCH)
        Reify_Branch_Out(out);

    // A va_list-based feed has a lookahead, and also may be spooled due to
    // the GC being triggered.  So the va_list had ownership taken, and it's
    // not possible to return a REBIXO here to "resume the va_list later".
    // That can only be done if the feed is held alive across evaluations.
    //
    return false;
}


inline static bool Eval_Value_Core_Throws(
    REBVAL *out,
    REBFLGS flags,
    const RELVAL *value,  // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
){
    assert(out != value);
    assert(Is_Fresh(out));

    if (ANY_INERT(value)) {
        Derelativize(out, value, specifier);
        return false;  // fast things that don't need frames (should inline)
    }

    // Passes `first` so can't use DECLARE_ARRAY_FEED
    REBFED *feed = Alloc_Feed();
    Prep_Array_Feed(
        feed,
        value,  // first--in this case, the only value in the feed...
        EMPTY_ARRAY,  // ...because we're using the empty array after that
        0,  // ...at index 0
        specifier,
        FEED_MASK_DEFAULT | (value->header.bits & FEED_FLAG_CONST)
    );

    DECLARE_FRAME (f, feed, flags | EVAL_FLAG_ALLOCATED_FEED);

    Push_Frame(out, f);
    bool threw = Eval_Core_Throws(f);
    Drop_Frame(f);

    return threw;
}

#define Eval_Value_Throws(out,value,specifier) \
    Eval_Value_Core_Throws(RESET(out), EVAL_MASK_DEFAULT, (value), (specifier))
