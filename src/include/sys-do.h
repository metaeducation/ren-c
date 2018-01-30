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


// !!! Find a better place for this!
//
inline static REBOOL IS_QUOTABLY_SOFT(const RELVAL *v) {
    return LOGICAL(IS_GROUP(v) || IS_GET_WORD(v) || IS_GET_PATH(v));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  TICK-RELATED FUNCTIONS <== **THESE ARE VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Each iteration of DO bumps a global count, that in deterministic repro
// cases can be very helpful in identifying the "tick" where certain problems
// are occurring.  The debug build pokes this ticks lots of places--into
// value cells when they are formatted, into series when they are allocated
// or freed, or into stack frames each time they perform a new operation.
//
// If you have a reproducible tick count, then BREAK_ON_TICK() is useful,
// since you can put it anywhere.  It's a macro so that it doesn't make a
// new C stack frame, leaving your debugger right at the callsite.
//
// The SPORADICALLY() macro uses the count to allow flipping between different
// behaviors in debug builds--usually to run the release behavior some of the
// time, and the debug behavior some of the time.  This exercises the release
// code path even when doing a debug build.
//

#define BREAK_ON_TICK(tick) \
    if (tick == TG_Tick) { \
        printf("BREAK_ON_TICK at %d\n", tick); /* double eval of tick! */ \
        fflush(stdout); \
        Dump_Frame_Location(NULL, FS_TOP); \
        debug_break(); /* see %debug_break.h */ \
    } \

#ifdef NDEBUG
    #define SPORADICALLY(modulus) \
        FALSE
#else
    #define SPORADICALLY(modulus) \
        (TG_Tick % modulus == 0)
#endif


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
// f->value with a valid "fetched" REBVAL*.
//
// This privileged level of access can be used by natives that feel they can
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
        Fail_Stack_Overflow();

    assert(f->flags.bits & NODE_FLAG_END);
    assert(NOT(f->flags.bits & NODE_FLAG_CELL));

    // Though we can protect the value written into the target pointer 'out'
    // from GC during the course of evaluation, we can't protect the
    // underlying value from relocation.  Technically this would be a problem
    // for any series which might be modified while this call is running, but
    // most notably it applies to the data stack--where output used to always
    // be returned.
    //
    // !!! A non-contiguous data stack which is not a series is a possibility.
    //
#ifdef STRESS_CHECK_DO_OUT_POINTER
    REBNOD *containing = Try_Find_Containing_Node_Debug(f->out);

    if (containing && NOT(containing->header.bits & NODE_FLAG_CELL)) {
        if (GET_SER_FLAG(containing, SERIES_FLAG_FIXED_SIZE)) {
            //
            // Currently it's considered OK to be writing into a fixed size
            // series, for instance the durable portion of a function's
            // arg storage.  It's assumed that the memory will not move
            // during the course of the argument evaluation.
            //
        }
        else {
            printf("Request for ->out location in movable series memory\n");
            panic (containing);
        }
    }
#else
    assert(!IN_DATA_STACK_DEBUG(f->out));
#endif

    // The arguments to functions in their frame are exposed via FRAME!s
    // and through WORD!s.  This means that if you try to do an evaluation
    // directly into one of those argument slots, and run arbitrary code
    // which also *reads* those argument slots...there could be trouble with
    // reading and writing overlapping locations.  So unless a function is
    // in the argument fulfillment stage (before the variables or frame are
    // accessible by user code), it's not legal to write directly into an
    // argument slot.  :-/  Note the availability of D_CELL for any functions
    // that have more than one argument, during their run.
    //
#if !defined(NDEBUG)
    REBFRM *ftemp = FS_TOP;
    for (; ftemp != NULL; ftemp = ftemp->prior) {
        if (NOT(Is_Function_Frame(ftemp)))
            continue;
        if (Is_Function_Frame_Fulfilling(ftemp))
            continue;
        assert(
            f->out < ftemp->args_head ||
            f->out >= ftemp->args_head + FRM_NUM_ARGS(ftemp)
        );
    }
#endif

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // REB_FUNCTION but doesn't have valid args/state.  The phase is a
    // good choice because it is only affected by the function call case,
    // see Is_Function_Frame_Fulfilling().
    //
    f->phase = NULL;

    TRASH_POINTER_IF_DEBUG(f->opt_label);

#if !defined(NDEBUG)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);

    // !!! TBD: the relevant file/line update when f->source.array changes
    //
    f->file = FRM_FILE_UTF8(f);
    f->line = FRM_LINE(f);
#endif

    f->prior = TG_Frame_Stack;
    TG_Frame_Stack = f;

    // If the source for the frame is a REBARR*, then we want to temporarily
    // lock that array against mutations.  
    //
    if (FRM_IS_VALIST(f)) {
        //
        // There's nothing to put a hold on while it's a va_list-based frame.
        // But a GC might occur and "Reify" it, in which case the array
        // which is created will have a hold put on it to be released when
        // the frame is finished.
    }
    else {
        if (GET_SER_INFO(f->source.array, SERIES_INFO_HOLD))
            NOOP; // already temp-locked
        else {
            SET_SER_INFO(f->source.array, SERIES_INFO_HOLD);
            f->flags.bits |= DO_FLAG_TOOK_FRAME_HOLD;
        }
    }

#if !defined(NDEBUG)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
#endif
}

inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->source.index; // this is garbage if DO_FLAG_VA_LIST
}

inline static void Abort_Frame_Core(REBFRM *f) {
    if (f->flags.bits & DO_FLAG_TOOK_FRAME_HOLD) {
        //
        // The frame was either never variadic, or it was but got spooled into
        // an array by Reify_Va_To_Array_In_Frame()
        //
        assert(NOT(FRM_IS_VALIST(f)));

        assert(GET_SER_INFO(f->source.array, SERIES_INFO_HOLD));
        CLEAR_SER_INFO(f->source.array, SERIES_INFO_HOLD);
    }
    else if (FRM_IS_VALIST(f)) {
        //
        // Note: While on many platforms va_end() is a no-op, the C standard
        // is clear it must be called...it's undefined behavior to skip it:
        //
        // http://stackoverflow.com/a/32259710/211160
        //
        // !!! If rebDo() allows transient elements to be put into the va_list
        // with the expectation that they will be cleaned up by the end of
        // the call, any remaining entries in the list would have to be
        // fetched and processed here.
        //
        va_end(*f->source.vaptr);
    }

    assert(TG_Frame_Stack == f);
    TG_Frame_Stack = f->prior;
}

inline static void Drop_Frame_Core(REBFRM *f) {
#if !defined(NDEBUG)
    //
    // To keep from slowing down the debug build too much, Do_Core() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    ASSERT_STATE_BALANCED(&f->state);
#endif
    Abort_Frame_Core(f);
}


inline static void Push_Frame_At(
    REBFRM *f,
    REBARR *array,
    REBCNT index,
    REBSPC *specifier,
    REBUPT flags
){
    Init_Endlike_Header(&f->flags, flags);

    f->gotten = END; // tells ET_WORD and ET_GET_WORD they must do a get
    SET_FRAME_VALUE(f, ARR_AT(array, index));

    f->source.vaptr = NULL;
    f->source.array = array;
    f->source.index = index + 1;
    f->source.pending = f->value + 1;

    f->specifier = specifier;

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
    Push_Frame_At(
        f, VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), DO_FLAG_NORMAL
    );
}

inline static void Drop_Frame(REBFRM *f)
{
    assert(f->eval_type == REB_0);
    Drop_Frame_Core(f);
}

// The experimental native DO-ALL tries to recover a frame that experienced
// a FAIL.  This captures what things one needs to reset to make that work.
//
inline static void Recover_Frame(REBFRM *f)
{
    assert(f == FS_TOP);
    f->eval_type = REB_0;
    f->phase = NULL;

    TRASH_POINTER_IF_DEBUG(f->opt_label);
#if !defined(NDEBUG)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
#endif
}


// Ordinary Rebol internals deal with REBVAL* that are resident in arrays.
// But a va_list can contain UTF-8 string components or special instructions
// that are other Detect_Rebol_Pointer() types.  Anyone who wants to set or
// preload a frame's state for a va_list has to do this detection, so this
// code has to be factored out (because a C va_list cannot have its first
// parameter in the variadic).
//
inline static void Set_Frame_Detected_Fetch(REBFRM *f, const void *p)
{
detect_again:

#if !defined(NDEBUG)
    if (p == NULL)
        panic ("NULL should not be used to terminate C va_lists, use END");
#endif

    switch (Detect_Rebol_Pointer(p)) {
    case DETECTED_AS_UTF8: {
        SCAN_STATE ss;
        const REBUPT start_line = 1;
        Init_Va_Scan_State_Core(
            &ss,
            STR("sys-do.h"),
            start_line,
            cast(const REBYTE*, p),
            f->source.vaptr
        );

        // This scan may advance the va_list across several units,
        // incorporating REBVALs into the scanned array as it goes.  It could
        // stop when it completed just some tokens, e.g. it doesn't need to
        // include `b` in the array here, it could stop at "]":
        //
        //     rebDo("[", a, "]", b, END);
        //
        // But if scanning is to be done anyway to produce a REBARR*, it would
        // be wasteful to create individual arrays for each string section,
        // not to mention wasteful to repeat binding for each string.
        //
        // !!! There may be special "binding instructions" or otherwise, as
        // well.  This is an area to be investigated, and tight integration
        // between this code and the scanner may be needed.
        //
        f->source.array = Scan_Array(&ss, 0);

        if (IS_END(ARR_HEAD(f->source.array))) {
            //
            // This happens when somone says rebDo(..., "", ...) or similar,
            // and gets an empty array from a string scan.  It's not legal
            // to put an END in f->value, and it's unknown if the variadic
            // feed is actually over so as to put NULL... so get another
            // value out of the va_list and keep going.
            //
            p = va_arg(*f->source.vaptr, const void*);
            goto detect_again;
        }

        f->value = ARR_HEAD(f->source.array);
        f->source.pending = f->value + 1; // may be END
        f->source.index = 1;

        panic ("String-based rebDo() not actually ready yet.");
    }

    case DETECTED_AS_SERIES: {
        //
        // Currently the only kind of series we handle here are the
        // result of the rebEval() instruction, which is assumed to only
        // provide a value and then be automatically freed.  (The system
        // exposes EVAL the primitive but not a generalized EVAL bit on
        // values, so this is a hack to make rebDo() slightly more
        // palatable.)
        //
        REBARR *eval = ARR(m_cast(void*, p));
        assert(NOT_SER_INFO(eval, SERIES_INFO_HAS_DYNAMIC));
        assert(GET_VAL_FLAG(ARR_HEAD(eval), VALUE_FLAG_EVAL_FLIP));

        Move_Value(&f->cell, KNOWN(ARR_HEAD(eval)));
        SET_VAL_FLAG(&f->cell, VALUE_FLAG_EVAL_FLIP);
        f->value = &f->cell;
    #if !defined(NDEBUG)
        f->kind = VAL_TYPE(f->value);
    #endif

        // !!! Ideally we would free the array here, but since the free
        // would occur during a Do_Core() it would appear to be happening
        // outside of a checkpoint.  It's an important enough assert to
        // not disable lightly just for this case, so the instructions
        // are managed for now...but the intention is to free them as
        // they are encountered.
        //
        /* Free_Array(eval); */
        break; }

    case DETECTED_AS_FREED_SERIES:
        panic (p);

    case DETECTED_AS_VALUE:
        f->source.array = NULL;
        f->value = cast(const RELVAL*, p); // not END, detected separately
    #if !defined(NDEBUG)
        f->kind = VAL_TYPE(f->value);
    #endif
        assert(
            (
                IS_VOID(f->value)
                && (f->flags.bits & DO_FLAG_EXPLICIT_EVALUATE)
            ) || NOT(IS_RELATIVE(f->value))
        );
        break;
        
    case DETECTED_AS_END: {
        //
        // We're at the end of the variadic input, so this is the end of
        // the line.  va_end() is taken care of by Drop_Frame_Core()
        //
        f->value = NULL;
    #if !defined(NDEBUG)
        f->kind = REB_0;
        TRASH_POINTER_IF_DEBUG(f->source.pending);
    #endif
        break; }

    case DETECTED_AS_TRASH_CELL:
        panic (p);

    default:
        assert(FALSE);
    }
}


//
// Fetch_Next_In_Frame() (see notes above)
//
// Once a va_list is "fetched", it cannot be "un-fetched".  Hence only one
// unit of fetch is done at a time, into f->value.  f->source.pending thus
// must hold a signal that data remains in the va_list and it should be
// consulted further.  That signal is an END marker.
//
// More generally, an END marker in f->source.pending for this routine is a
// signal that the vaptr (if any) should be consulted next.
//
inline static void Fetch_Next_In_Frame(REBFRM *f) {
    assert(FRM_HAS_MORE(f)); // caller should test this first

    if (NOT_END(f->source.pending)) {
        //
        // We assume the ->pending value lives in a source array, and can
        // just be incremented since the array has SERIES_INFO_HOLD while it
        // is being executed hence won't be relocated or modified.  This
        // means the release build doesn't need to call ARR_AT().
        //
        assert(
            f->source.array == NULL // incrementing plain array of REBVAL[]
            || f->source.pending == ARR_AT(f->source.array, f->source.index)
        );

        f->value = f->source.pending;
    #if !defined(NDEBUG)
        f->kind = VAL_TYPE(f->value);
    #endif

        ++f->source.pending; // might be becoming an END marker, here
        ++f->source.index;
    }
    else if (f->source.vaptr == NULL) {
        //
        // We're not processing a C variadic at all, so the first END we hit
        // is the full stop end.
        //
        f->value = NULL;
    #if !defined(NDEBUG)
        f->kind = REB_0;
        TRASH_POINTER_IF_DEBUG(f->source.pending);
    #endif
    }
    else {
        // A variadic can source arbitrary pointers, which can be detected
        // and handled in different ways.  Notably, a UTF-8 string can be
        // differentiated and loaded.
        //
        const void *p = va_arg(*f->source.vaptr, const void*);

    #if !defined(NDEBUG)
        f->source.index = TRASHED_INDEX;
    #endif

        Set_Frame_Detected_Fetch(f, p);
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

    f->out = out;
    (*PG_Do)(f); // should already be pushed

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
inline static REBOOL Do_Next_Mid_Frame_Throws(REBFRM *f, REBFLGS flags) {
    assert(f->eval_type == REB_SET_WORD || f->eval_type == REB_SET_PATH);

    REBFLGS prior_flags = f->flags.bits;
    Init_Endlike_Header(&f->flags, flags);

    REBDSP prior_dsp_orig = f->dsp_orig; // Do_Core() overwrites on entry

    (*PG_Do)(f); // should already be pushed

    // The & on the following line is purposeful.  See Init_Endlike_Header.
    //
    (&f->flags)->bits = prior_flags; // e.g. restore DO_FLAG_TO_END
    
    f->dsp_orig = prior_dsp_orig;

    // Note: f->eval_type will have changed, but it should not matter to
    // REB_SET_WORD or REB_SET_PATH, which will either continue executing
    // the frame and fetch a new eval_type (if DO_FLAG_TO_END) else return
    // with no guarantee about f->eval_type.

    return THROWN(f->out);
}

//
// !!! This operation used to try and optimize some cases without using a
// subframe.  But checking for whether an optimization would be legal or not
// was complex, as even something inert like `1` cannot be evaluated into a
// slot as `1` unless you are sure there's no `+` or other enfixed operation.
// Over time as the evaluator got more complicated, the redundant work and
// conditional code paths showed a slight *slowdown* over just having an
// inline straight-line function that built a frame and recursed Do_Core().
//
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
    // more efficient to call Do_Next_In_Frame_Throws(), or the also lighter
    // Do_Next_In_Mid_Frame_Throws() used by REB_SET_WORD and REB_SET_PATH.
    //
    assert(parent->eval_type == REB_FUNCTION);

    DECLARE_FRAME (child);

    child->gotten = parent->gotten;

    child->out = out;

    // !!! Should they share a source instead of updating?
    child->source = parent->source;

    child->value = parent->value;
#if !defined(NDEBUG)
    child->kind = parent->kind;
#endif

    child->specifier = parent->specifier;
    Init_Endlike_Header(&child->flags, flags);

    Push_Frame_Core(child);
    (*PG_Do)(child);
    Drop_Frame_Core(child);

    assert(
        FRM_IS_VALIST(child)
        || FRM_AT_END(child)
        || parent->source.index != child->source.index
        || THROWN(out)
    );

    // !!! Should they share a source instead of updating?
    parent->source = child->source;

    parent->value = child->value;
#if !defined(NDEBUG)
    parent->kind = child->kind;
#endif

    parent->gotten = child->gotten;

    return THROWN(out);
}


inline static void Quote_Next_In_Frame(REBVAL *dest, REBFRM *f) {
    Derelativize(dest, f->value, f->specifier);
    SET_VAL_FLAG(dest, VALUE_FLAG_UNEVALUATED);
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
    DECLARE_FRAME (f);

    f->gotten = END;
    SET_FRAME_VALUE(f, ARR_AT(array, index));

    if (FRM_AT_END(f)) {
        Init_Void(out);
        return END_FLAG;
    }

    Init_Endlike_Header(&f->flags, DO_FLAG_NORMAL);

    f->source.vaptr = NULL;
    f->source.array = array;
    f->source.index = index + 1;
    f->source.pending = f->value + 1;

    f->specifier = specifier;

    f->out = out;

    Push_Frame_Core(f);
    (*PG_Do)(f);
    Drop_Frame_Core(f); // Drop_Frame() requires f->eval_type to be REB_0

    if (THROWN(out))
        return THROWN_FLAG;

    if (FRM_AT_END(f))
        return END_FLAG;

    assert(f->source.index > 1);
    return f->source.index - 1;
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
){
    DECLARE_FRAME (f);

    f->gotten = END;

    f->source.vaptr = NULL;
    f->source.array = array;
    if (opt_first != NULL) {
        SET_FRAME_VALUE(f, opt_first);
        f->source.index = index;
        f->source.pending = ARR_AT(array, index);
    }
    else {
        SET_FRAME_VALUE(f, ARR_AT(array, index));
        f->source.index = index + 1;
        f->source.pending = f->value + 1;
    }

    if (FRM_AT_END(f)) {
        Init_Void(out);
        return END_FLAG;
    }

    f->out = out;

    f->specifier = specifier;

    Init_Endlike_Header(&f->flags, flags); // see notes on definition

    Push_Frame_Core(f);
    (*PG_Do)(f);
    Drop_Frame_Core(f);

    if (THROWN(f->out))
        return THROWN_FLAG;

    return FRM_AT_END(f) ? END_FLAG : f->source.index;
}


// !!! Not yet implemented--concept is to accept a REBVAL[] array, rather
// than a REBARR of values.
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

    assert(FRM_IS_VALIST(f));

    if (truncated) {
        DS_PUSH_TRASH;
        Init_Word(DS_TOP, Canon(SYM___OPTIMIZED_OUT__));
    }

    if (FRM_HAS_MORE(f)) {
        assert(f->source.pending == END);

        do {
            // may be void.  Preserve VALUE_FLAG_EVAL_FLIP flag.
            DS_PUSH_RELVAL_KEEP_EVAL_FLIP(f->value, f->specifier);
            Fetch_Next_In_Frame(f);
        } while (FRM_HAS_MORE(f));

        if (truncated)
            f->source.index = 2; // skip the --optimized-out--
        else
            f->source.index = 1; // position at start of the extracted values
    }
    else {
        assert(IS_POINTER_TRASH_DEBUG(f->source.pending));

        // Leave at end of frame, but give back the array to serve as
        // notice of the truncation (if it was truncated)
        //
        f->source.index = 0;
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
    f->source.vaptr = NULL;

    // special array...may contain voids and eval flip is kept
    f->source.array = Pop_Stack_Values_Keep_Eval_Flip(dsp_orig);
    MANAGE_ARRAY(f->source.array); // held alive while frame running
    SET_SER_FLAG(f->source.array, ARRAY_FLAG_VOIDS_LEGAL);

    // The array just popped into existence, and it's tied to a running
    // frame...so safe to say we're holding it.  (This would be more complex
    // if we reused the empty array if dsp_orig == DSP, since someone else
    // might have a hold on it...not worth the complexity.) 
    //
    SET_SER_INFO(f->source.array, SERIES_INFO_HOLD);
    f->flags.bits |= DO_FLAG_TOOK_FRAME_HOLD;

    if (truncated)
        SET_FRAME_VALUE(f, ARR_AT(f->source.array, 1)); // skip `--optimized--`
    else
        SET_FRAME_VALUE(f, ARR_HEAD(f->source.array));

    f->source.pending = f->value + 1;

    assert(NOT(FRM_IS_VALIST(f))); // no longer a va_list fed frame
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
// in a block.  So this is often used with DO_FLAG_EXPLICIT_EVALUATE.
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
){
    DECLARE_FRAME (f);

    f->gotten = END; // so REB_WORD and REB_GET_WORD do their own Get_Var

#if !defined(NDEBUG)
    f->source.index = TRASHED_INDEX;
#endif
    f->source.array = NULL;
    f->source.vaptr = vaptr;
    f->source.pending = END; // signal next fetch should come from va_list
    if (opt_first != NULL)
        Set_Frame_Detected_Fetch(f, opt_first);
    else {
    #if !defined(NDEBUG)
        //
        // We need to reuse the logic from Fetch_Next_In_Frame here, but it
        // requires the prior-fetched f->value to be non-NULL in the debug
        // build.  Make something up that the debug build can trace back to
        // here via the value's ->track information if it ever gets used.
        //
        DECLARE_LOCAL (junk);
        Init_Unreadable_Blank(junk);
        f->value = junk;
    #endif
        Fetch_Next_In_Frame(f);
    }

    if (FRM_AT_END(f)) {
        Init_Void(out);
        return END_FLAG;
    }

    f->out = out;

    f->specifier = SPECIFIED; // relative values not allowed in va_lists

    Init_Endlike_Header(&f->flags, flags); // see notes

    Push_Frame_Core(f);
    (*PG_Do)(f);
    Drop_Frame_Core(f); // will va_end() if not reified during evaluation

    if (THROWN(f->out))
        return THROWN_FLAG;

    return FRM_AT_END(f) ? END_FLAG : VA_LIST_FLAG;
}


// Wrapper around Do_Va_Core which has the actual variadic interface (as
// opposed to taking the `va_list` which has been captured out of the
// variadic interface).
//
inline static REBOOL Do_Va_Throws(
    REBVAL *out, // last param before ... mentioned in va_start()
    ...
){
    va_list va;
    va_start(va, out);

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
    const REBVAL *applicand, // last param before ... mentioned in va_start()
    ...
) {
    va_list va;
    va_start(va, applicand);

    DECLARE_LOCAL (applicand_eval);
    Move_Value(applicand_eval, applicand);
    SET_VAL_FLAG(applicand_eval, VALUE_FLAG_EVAL_FLIP);

    REBIXO indexor = Do_Va_Core(
        out,
        applicand_eval, // opt_first
        &va,
        DO_FLAG_EXPLICIT_EVALUATE | DO_FLAG_NO_LOOKAHEAD
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


// When running a "branch" of code in conditional execution, Rebol has
// traditionally executed BLOCK!s.  But Ren-C also executes FUNCTION!s that
// are arity 0 or 1:
//
//     >> foo: does [print "Hello"]
//     >> if true :foo
//     Hello
//
//     >> foo: func [x] [print x]
//     >> if 5 :foo
//     5
//
// When the branch is single-arity, the condition which triggered the branch
// is passed as the argument.  This permits some interesting possibilities in
// chaining.
//
//     >> case [true "a" false "b"] then func [x] [print x] else [print "*"]
//     a
//     >> case [false "a" true "b"] then func [x] [print x] else [print "*"]
//     b
//     >> case [false "a" false "b"] then func [x] [print x] else [print "*"]
//     *
//
// Also, Ren-C does something called "barification", unless the /ONLY
// refinement is used.  This is the process by which a void or BLANK! branch
// result is forced to be BAR! instead, allowing void to be reserved for the
// result when no branch ran, and BLANK! to be reserved by constructs like
// ANY and ALL to signal failure that can be used in a condition.  This gives
// a uniform way of determining whether a branch ran or not (utilized by ELSE,
// THEN, etc.)
//
// Note: Tolerance of non-BLOCK! and non-FUNCTION! branches to act as literal
// values was proven to cause more harm than good.
//
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
inline static REBOOL Run_Branch_Throws(
    REBVAL *out,
    const REBVAL *condition,
    const REBVAL *branch,
    REBOOL only
){
    assert(branch != out); // !!! review, CASE can perhaps do better...
    assert(condition != out); // direct pointer in va_list, also destination

    if (IS_BLOCK(branch)) {
        if (Do_Any_Array_At_Throws(out, branch))
            return TRUE;
    }
    else {
        assert(IS_FUNCTION(branch));

        const REBOOL fully = FALSE; // arity-0 functions can ignore condition
        if (Apply_Only_Throws(out, fully, branch, condition, END))
            return TRUE;
    }

    if (NOT(only) && (IS_VOID(out) || IS_BLANK(out)))
        Init_Bar(out); // "barification", see comment above

    return FALSE;
}


enum {
    REDUCE_FLAG_INTO = 1 << 0,
    REDUCE_FLAG_DROP_BARS = 1 << 1,
    REDUCE_FLAG_KEEP_BARS = 1 << 2
};
