//
//  File: %sys-do.h
//  Summary: {Evaluator Helper Functions and Macros}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The primary routine that performs DO and DO/NEXT is called Do_Core().  It
// takes a single parameter which holds the running state of the evaluator.
// This state may be allocated on the C variable stack:  Do_Core() is
// written such that a longjmp up to a failure handler above it can run
// safely and clean up even though intermediate stacks have vanished.
//
// Ren-C can run the evaluator across a REBARR-style series of input based on
// index.  It can also enumerate through C's `va_list`, providing the ability
// to pass pointers as REBVAL* to comma-separated input at the source level.
// (Someday it may fetch values from a standard C array of REBVAL[] as well.) 
//
// To provide even greater flexibility, it allows the very first element's
// pointer in an evaluation to come from an arbitrary source.  It doesn't
// have to be resident in the same sequence from which ensuing values are
// pulled, allowing a free head value (such as a FUNCTION! REBVAL in a local
// C variable) to be evaluated in combination from another source (like a
// va_list or series representing the arguments.)  This avoids the cost and
// complexity of allocating a series to combine the values together.
//
// These features alone would not cover the case when REBVAL pointers that
// are originating with C source were intended to be supplied to a function
// with no evaluation.  In R3-Alpha, the only way in an evaluative context
// to suppress such evaluations would be by adding elements (such as QUOTE).
// Besides the cost and labor of inserting these, the risk is that the
// intended functions to be called without evaluation, if they quoted
// arguments would then receive the QUOTE instead of the arguments.
//
// The problem was solved by adding a feature to the evaluator which was
// also opened up as a new privileged native called EVAL.  EVAL's refinements
// completely encompass evaluation possibilities in R3-Alpha, but it was also
// necessary to consider cases where a value was intended to be provided
// *without* evaluation.  This introduced EVAL/ONLY.
//


// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The SPORADICALLY() macro uses this to allow flipping
// between different behaviors in debug builds--usually to run the release
// behavior some of the time, and the debug behavior some of the time.  This
// exercises the release code path even when doing a debug build.
//
#ifdef NDEBUG
    #define SPORADICALLY(modulus) \
        FALSE
#else
    #define SPORADICALLY(modulus) \
        (TG_Do_Count % modulus == 0)
#endif

inline static REBOOL IS_QUOTABLY_SOFT(const RELVAL *v) {
    return LOGICAL(IS_GROUP(v) || IS_GET_WORD(v) || IS_GET_PATH(v));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Do_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (f), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each DO/NEXT step.
//
// Like higher level APIs that move through the input series, this low-level
// API can move at full DO/NEXT intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after a DO/NEXT on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// f->value with a valid "fetched" REBVAL*.  END is not valid input, so
// callers beginning a Do_To_End must pre-check that condition themselves
// before calling Do_Core.  And if an operation sets the c->index to END_FLAG
// then that must be checked--it's not legal to call more operations on a
// call frame after a fetch reports the end.
//
// Operations are:
//
// Fetch_Next_In_Frame()
//
//      Retrieve next pointer for examination to f->value.  The previous
//      f->value pointer is overwritten.  (No REBVAL bits are moved by
//      this operation, only the 'currently processing' pointer reassigned.)
//      f->value may become an END marker...test with IS_END()
//
// Do_Next_In_Frame_Throws()
//
//      Executes the already-fetched pointer, consuming as much of the input
//      as necessary to complete a /NEXT (or failing with an error).  This
//      writes the computed REBVAL into a destination location.  After the
//      operation, the next f->value pointer will already be fetched and
//      waiting for examination or use.  The returned value may be THROWN(),
//      and IS_END(f->value) may be true after the operation.
//
// Quote_Next_In_Frame()
//
//      This operation is fairly trivial in the sense that it just assigns
//      the REBVAL bits pointed to by the current value to the destination
//      cell.  Then it does a simple fetch.  The main reason for making an
//      operation vs just having callers do the two steps is to monitor
//      when some of the input has been "consumed" vs. merely fetched.
//
// This is not intending to be a "published" API of Rebol/Ren-C.  But the
// privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.
//

inline static void Push_Frame_Core(REBFRM *f)
{
    // All calls to a Do_Core() are assumed to happen at the same C stack
    // level for a pushed frame (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Do_Next_In_Frame_Throws() for `reduce [a | b | ... | z]`.
    //
    if (C_STACK_OVERFLOWING(&f))
        Trap_Stack_Overflow();

    Prep_Global_Cell(&f->cell);

    f->prior = TG_Frame_Stack;
    TG_Frame_Stack = f;
    if (NOT(f->flags.bits & DO_FLAG_VA_LIST)) {
        if (Get_Ser_Info(f->source.array, SERIES_INFO_RUNNING))
            NOOP; // already temp-locked
        else {
            Set_Ser_Info(f->source.array, SERIES_INFO_RUNNING);
            f->flags.bits |= DO_FLAG_TOOK_FRAME_LOCK;
        }
    }
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->index; // note this is garbage if DO_FLAG_VA_LIST
}

inline static void Drop_Frame_Core(REBFRM *f) {
    if (f->flags.bits & DO_FLAG_TOOK_FRAME_LOCK) {
        assert(Get_Ser_Info(f->source.array, SERIES_INFO_RUNNING));
        Clear_Ser_Info(f->source.array, SERIES_INFO_RUNNING);
    }
    assert(TG_Frame_Stack == f);
    TG_Frame_Stack = f->prior;
}


//
// Code that walks across Rebol arrays and performs evaluations must consider
// that arbitrary user code may disrupt the array being enumerated.  If the
// array is to expand, it might have a different data pointer entirely.
//

inline static void Push_Frame_At(
    REBFRM *f,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier
) {
    SET_FRAME_VALUE(f, ARR_AT(array, index));
    f->source.array = array;

    Init_Endlike_Header(&f->flags, 0);

    f->gotten = END; // tells ET_WORD and ET_GET_WORD they must do a get
    f->index = index + 1;
    f->specifier = specifier;
    f->pending = NULL;

    // The goal of pushing a frame is to reuse it for several sequential
    // operations, when not using DO_FLAG_TO_END.  This is found in operations
    // like ANY and ALL, or anything that needs to do additional processing
    // beyond a plain DO.  Each time those operations run, they can set the
    // output to a new location, and Do_Next_In_Frame_Throws() will call into
    // Do_Core() and properly configure the eval_type.
    //
    // But to make the frame safe for Recycle() in-between the calls to
    // Do_Next_In_Frame_Throws(), the eval_type and output cannot be left as
    // uninitialized bits.  So start with an unwritable END, and then
    // each evaluation will canonize the eval_type to REB_0 in-between.
    // (Do_Core() does not do this, but the wrappers that need it do.)
    //
    f->eval_type = REB_0;
    f->out = m_cast(REBVAL*, END);

    Push_Frame_Core(f);
}

inline static void Push_Frame(REBFRM *f, const REBVAL *v)
{
    Push_Frame_At(f, VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v));
}

inline static void Drop_Frame(REBFRM *f)
{
    assert(f->eval_type == REB_0);
    Drop_Frame_Core(f);
}


#define VA_LIST_PENDING \
    cast(const RELVAL*, &PG_Va_List_Pending)


//
// Fetch_Next_In_Frame() (see notes above)
//
// This routine is optimized assuming the common case is that values are
// being read out of an array.  Whether to read out of a C va_list or to use
// a "virtual" next value (e.g. an old value saved by EVAL) are both indicated
// by f->pending, hence a NULL test of that can be executed quickly.
//
inline static void Fetch_Next_In_Frame(REBFRM *f) {
    //
    // If f->value is pointing to f->cell, it's possible that it may wind up
    // with an END in it between fetches if f->cell gets reused (as in when
    // arguments are pushed for a function)
    //
    assert(NOT_END(f->value) || f->value == &f->cell);

    assert(f->gotten == END); // we'd be invalidating it!

    if (f->pending == NULL) {
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, f->index));
        ++f->index;
    }
    else if (f->pending == VA_LIST_PENDING) {
        SET_FRAME_VALUE(f, va_arg(*f->source.vaptr, const REBVAL*));
        assert(
            IS_END(f->value)
            || (IS_VOID(f->value) && (f->flags.bits & DO_FLAG_NO_ARGS_EVALUATE))
            || !IS_RELATIVE(f->value)
        );
        assert(NOT(IN_DATA_STACK_DEBUG(f->value)));
    }
    else {
        SET_FRAME_VALUE(f, f->pending);
        if (f->flags.bits & DO_FLAG_VA_LIST)
            f->pending = VA_LIST_PENDING;
        else
            f->pending = NULL;
    }
}


// This is a very light wrapper over Do_Core(), which is used with
// Push_Frame_At() for operations like ANY or REDUCE that wish to perform
// several successive operations on an array, without creating a new frame
// each time.
//
inline static REBOOL Do_Next_In_Frame_Throws(
    REBVAL *out,
    REBFRM *f
){
    assert(f->eval_type == REB_0); // see notes in Push_Frame_At()
    assert(NOT(f->flags.bits & DO_FLAG_TO_END));

    Init_End(out);
    f->out = out;
    Do_Core(f); // should already be pushed

    // Since Do_Core() currently makes no guarantees about the state of
    // f->eval_type when an operation is over, restore it to a benign REB_0
    // so that a GC between calls to Do_Next_In_Frame_Throws() doesn't think
    // it has to protect the frame as another running type.
    //
    f->eval_type = REB_0;
    return THROWN(out);
}


// Slightly heavier wrapper over Do_Core() than Do_Next_In_Frame_Throws().
// It also reuses the frame...but has to clear and restore the frame's
// flags.  It is currently used only by SET-WORD! and SET-PATH!.
//
// Note: Consider pathological case `x: eval quote y: eval eval quote z: ...`
// This can be done without making a new frame, but the eval cell which holds
// the SET-WORD! needs to be put back in place before returning, so that the
// set knows where to write.  The caller handles this with the data stack.
//
// !!! Review how much cheaper this actually is than making a new frame.
//
inline static REBOOL Do_Next_Mid_Frame_Throws(REBFRM *f) {
    assert(f->eval_type == REB_SET_WORD || f->eval_type == REB_SET_PATH);

    REBFLGS prior_flags = f->flags.bits;
    Init_Endlike_Header(&f->flags, DO_FLAG_NORMAL); // e.g. no DO_FLAG_TO_END

    REBDSP prior_dsp_orig = f->dsp_orig;
#if !defined(NDEBUG)
    assert(f->state.dsp == f->dsp_orig);
#endif

    Init_End(f->out);
    Do_Core(f); // should already be pushed

    // The & on the following line is purposeful.  See Init_Endlike_Header.
    //
    (&f->flags)->bits = prior_flags; // e.g. restore DO_FLAG_TO_END
    
    f->dsp_orig = prior_dsp_orig;
#if !defined(NDEBUG)
    f->state.dsp = prior_dsp_orig;
#endif

    // Note: f->eval_type will have changed, but it should not matter to
    // REB_SET_WORD or REB_SET_PATH, which will either continue executing
    // the frame and fetch a new eval_type (if DO_FLAG_TO_END) else return
    // with no guarantee about f->eval_type.

    return THROWN(f->out);
}

//
// !!! This operation used to provide some optimization beyond setting up
// a frame for a nested Do_Core().  It would take simpler cases which could
// be done without a nested frame and hand them back more immediately, and
// if it found it couldn't do an optimization then the work done in any
// word fetches could be reused by keeping the fetch result in `f->gotten`
//
// Checking for whether an optimization would be legal or not was complex,
// as even something inert like `1` cannot be evaluated into a slot as `1`
// unless one is sure that there isn't an ensuing `+` or other enfixed
// operation.  Hence, complex evaluator logic had to be reproduced here
// and second-guessed, often falling through to no optimization.
//
// Over time as the evaluator got more complicated, the redundant work and
// conditional code paths showed a slight *slowdown* over just having an
// inline straight-line function that built a frame and recursed Do_Core().
// Future investigation could attack the problem again and see if there is
// any common case that actually offered an advantage to optimize for here.
//
inline static REBOOL Do_Next_In_Subframe_Throws(
    REBVAL *out,
    REBFRM *parent,
    REBUPT flags
){
    // It should not be necessary to use a subframe unless there is meaningful
    // state which would be overwritten in the parent frame.  For the moment,
    // that only happens if a function call is in effect.  Otherwise, it is
    // more efficient to use Do_Next_In_Frame_Throws().
    //
    // !!! Note: It is currently the case that SET-WORD! and SET-PATH! also
    // generate a new frame, in order that lookback quoting can find them.
    // This method is being reviewed in order to generalize it, hopefully
    // saving on frame creations in the process.
    //
    assert(
        parent->eval_type == REB_FUNCTION
        || parent->eval_type == REB_SET_WORD
        || parent->eval_type == REB_SET_PATH
    );

    REBFRM child_frame;
    REBFRM *child = &child_frame;

    child->gotten = parent->gotten;

    Init_End(out);
    child->out = out;

    child->source = parent->source;
    SET_FRAME_VALUE(child, parent->value);
    child->index = parent->index;
    child->specifier = parent->specifier;
    Init_Endlike_Header(&child->flags, flags);
    child->pending = parent->pending;

    Push_Frame_Core(child);
    Do_Core(child);
    Drop_Frame_Core(child);

    // !!! `print 1 + 2 <| print 1 + 7` wishes to print 3 and then 8, rather
    // than print 8 and then evaluate...(is that good?)

    assert(
        (child->flags.bits & DO_FLAG_VA_LIST)
        || parent->index != child->index
        || THROWN(out)
    );
    parent->pending = child->pending;
    SET_FRAME_VALUE(parent, child->value);
    parent->index = child->index;
    parent->gotten = child->gotten;

    return THROWN(out);
}


inline static void Quote_Next_In_Frame(REBVAL *dest, REBFRM *f) {
    Derelativize(dest, f->value, f->specifier);
    Set_Val_Flag(dest, VALUE_FLAG_UNEVALUATED);
    f->gotten = END;
    Fetch_Next_In_Frame(f);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BASIC API: DO_NEXT_MAY_THROW and DO_ARRAY_THROWS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This is a wrapper for a single evaluation.  If one is planning to do
// multiple evaluations, it is not as efficient as creating a frame and then
// doing `Do_Next_In_Frame_Throws()` calls into it.
//
// DO_NEXT_MAY_THROW takes in an array and a REBCNT offset into that array
// of where to execute.  Although the return value is a REBCNT, it is *NOT*
// always a series index!!!  It may return END_FLAG, THROWN_FLAG, VA_LIST_FLAG
//
// Do_Any_Array_At_Throws is another helper for the frequent case where one
// has a BLOCK! or a GROUP! REBVAL at an index which already indicates the
// point where execution is to start.
//
// (The "Throws" name is because it's expected to usually be used in an
// 'if' statement.  It cues you into realizing that it returns TRUE if a
// THROW interrupts this current DO_BLOCK execution--not asking about a
// "THROWN" that happened as part of a prior statement.)
//
// If it returns FALSE, then the DO completed successfully to end of input
// without a throw...and the output contains the last value evaluated in the
// block (empty blocks give void).  If it returns TRUE then it will be the
// THROWN() value.
//
inline static REBIXO DO_NEXT_MAY_THROW(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier
){
    REBFRM frame;
    Prep_Global_Cell(&frame.cell);
    REBFRM *f = &frame;

    SET_FRAME_VALUE(f, ARR_AT(array, index));
    if (IS_END(f->value)) {
        Init_Void(out);
        return END_FLAG;
    }

    f->source.array = array;
    f->specifier = specifier;
    f->index = index + 1;

    Init_Endlike_Header(&f->flags, DO_FLAG_NORMAL);

    f->pending = NULL;
    f->gotten = END;

    Init_End(out);
    f->out = out;

    Push_Frame_Core(f);    
    Do_Core(f);
    Drop_Frame_Core(f); // Drop_Frame() requires f->eval_type to be REB_0

    if (THROWN(out))
        return THROWN_FLAG;

    if (IS_END(f->value))
        return END_FLAG;

    assert(f->index > 1);
    return f->index - 1;
}


// Most common case of evaluator invocation in Rebol: the data lives in an
// array series.  Generic routine takes flags and may act as either a DO
// or a DO/NEXT at the position given.  Option to provide an element that
// may not be resident in the array to kick off the execution.
//
inline static REBIXO Do_Array_At_Core(
    REBVAL *out,
    const RELVAL *opt_first, // must also be relative to specifier if relative
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    REBFLGS flags
) {
    REBFRM f;
    Prep_Global_Cell(&f.cell);

    if (opt_first) {
        SET_FRAME_VALUE(&f, opt_first);
        f.index = index;
    }
    else {
        // Do_Core() requires caller pre-seed first value, always
        //
        SET_FRAME_VALUE(&f, ARR_AT(array, index));
        f.index = index + 1;
    }

    if (IS_END(f.value)) {
        Init_Void(out);
        return END_FLAG;
    }

    Init_End(out);
    f.out = out;

    f.source.array = array;
    f.specifier = specifier;

    Init_Endlike_Header(&f.flags, flags); // see notes on definition

    f.gotten = END; // so ET_WORD and ET_GET_WORD do their own Get_Var
    f.pending = NULL;

    Push_Frame_Core(&f);
    Do_Core(&f);
    Drop_Frame_Core(&f);

    if (THROWN(f.out))
        return THROWN_FLAG; // !!! prohibits recovery from exits

    return IS_END(f.value) ? END_FLAG : f.index;
}


// !!! Not yet implemented--concept is to accept a REBVAL[] array, rather
// than a REBARR of values.
//
// !!! Considerations of this core interface are to see the values as being
// potentially in non-contiguous points in memory, and advanced with some
// skip length between them.  Additionally the idea of some kind of special
// Rebol value or "REB_INSTRUCTION" to say how far to skip is a possibility,
// which would be more general in the sense that it would allow the skip
// distances to be generalized, though this would cost a pointer size
// entity at each point.  The advantage of REB_INSTRUCTION is that only the
// clients using the esoteric ability would be paying anything for it or
// the API complexity, but if an important client like Ren-C++ it might
// be worth the savings.
//
// Note: Functionally it would be possible to assume a 0 index and require
// the caller to bump the value pointer as necessary.  But an index-based
// interface is likely useful to avoid the bookkeeping required for the caller.
//
/*inline static REBIXO Do_Values_At_Core(
    REBVAL *out,
    REBFLGS flags,
    const REBVAL *opt_head,
    const REBVAL values[],
    REBCNT index
) {
    fail (Error_Not_Done_Raw());
}*/


//
//  Reify_Va_To_Array_In_Frame: C
//
// For performance and memory usage reasons, a variadic C function call that
// wants to invoke the evaluator with just a comma-delimited list of REBVAL*
// does not need to make a series to hold them.  Do_Core is written to use
// the va_list traversal as an alternate to DO-ing an ARRAY.
//
// However, va_lists cannot be backtracked once advanced.  So in a debug mode
// it can be helpful to turn all the va_lists into arrays before running
// them, so stack frames can be inspected more meaningfully--both for upcoming
// evaluations and those already past.
//
// A non-debug reason to reify a va_list into an array is if the garbage
// collector needs to see the upcoming values to protect them from GC.  In
// this case it only needs to protect those values that have not yet been
// consumed.
//
// Because items may well have already been consumed from the va_list() that
// can't be gotten back, we put in a marker to help hint at the truncation
// (unless told that it's not truncated, e.g. a debug mode that calls it
// before any items are consumed).
//
inline static void Reify_Va_To_Array_In_Frame(
    REBFRM *f,
    REBOOL truncated
) {
    REBDSP dsp_orig = DSP;

    assert(f->flags.bits & DO_FLAG_VA_LIST);

    if (truncated) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM___OPTIMIZED_OUT__));
    }

    if (NOT_END(f->value)) {
        do {
            DS_PUSH_RELVAL(f->value, f->specifier); // may be void
            Fetch_Next_In_Frame(f);
        } while (NOT_END(f->value));

        if (truncated)
            f->index = 2; // skip the --optimized-out--
        else
            f->index = 1; // position at the start of the extracted values
    }
    else {
        // Leave at the END, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        f->index = 0;
    }

    // We're about to overwrite the va_list pointer in the f->source union,
    // which means there'd be no way to call va_end() on it if we don't do it
    // now.  The Do_Va_Core() routine is aware of this, and doesn't try to do
    // a second va_end() if the conversion has happened here.
    //
    // Note: Fail_Core() also has to do this tie-up of the va_list, since
    // the Do_Core() call in Do_Va_Core() never returns.
    //
    va_end(*f->source.vaptr);

    f->source.array = Pop_Stack_Values(dsp_orig); // may contain voids
    MANAGE_ARRAY(f->source.array); // held alive while frame running
    Set_Ser_Flag(f->source.array, ARRAY_FLAG_VOIDS_LEGAL);

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we locked it.  (This would be more complex if
    // we reused the empty array if dsp_orig == DSP, since someone else
    // might have it locked...not worth the complexity.) 
    //
    Set_Ser_Info(f->source.array, SERIES_INFO_RUNNING);
    f->flags.bits |= DO_FLAG_TOOK_FRAME_LOCK;

    if (truncated)
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, 1)); // skip `--optimized--`
    else
        SET_FRAME_VALUE(f, ARR_HEAD(f->source.array));

    // We clear the DO_FLAG_VA_LIST, assuming that the truncation marker is
    // enough information to record the fact that it was a va_list (revisit
    // if there's another reason to know what it was...)

    f->flags.bits &= ~cast(REBUPT, DO_FLAG_VA_LIST);

    assert(f->pending == VA_LIST_PENDING);
    f->pending = NULL;

    assert(NOT(FRM_IS_VALIST(f))); // no longer a va_list fed frame
}


// (va_list by pointer: http://stackoverflow.com/a/3369762/211160)
//
// Central routine for doing an evaluation of an array of values by calling
// a C function with those parameters (e.g. supplied as arguments, separated
// by commas).  Uses same method to do so as functions like printf() do.
//
// In R3-Alpha this style of invocation was specifically used to call single
// Rebol functions.  It would use a list of REBVAL*s--each of which could
// come from disjoint memory locations and be passed directly with no
// evaluation.  Ren-C replaced this entirely by adapting the evaluator to
// use va_arg() lists for the same behavior as a DO of an ARRAY.
//
// The previously accomplished style of execution with a function which may
// not be in the arglist can be accomplished using `opt_first` to put that
// function into the optional first position.  To instruct the evaluator not
// to do any evaluation on the values supplied as arguments after that
// (corresponding to R3-Alpha's APPLY/ONLY) then DO_FLAG_NO_ARGS_EVALUATE
// should be used--otherwise they will be evaluated normally.
//
// NOTE: Ren-C no longer supports the built-in ability to supply refinements
// positionally, due to the brittleness of this approach (for both system
// and user code).  The `opt_head` value should be made a path with the
// function at the head and the refinements specified there.  Future
// additions could do this more efficiently by allowing the refinement words
// to be pushed directly to the data stack.
//
// !!! C's va_lists are very dangerous, there is no type checking!  The
// C++ build should be able to check this for the callers of this function
// *and* check that you ended properly.  It means this function will need
// two different signatures (and so will each caller of this routine).
//
// Returns THROWN_FLAG, END_FLAG, or VA_LIST_FLAG
//
inline static REBIXO Do_Va_Core(
    REBVAL *out,
    const REBVAL *opt_first,
    va_list *vaptr,
    REBFLGS flags
) {
    REBFRM f;
    Prep_Global_Cell(&f.cell);

    if (opt_first)
        SET_FRAME_VALUE(&f, opt_first); // no specifier, not relative
    else {
        SET_FRAME_VALUE(&f, va_arg(*vaptr, const REBVAL*));
        assert(!IS_RELATIVE(f.value));
    }

    if (IS_END(f.value)) {
        Init_Void(out);
        return END_FLAG;
    }

    Init_End(out);
    f.out = out;

#if !defined(NDEBUG)
    f.index = TRASHED_INDEX;
#endif
    f.source.vaptr = vaptr;
    f.gotten = END; // so ET_WORD and ET_GET_WORD do their own Get_Var
    f.specifier = SPECIFIED; // va_list values MUST be full REBVAL* already
    f.pending = VA_LIST_PENDING;

    Init_Endlike_Header(&f.flags, flags | DO_FLAG_VA_LIST); // see notes

    Push_Frame_Core(&f);
    Do_Core(&f);
    Drop_Frame_Core(&f);

    // Note: While on many platforms va_end() is a no-op, the C standard is
    // clear that it must be called...it's undefined behavior if you skip it:
    //
    // http://stackoverflow.com/a/32259710/211160
    //
    // Yet fail() will longjmp above this stack level, never getting here.  So
    // it is necessary that the call to va_end be done by Fail_Core() *before*
    // that longjmp, by walking the stack list and looking for any va_list
    // frames between the failure point and the trapper.
    //
    // But additionally, a frame may have to be "reified" if a GC runs while
    // this va_list is being processed.  (The reason is that the va_list has
    // to have its values examined to be GC protected, but there's no API to
    // allow the values to be examined for GC and then "rewound" to be fed
    // into evaluation, so they must be stored in an intermediate array.)
    // Reification also does a va_end(), so we don't want to do it again.
    //
    if (FRM_IS_VALIST(&f)) // didn't get reified, so va_end() not called...
        va_end(*vaptr);

    if (THROWN(f.out))
        return THROWN_FLAG; // !!! prohibits recovery from exits

    return IS_END(f.value) ? END_FLAG : VA_LIST_FLAG;
}


// Wrapper around Do_Va_Core which has the actual variadic interface (as
// opposed to taking the `va_list` which has been captured out of the
// variadic interface).
//
inline static REBOOL Do_Va_Throws(REBVAL *out, ...)
{
    va_list va;

    va_start(va, out); // must mention last param before the "..."

    REBIXO indexor = Do_Va_Core(
        out,
        NULL, // opt_first
        &va,
        DO_FLAG_TO_END
    );

    // Note: va_end() is handled by Do_Va_Core (one way or another)

    assert(indexor == THROWN_FLAG || indexor == END_FLAG);
    return LOGICAL(indexor == THROWN_FLAG);
}


// Takes a list of arguments terminated by an end marker and will do something
// similar to R3-Alpha's "apply/only" with a value.  If that value is a
// function, it will be called...if it's a SET-WORD! it will be assigned, etc.
//
// This is equivalent to putting the value at the head of the input and
// then calling EVAL/ONLY on it.  If all the inputs are not consumed, an
// error will be thrown.
//
// The boolean result will be TRUE if an argument eval or the call created
// a THROWN() value, with the thrown value in `out`.
//
inline static REBOOL Apply_Only_Throws(
    REBVAL *out,
    REBOOL fully,
    const REBVAL *applicand,
    ...
) {
    va_list va;
    va_start(va, applicand); // must mention last param before the "..."

    REBIXO indexor = Do_Va_Core(
        out,
        applicand, // opt_first
        &va,
        DO_FLAG_NO_ARGS_EVALUATE | DO_FLAG_NO_LOOKAHEAD
    );

    if (fully && indexor == VA_LIST_FLAG) {
        //
        // Not consuming all the arguments given suggests a problem if `fully`
        // is passed in as TRUE.
        //
        fail (Error_Apply_Too_Many_Raw());
    }

    // Note: va_end() is handled by Do_Va_Core (one way or another)

    assert(
        indexor == THROWN_FLAG
        || indexor == END_FLAG
        || (NOT(fully) && indexor == VA_LIST_FLAG)
    );
    return LOGICAL(indexor == THROWN_FLAG);
}


inline static REBOOL Do_At_Throws(
    REBVAL *out,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier
){
    return LOGICAL(
        THROWN_FLAG == Do_Array_At_Core(
            out,
            NULL,
            array,
            index,
            specifier,
            DO_FLAG_TO_END
        )
    );
}

// Note: It is safe for `out` and `array` to be the same variable.  The
// array and index are extracted, and will be protected from GC by the DO
// state...so it is legal to e.g Do_Any_Array_At_Throws(D_OUT, D_OUT).
//
inline static REBOOL Do_Any_Array_At_Throws(
    REBVAL *out,
    const REBVAL *any_array
){
    return Do_At_Throws(
        out,
        VAL_ARRAY(any_array),
        VAL_INDEX(any_array),
        VAL_SPECIFIER(any_array)
    );
}

// Because Do_Core can seed with a single value, we seed with our value and
// an EMPTY_ARRAY.  Revisit if there's a "best" dispatcher.  Note this is
// an EVAL and not a DO...hence if you pass it a block, then the block will
// just evaluate to itself!
//
inline static REBOOL Eval_Value_Core_Throws(
    REBVAL *out,
    const RELVAL *value,
    REBSPC *specifier
){
    return LOGICAL(
        THROWN_FLAG == Do_Array_At_Core(
            out,
            value,
            EMPTY_ARRAY,
            0,
            specifier,
            DO_FLAG_TO_END
        )
    );
}

#define Eval_Value_Throws(out,value) \
    Eval_Value_Core_Throws((out), (value), SPECIFIED)


// When running a "branch" of code in conditional execution, Ren-C allows the
// use of single-arity functions.
//
//    >> foo: does [print "Hello"]
//    >> if true :foo
//    Hello
//
// This was not allowed in R3-Alpha, and given the fact that you could write
// that as `if true [foo]` the added flexibility may not be necessary.  But
// there is a feature in Ren-C which allows you to have a branch evaluate to
// itself literally, e.g.
//
//    [print "doesn't print"] = case/only [true [print "doesn't print"]]
//
// In order to capture all the "branch-like" decisions into one place, the
// shared decision of what to allow or not allow is captured in this one
// inline function, used by conditional and loop constructs instead of a
// plain DO.
//
inline static REBOOL Run_Branch_Throws(
    REBVAL *out,
    const REBVAL *branch,
    REBOOL only
) {
    assert(branch != out); // !!! review, CASE can perhaps do better...

    if (only) {
        Move_Value(out, branch);
    }
    else if (IS_BLOCK(branch)) {
        if (Do_Any_Array_At_Throws(out, branch))
            return TRUE;
    }
    else if (IS_FUNCTION(branch)) {
        //
        // The function is allowed to be arity-0 only.
        //
        // !!! Might it be interesting if arity-1 functions were also allowed,
        // by passing in the condition evaluation that caused the branch?
        //
        //     >> if 1 + 2 func [x] [print x]
        //     3
        //
        // This could look even better with lambdas:
        //
        //     >> if 1 + 2 (x -> print x)
        //     3
        //
        // To implement that feature, callers would have to pass in the
        // condition, then the argument would be included in the apply, with
        // `fully` not enforced...so the function could either consume the
        // argument or not.  Review.
        //
        const REBOOL fully = TRUE;
        if (Apply_Only_Throws(out, fully, branch, END))
            return TRUE;
    }
    else
        Move_Value(out, branch); // it's not code -- nothing to run

    return FALSE;
}


enum {
    REDUCE_FLAG_INTO = 1 << 0,
    REDUCE_FLAG_DROP_BARS = 1 << 1,
    REDUCE_FLAG_KEEP_BARS = 1 << 2
};
