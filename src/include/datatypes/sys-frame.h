//
//  File: %sys-frame.h
//  Summary: {Accessors and Argument Pushers/Poppers for Function Call Frames}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// A single FRAME! can go through multiple phases of evaluation, some of which
// should expose more fields than others.  For instance, when you specialize
// a function that has 10 parameters so it has only 8, then the specialization
// frame should not expose the 2 that have been removed.  It's as if the
// KEYS OF the spec is shorter than the actual length which is used.
//
// Hence, each independent value that holds a frame must remember the function
// whose "view" it represents.  This field is only applicable to frames, and
// so it could be used for something else on other types
//
// Note that the binding on a FRAME! can't be used for this purpose, because
// it's already used to hold the binding of the function it represents.  e.g.
// if you have a definitional return value with a binding, and try to
// MAKE FRAME! on it, the paramlist alone is not enough to remember which
// specific frame that function should exit.
//

// !!! Find a better place for this!
//
inline static bool IS_QUOTABLY_SOFT(const RELVAL *v) {
    return IS_GET_GROUP(v) or IS_GET_WORD(v) or IS_GET_PATH(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


// When Push_Action() happens, it sets f->original, but it's guaranteed to be
// null if an action is not running.  This is tested via a macro because the
// debug build doesn't do any inlining, and it's called often.
//
#define Is_Action_Frame(f) \
    ((f)->original != nullptr)


// While a function frame is fulfilling its arguments, the `f->param` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->param` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
// Some cases in debug code call this all the way up the call stack, and when
// the debug build doesn't inline functions it's best to use as a macro.

#define Is_Action_Frame_Fulfilling_Unchecked(f) \
    NOT_END((f)->param)

inline static bool Is_Action_Frame_Fulfilling(REBFRM *f) {
    assert(Is_Action_Frame(f));
    return Is_Action_Frame_Fulfilling_Unchecked(f);
}


inline static bool FRM_IS_VARIADIC(REBFRM *f) {
    return f->feed->vaptr != nullptr or f->feed->packed != nullptr;
}

inline static const REBARR *FRM_ARRAY(REBFRM *f) {
    assert(IS_END(f->feed->value) or not FRM_IS_VARIADIC(f));
    return f->feed->array;
}

// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `opt_head` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBLEN FRM_INDEX(REBFRM *f) {
    if (IS_END(f->feed->value))
        return ARR_LEN(f->feed->array);

    assert(not FRM_IS_VARIADIC(f));
    return f->feed->index - 1;
}

inline static REBLEN FRM_EXPR_INDEX(REBFRM *f) {
    assert(not FRM_IS_VARIADIC(f));
    return f->expr_index - 1;
}

inline static REBSTR* FRM_FILE(REBFRM *f) { // https://trello.com/c/K3vntyPx
    if (not f->feed->array)
        return nullptr;
    if (NOT_ARRAY_FLAG(f->feed->array, HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return STR(LINK(f->feed->array).custom.node);
}

inline static const char* FRM_FILE_UTF8(REBFRM *f) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(__ANONYMOUS__).
    //
    REBSTR *str = FRM_FILE(f);
    return str ? STR_UTF8(str) : "(anonymous)"; 
}

inline static int FRM_LINE(REBFRM *f) {
    if (not f->feed->array)
        return 0;
    if (NOT_ARRAY_FLAG(f->feed->array, HAS_FILE_LINE_UNMASKED))
        return 0;
    return MISC(SER(f->feed->array)).line;
}

#define FRM_OUT(f) \
    (f)->out


// Note about FRM_NUM_ARGS: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define FRM_NUM_ARGS(f) \
    (cast(REBSER*, (f)->varlist)->content.dynamic.used - 1) // minus rootvar

#define FRM_SPARE(f) \
    cast(REBVAL*, &(f)->spare)

#define FRM_PRIOR(f) \
    ((f)->prior + 0) // prevent assignment via this macro

// The "phase" slot of a FRAME! value is the second node pointer in PAYLOAD().
// If a frame value is non-archetypal, this slot may be occupied by a REBSTR*
// which represents the cached name of the action from which the frame
// was created.  This FRAME! value is archetypal, however...which never holds
// such a cache.  For performance (even in the debug build, where this is
// called *a lot*) this is a macro and is unchecked.
//
#define FRM_PHASE(f) \
    cast(REBACT*, VAL_FRAME_PHASE_OR_LABEL_NODE((f)->rootvar))

#define INIT_FRM_PHASE(f,phase) \
    INIT_VAL_CONTEXT_PHASE((f)->rootvar, (phase))

#define FRM_BINDING(f) \
    EXTRA(Binding, (f)->rootvar).node

inline static const REBSTR *FRM_LABEL(REBFRM *f) {
    assert(Is_Action_Frame(f));
    return f->opt_label;
}

#define FRM_UNDERLYING(f) \
    ACT_UNDERLYING((f)->original)

#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // prevent assignment via this macro


#if !defined(__cplusplus)
    #define STATE_BYTE(f) \
        mutable_SECOND_BYTE((f)->flags)
#else
    inline static REBYTE& STATE_BYTE(REBFRM *f)  // type checks f...
      { return mutable_SECOND_BYTE(f->flags); }  // ...but mutable
#endif

#define FLAG_STATE_BYTE(state) \
    FLAG_SECOND_BYTE(state)


// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define FRM_ARGS_HEAD(f) \
    ((f)->rootvar + 1)

#ifdef NDEBUG
    #define FRM_ARG(f,n) \
        ((f)->rootvar + (n))
#else
    inline static REBVAL *FRM_ARG(REBFRM *f, REBLEN n) {
        assert(n != 0 and n <= FRM_NUM_ARGS(f));
        return f->rootvar + n;  // 1-indexed
    }
#endif


// These shorthands help you when your frame is named "f".  While such macros
// are a bit "evil", they are extremely helpful for code readability.  They
// may be #undef'd if they are causing a problem somewhere.

#define f_value f->feed->value
#define f_specifier f->feed->specifier
#define f_spare FRM_SPARE(f)
#define f_gotten f->feed->gotten
#define f_index FRM_INDEX(f)



inline static REBCTX *Context_For_Frame_May_Manage(REBFRM *f) {
    assert(not Is_Action_Frame_Fulfilling(f));
    SET_SERIES_FLAG(f->varlist, MANAGED);
    return CTX(f->varlist);
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

inline static void Get_Frame_Label_Or_Blank(RELVAL *out, REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        Init_Word(out, f->opt_label); // invoked via WORD! or PATH!
    else
        Init_Blank(out); // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->opt_label != NULL)
        return STR_UTF8(f->opt_label);
    return "[anonymous]";
}


//=//// VARLIST CONSERVATION //////////////////////////////////////////////=//
//
// If a varlist does not become managed over the course of its usage, it is
// put into a list of reusable ones.  You can reuse the series node identity
// (avoiding the call to Alloc_Series_Node()) and also possibly the data
// (avoiding the call to Did_Series_Data_Alloc() and other initialization).
//
// This optimization is not necessarily trivial, because freeing even an
// unmanaged series has cost...in particular with Decay_Series().  Removing
// it and changing to just use `GC_Kill_Series()` degrades performance on
// simple examples like `x: 0 loop 1000000 [x: x + 1]` by at least 20%.
// Broader studies might reveal better approaches--but point is, it does at
// least do *something*.

inline static bool Did_Reuse_Varlist_Of_Unknown_Size(
    REBFRM *f,
    REBLEN size_hint  // !!! Currently ignored, smaller sizes can come back
){
    // !!! At the moment, the reuse is not very intelligent and just picks the
    // last one...which could commonly be wastefully big or too small.  But it
    // is a proof of concept to show an axis for performance work.
    //
    UNUSED(size_hint);

    assert(f->varlist == nullptr);

    if (not TG_Reuse)
        return false;

    f->varlist = TG_Reuse;
    TG_Reuse = LINK(TG_Reuse).reuse;
    f->rootvar = cast(REBVAL*, SER(f->varlist)->content.dynamic.data);
    LINK_KEYSOURCE(f->varlist) = NOD(f);
    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    return true;
}

inline static void Conserve_Varlist(REBARR *varlist)
{
  #if !defined(NDEBUG)
    assert(NOT_SERIES_INFO(varlist, INACCESSIBLE));
    assert(NOT_SERIES_FLAG(varlist, MANAGED));

    RELVAL *rootvar = ARR_HEAD(varlist);
    assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == varlist);
    TRASH_POINTER_IF_DEBUG(PAYLOAD(Any, rootvar).second.node);  // phase
    TRASH_POINTER_IF_DEBUG(EXTRA(Binding, rootvar).node);
  #endif

    LINK(varlist).reuse = TG_Reuse;
    TG_Reuse = varlist;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DO's LOWEST-LEVEL EVALUATOR HOOKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This API is used internally in the implementation of Eval_Core.  It does
// not speak in terms of arrays or indices, it works entirely by setting
// up a call frame (f), and threading that frame's state through successive
// operations, vs. setting it up and disposing it on each EVALUATE step.
//
// Like higher level APIs that move through the input series, this low-level
// API can move at full EVALUATE intervals.  Unlike the higher APIs, the
// possibility exists to move by single elements at a time--regardless of
// if the default evaluation rules would consume larger expressions.  Also
// making it different is the ability to resume after an EVALUATE on value
// sources that aren't random access (such as C's va_arg list).
//
// One invariant of access is that the input may only advance.  Before any
// operations are called, any low-level client must have already seeded
// f->value with a valid "fetched" REBVAL*.
//
// This privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.

inline static void Free_Frame_Internal(REBFRM *f) {
    if (f->varlist and NOT_SERIES_FLAG(f->varlist, MANAGED))
        Conserve_Varlist(f->varlist);
    TRASH_POINTER_IF_DEBUG(f->varlist);

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));

    Free_Node(FRM_POOL, f);
}


inline static void Push_Frame(REBVAL *out, REBFRM *f)
{
    assert(f->feed->value != nullptr);

    // All calls through to Eval_Core() are assumed to happen at the same C
    // stack level for a pushed frame (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Eval_Step_Throws() for `reduce [a | b | ... | z]`.
    //
    // !!! This method is being replaced by "stackless", as there is no
    // reliable platform independent method for detecting stack overflows.
    //
    if (C_STACK_OVERFLOWING(&f)) {
        Free_Frame_Internal(f);  // not in stack, feed + frame wouldn't free
        Fail_Stack_Overflow();
    }

    // Frames are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use null until first eval call if needed
    //
    f->out = out;

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
    REBNOD *containing;
    if (
        did (containing = Try_Find_Containing_Node_Debug(f->out))
        and not (containing->header.bits & NODE_FLAG_CELL)
        and NOT_SERIES_FLAG(containing, DONT_RELOCATE)
    ){
        printf("Request for ->out location in movable series memory\n");
        panic (containing);
    }
  #else
    assert(not IN_DATA_STACK_DEBUG(f->out));
  #endif

  #ifdef DEBUG_EXPIRED_LOOKBACK
    f->stress = nullptr;
  #endif

    // The arguments to functions in their frame are exposed via FRAME!s
    // and through WORD!s.  This means that if you try to do an evaluation
    // directly into one of those argument slots, and run arbitrary code
    // which also *reads* those argument slots...there could be trouble with
    // reading and writing overlapping locations.  So unless a function is
    // in the argument fulfillment stage (before the variables or frame are
    // accessible by user code), it's not legal to write directly into an
    // argument slot.  :-/
    //
  #if !defined(NDEBUG)
    REBFRM *ftemp = FS_TOP;
    for (; ftemp != FS_BOTTOM; ftemp = ftemp->prior) {
        if (not Is_Action_Frame(ftemp))
            continue;
        if (Is_Action_Frame_Fulfilling_Unchecked(ftemp))
            continue;
        if (GET_SERIES_INFO(ftemp->varlist, INACCESSIBLE))
            continue; // Encloser_Dispatcher() reuses args from up stack
        assert(
            f->out < FRM_ARGS_HEAD(ftemp)
            or f->out >= FRM_ARGS_HEAD(ftemp) + FRM_NUM_ARGS(ftemp)
        );
    }
  #endif

    // Some initialized bit pattern is needed to check to see if a
    // function call is actually in progress, or if eval_type is just
    // REB_ACTION but doesn't have valid args/state.  The original action is a
    // good choice because it is only affected by the function call case,
    // see Is_Action_Frame_Fulfilling().
    //
    f->original = nullptr;

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif

  #if !defined(NDEBUG)
    //
    // !!! TBD: the relevant file/line update when f->feed->array changes
    //
    f->file = FRM_FILE_UTF8(f);
    f->line = FRM_LINE(f);
  #endif

    f->prior = TG_Top_Frame;
    TG_Top_Frame = f;

    // If the source for the frame is a REBARR*, then we want to temporarily
    // lock that array against mutations.  
    //
    if (IS_END(f->feed->value)) {  // don't take hold on empty feeds
        assert(IS_POINTER_TRASH_DEBUG(f->feed->pending));
        assert(NOT_EVAL_FLAG(f, TOOK_HOLD));
    }
    else if (FRM_IS_VARIADIC(f)) {
        //
        // There's nothing to put a hold on while it's a va_list-based frame.
        // But a GC might occur and "Reify" it, in which case the array
        // which is created will have a hold put on it to be released when
        // the frame is finished.
        //
        assert(NOT_EVAL_FLAG(f, TOOK_HOLD));
    }
    else {
        if (GET_SERIES_INFO(f->feed->array, HOLD))
            NOOP; // already temp-locked
        else {
            SET_SERIES_INFO(f->feed->array, HOLD);
            SET_EVAL_FLAG(f, TOOK_HOLD);
        }
    }

  #if defined(DEBUG_BALANCE_STATE)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    f->state.dsp = f->dsp_orig;
  #endif

    assert(f->varlist == nullptr);  // Prep_Frame_Core() set to nullptr

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));
    f->alloc_value_list = NOD(f);  // doubly link list, terminates in `f`
}


inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    f->expr_index = f->feed->index; // this is garbage if EVAL_FLAG_VA_LIST
}


#define Literal_Next_In_Frame(out,f) \
    Literal_Next_In_Feed((out), (f)->feed)

inline static void Abort_Frame(REBFRM *f) {
    //
    // If a frame is aborted, then we allow its API handles to leak.
    //
    REBNOD *n = f->alloc_value_list;
    while (n != NOD(f)) {
        REBARR *a = ARR(n);
        n = LINK(n).custom.node;
        TRASH_CELL_IF_DEBUG(ARR_SINGLE(a));
        GC_Kill_Series(SER(a));
    }
    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    // Abort_Frame() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    // 
    if (IS_END(f->feed->value))
        goto pop;

    if (FRM_IS_VARIADIC(f)) {
        assert(NOT_EVAL_FLAG(f, TOOK_HOLD));

        // Aborting valist frames is done by just feeding all the values
        // through until the end.  This is assumed to do any work, such
        // as SINGULAR_FLAG_API_RELEASE, which might be needed on an item.  It
        // also ensures that va_end() is called, which happens when the frame
        // manages to feed to the end.
        //
        // Note: While on many platforms va_end() is a no-op, the C standard
        // is clear it must be called...it's undefined behavior to skip it:
        //
        // http://stackoverflow.com/a/32259710/211160

        // !!! Since we're not actually fetching things to run them, this is
        // overkill.  A lighter sweep of the va_list pointers that did just
        // enough work to handle rebR() releases, and va_end()ing the list
        // would be enough.  But for the moment, it's more important to keep
        // all the logic in one place than to make variadic interrupts
        // any faster...they're usually reified into an array anyway, so
        // the frame processing the array will take the other branch.

        while (NOT_END(f->feed->value))
            Fetch_Next_Forget_Lookback(f);
    }
    else {
        if (GET_EVAL_FLAG(f, TOOK_HOLD)) {
            //
            // The frame was either never variadic, or it was but got spooled
            // into an array by Reify_Va_To_Array_In_Frame()
            //
            assert(GET_SERIES_INFO(f->feed->array, HOLD));
            CLEAR_SERIES_INFO(f->feed->array, HOLD);
            CLEAR_EVAL_FLAG(f, TOOK_HOLD); // !!! needed?
        }
    }

  pop:
    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;

    Free_Frame_Internal(f);
}


inline static void Drop_Frame_Core(REBFRM *f) {
  #ifdef DEBUG_ENSURE_FRAME_EVALUATES
    assert(f->was_eval_called);  // must call evaluator--even on empty array
  #endif

  #if defined(DEBUG_EXPIRED_LOOKBACK)
    free(f->stress);
  #endif

    if (GET_EVAL_FLAG(f, TOOK_HOLD)) {
        assert(GET_SERIES_INFO(f->feed->array, HOLD));
        CLEAR_SERIES_INFO(f->feed->array, HOLD);
        CLEAR_EVAL_FLAG(f, TOOK_HOLD);  // needed?
    }

    assert(TG_Top_Frame == f);

    REBNOD *n = f->alloc_value_list;
    while (n != NOD(f)) {
        REBARR *a = ARR(n);
      #if defined(DEBUG_STDIO_OK)
        printf("API handle was allocated but not freed, panic'ing leak\n");
      #endif
        panic (a);
    }
    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    TG_Top_Frame = f->prior;

    Free_Frame_Internal(f);
}

inline static void Drop_Frame_Unbalanced(REBFRM *f) {
    Drop_Frame_Core(f);
}

inline static void Drop_Frame(REBFRM *f)
{
  #if defined(DEBUG_BALANCE_STATE)
    //
    // To avoid slowing down the debug build a lot, Eval_Core() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    f->state.dsp = DSP; // e.g. Reduce_To_Stack_Throws() doesn't want check
    ASSERT_STATE_BALANCED(&f->state);
  #endif

    assert(DSP == f->dsp_orig); // Drop_Frame_Core() does not check
    Drop_Frame_Unbalanced(f);
}

inline static void Prep_Frame_Core(
    REBFRM *f,
    struct Reb_Feed *feed,
    REBFLGS flags
){
    assert(NOT_FEED_FLAG(feed, BARRIER_HIT));  // couldn't do anything

   if (f == nullptr)  // e.g. a failed allocation
       fail (Error_No_Memory(sizeof(REBFRM)));

    assert(
        (flags & EVAL_MASK_DEFAULT) ==
            (EVAL_FLAG_0_IS_TRUE | EVAL_FLAG_7_IS_TRUE)
    );
    f->flags.bits = flags;

    f->feed = feed;
    Prep_Cell(&f->spare);
    Init_Unreadable_Void(&f->spare);
    f->dsp_orig = DS_Index;
    TRASH_POINTER_IF_DEBUG(f->out);

  #ifdef DEBUG_ENSURE_FRAME_EVALUATES
    f->was_eval_called = false;
  #endif

    f->varlist = nullptr;

    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);
}

#define DECLARE_FRAME(name,feed,flags) \
    REBFRM * name = cast(REBFRM*, Alloc_Node(FRM_POOL)); \
    Prep_Frame_Core(name, feed, flags);

#define DECLARE_FRAME_AT(name,any_array,flags) \
    DECLARE_FEED_AT (name##feed, any_array); \
    DECLARE_FRAME (name, name##feed, flags)

#define DECLARE_END_FRAME(name,flags) \
    DECLARE_FRAME (name, &TG_Frame_Feed_End, flags)


inline static void Begin_Action_Core(
    REBFRM *f,
    const REBSTR *opt_label,
    bool enfix
){
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));

    assert(not f->original);
    f->original = FRM_PHASE(f);

    assert(IS_POINTER_TRASH_DEBUG(f->opt_label)); // only valid w/REB_ACTION
    assert(not opt_label or GET_SERIES_FLAG(opt_label, IS_STRING));
    f->opt_label = opt_label;
  #if defined(DEBUG_FRAME_LABELS) // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    assert(NOT_EVAL_FLAG(f, REQUOTE_NULL));
    f->requotes = 0;

    // There's a current state for the FEED_FLAG_NO_LOOKAHEAD which invisible
    // actions want to put back as it was when the invisible operation ends.
    // (It gets overwritten during the invisible's own argument gathering).
    // Cache it on the varlist and put it back when an R_INVISIBLE result
    // comes back.
    //
    if (GET_ACTION_FLAG(f->original, IS_INVISIBLE)) {
        if (GET_FEED_FLAG(f->feed, NO_LOOKAHEAD)) {
            assert(GET_EVAL_FLAG(f, FULFILLING_ARG));
            CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
            SET_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD);
        }
    }

    if (enfix) {
        SET_EVAL_FLAG(f, RUNNING_ENFIX);  // set for duration of function call
        SET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT);  // only set for first arg

        // All the enfix call sites cleared this flag on the feed, so it was
        // moved into the Begin_Enfix_Action() case.  Note this has to be done
        // *after* the existing flag state has been captured for invisibles.
        //
        CLEAR_FEED_FLAG(f->feed, NO_LOOKAHEAD);
    }
}

#define Begin_Enfix_Action(f,opt_label) \
    Begin_Action_Core((f), (opt_label), true)

#define Begin_Prefix_Action(f,opt_label) \
    Begin_Action_Core((f), (opt_label), false)


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates f->param so the GC knows how
// far it has gotten so as not to see garbage.  APPLY has different handling
// when it has to build the frame for the user to write to before running;
// so Eval_Core only checks the arguments, and does not fulfill them.
//
// If the function is a specialization, then the parameter list of that
// specialization will have *fewer* parameters than the full function would.
// For this reason we push the arguments for the "underlying" function.
// Yet if there are specialized values, they must be filled in from the
// exemplar frame.
//
// Rather than "dig" through layers of functions to find the underlying
// function or the specialization's exemplar frame, those properties are
// cached during the creation process.
//
inline static void Push_Action(
    REBFRM *f,
    REBACT *act,
    REBNOD *binding
){
    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));

    f->param = ACT_PARAMS_HEAD(act); // Specializations hide some params...
    REBLEN num_args = ACT_NUM_PARAMS(act); // ...so see REB_TS_HIDDEN

    REBSER *s;
    if (
        f->varlist  // !!! May be going to point of assuming nullptr
        or Did_Reuse_Varlist_Of_Unknown_Size(f, num_args)  // want `num_args`
    ){
        s = SER(f->varlist);
        if (s->content.dynamic.rest >= num_args + 1 + 1) // +roovar, +end
            goto sufficient_allocation;
            
        // It wasn't big enough for `num_args`, so we free the data.
        // But at least we can reuse the series node.

        //assert(SER_BIAS(s) == 0);
        Free_Unbiased_Series_Data(
            s->content.dynamic.data,
            SER_TOTAL(s)
        );
    }
    else {
        s = Alloc_Series_Node(
            SERIES_MASK_VARLIST
                | SERIES_FLAG_FIXED_SIZE // FRAME!s don't expand ATM
        );
        s->info = Endlike_Header(
            FLAG_WIDE_BYTE_OR_0(0) // signals array, also implicit terminator
                | FLAG_LEN_BYTE_OR_255(255) // signals dynamic
        );
        INIT_LINK_KEYSOURCE(s, NOD(f)); // maps varlist back to f
        MISC_META_NODE(s) = nullptr; // GC will sees this
        f->varlist = ARR(s);
    }

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) {  // +rootvar, +end
        s->info.bits |= SERIES_INFO_INACCESSIBLE;
        GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE
        f->varlist = nullptr;
        fail (Error_No_Memory(sizeof(REBVAL) * (num_args + 1 + 1)));
    }

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    f->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | CELL_FLAG_PROTECTED  // payload/binding tweaked, but not by user
            | CELL_MASK_CONTEXT
            | FLAG_KIND3Q_BYTE(REB_FRAME)
            | FLAG_HEART_BYTE(REB_FRAME);
    TRACK_CELL_IF_DEBUG_EVIL_MACRO(f->rootvar, __FILE__, __LINE__);
    INIT_VAL_CONTEXT_VARLIST(f->rootvar, f->varlist);

  sufficient_allocation:

    INIT_VAL_CONTEXT_PHASE(f->rootvar, act);  // FRM_PHASE() (can be dummy)
    EXTRA(Binding, f->rootvar).node = binding; // FRM_BINDING()

    s->content.dynamic.used = num_args + 1;
    RELVAL *tail = ARR_TAIL(f->varlist);
    tail->header.bits = FLAG_KIND3Q_BYTE(REB_0);  // no NODE_FLAG_CELL
    TRACK_CELL_IF_DEBUG_EVIL_MACRO(tail, __FILE__, __LINE__);

    // Current invariant for all arrays (including fixed size), last cell in
    // the allocation is an end.
    RELVAL *ultimate = ARR_AT(f->varlist, s->content.dynamic.rest - 1);
    ultimate->header = Endlike_Header(0); // unreadable
    TRACK_CELL_IF_DEBUG_EVIL_MACRO(ultimate, __FILE__, __LINE__);

  #if !defined(NDEBUG)
    RELVAL *prep = ultimate - 1;
    for (; prep > tail; --prep) {
        prep->header.bits =
            FLAG_KIND3Q_BYTE(REB_T_TRASH)
            | FLAG_HEART_BYTE(REB_T_TRASH); // unreadable
        TRACK_CELL_IF_DEBUG_EVIL_MACRO(prep, __FILE__, __LINE__);
    }
  #endif

    f->arg = f->rootvar + 1;

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // f->special here will either equal f->param (to indicate normal argument
    // fulfillment) or the head of the "exemplar".  To speed this up, the
    // absence of a cached exemplar just means that the "specialty" holds the
    // paramlist... this means no conditional code is needed here.
    //
    f->special = ACT_SPECIALTY_HEAD(act);

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(NOT_SERIES_INFO(f->varlist, INACCESSIBLE));
}


inline static void Drop_Action(REBFRM *f) {
    assert(
        not f->opt_label
        or GET_SERIES_FLAG(f->opt_label, IS_STRING)
    );

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);

    CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);
    CLEAR_EVAL_FLAG(f, FULFILL_ONLY);
    CLEAR_EVAL_FLAG(f, REQUOTE_NULL);

    assert(
        GET_SERIES_INFO(f->varlist, INACCESSIBLE)
        or LINK_KEYSOURCE(f->varlist) == NOD(f)
    );

    if (GET_SERIES_INFO(f->varlist, INACCESSIBLE)) {
        //
        // If something like Encloser_Dispatcher() runs, it might steal the
        // variables from a context to give them to the user, leaving behind
        // a non-dynamic node.  Pretty much all the bits in the node are
        // therefore useless.  It served a purpose by being non-null during
        // the call, however, up to this moment.
        //
        if (GET_SERIES_FLAG(f->varlist, MANAGED))
            f->varlist = nullptr; // references exist, let a new one alloc
        else {
            // This node could be reused vs. calling Alloc_Node() on the next
            // action invocation...but easier for the moment to let it go.
            //
            Free_Node(SER_POOL, NOD(f->varlist));
            f->varlist = nullptr;
        }
    }
    else if (GET_SERIES_FLAG(f->varlist, MANAGED)) {
        //
        // Varlist wound up getting referenced in a cell that will outlive
        // this Drop_Action().
        //
        // !!! The new concept is to let frames survive indefinitely in this
        // case.  This is in order to not let JavaScript have the upper hand
        // in "closure"-like scenarios.  See:
        //
        // "What Happens To Function Args/Locals When The Call Ends"
        // https://forum.rebol.info/t/234
        //
        // Previously this said:
        //
        // "The pointer needed to stay working up until now, but the args
        // memory won't be available.  But since we know there are outstanding
        // references to the varlist, we need to convert it into a "stub"
        // that's enough to avoid crashes.
        //
        // ...but we don't free the memory for the args, we just hide it from
        // the stub and get it ready for potential reuse by the next action
        // call.  That's done by making an adjusted copy of the stub, which
        // steals its dynamic memory (by setting the stub not HAS_DYNAMIC)."
        //
      #if 0
        f->varlist = CTX_VARLIST(
            Steal_Context_Vars(
                CTX(f->varlist),
                NOD(f->original) // degrade keysource from f
            )
        );
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        INIT_LINK_KEYSOURCE(f->varlist, NOD(f));
      #endif

        INIT_LINK_KEYSOURCE(f->varlist, NOD(f->original));
        f->varlist = nullptr;
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.  
        //
        // But no series bits we didn't set should be set...and right now,
        // only PARAMLIST_FLAG_IS_NATIVE sets HOLD.  Clear that.  Also, it's
        // possible for a "telegraphed" no lookahead bit used by an invisible
        // to be left on, so clear it too.
        //
        CLEAR_SERIES_INFO(f->varlist, HOLD);
        CLEAR_SERIES_INFO(f->varlist, TELEGRAPH_NO_LOOKAHEAD);

        assert(0 == (SER(f->varlist)->info.bits & ~( // <- note bitwise not
            SERIES_INFO_0_IS_TRUE // parallels NODE_FLAG_NODE
            | FLAG_WIDE_BYTE_OR_0(0) // don't mask out wide (0 for arrays))
            | FLAG_LEN_BYTE_OR_255(255) // mask out non-dynamic-len (dynamic)
        )));
    }

  #if !defined(NDEBUG)
    if (f->varlist) {
        assert(NOT_SERIES_INFO(f->varlist, INACCESSIBLE));
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

        RELVAL *rootvar = ARR_HEAD(f->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == f->varlist);
        TRASH_POINTER_IF_DEBUG(PAYLOAD(Any, rootvar).second.node);  // phase
        TRASH_POINTER_IF_DEBUG(EXTRA(Binding, rootvar).node);
    }
  #endif

    f->original = nullptr; // signal an action is no longer running

    TRASH_POINTER_IF_DEBUG(f->opt_label);
  #if defined(DEBUG_FRAME_LABELS)
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif
}


//=//// ARGUMENT AND PARAMETER ACCESS HELPERS ////=///////////////////////////
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Reb_Frame* passed to every
// REBNATIVE ('frame_') and read the information out cleanly, like this:
//
//     PARAM(1, foo);
//     PARAM(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) and REF(bar)) { ... }
//
// The PARAM macro uses token pasting to name the indexes they are declaring
// `p_name` instead of just `name`.  This prevents collisions with C/C++
// identifiers, so PARAM(case) and PARAM(new) would make `p_case` and `p_new`
// instead of just `case` and `new` as the variable names.
//
// ARG() gives a mutable pointer to the argument's cell.  REF() is typically
// used with refinements, and gives a const reference where NULLED cells are
// turned into C nullptr.  This can be helpful for any argument that is
// optional, as the libRebol API does not accept NULLED cells directly.
//
// By contract, Rebol functions are allowed to mutate their arguments and
// refinements just as if they were locals...guaranteeing only their return
// result as externally visible.  Hence the ARG() cells provide a GC-safe
// slot for natives to hold values once they are no longer needed.
//
// It is also possible to get the typeset-with-symbol for a particular
// parameter or refinement, e.g. with `PAR(foo)` or `PAR(bar)`.

#define PARAM(n,name) \
    static const int p_##name##_ = n

#define ARG(name) \
    FRM_ARG(frame_, (p_##name##_))

#define PAR(name) \
    ACT_PARAM(FRM_PHASE(frame_), (p_##name##_))  // a REB_P_XXX pseudovalue

#define REF(name) \
    NULLIFY_NULLED(ARG(name))


// Quick access functions from natives (or compatible functions that name a
// Reb_Frame pointer `frame_`) to get some of the common public fields.
//
#define D_FRAME     frame_
#define D_OUT       FRM_OUT(frame_)         // GC-safe slot for output value
#define D_SPARE     FRM_SPARE(frame_)       // scratch GC-safe cell

// !!! Numbered arguments got more complicated with the idea of moving the
// definitional returns into the first slot (if applicable).  This makes it
// more important to use the named ARG() and REF() macros.  As a stopgap
// measure, we just sense whether the phase has a return or not.
//
inline static REBVAL *D_ARG_Core(REBFRM *f, REBLEN n) {  // 1 for first arg
    return GET_ACTION_FLAG(FRM_PHASE(f), HAS_RETURN)
        ? FRM_ARG(f, n + 1)
        : FRM_ARG(f, n);
}
#define D_ARG(n) \
    D_ARG_Core(frame_, (n))

// Convenience routine for returning a value which is *not* located in D_OUT.
// (If at all possible, it's better to build values directly into D_OUT and
// then return the D_OUT pointer...this is the fastest form of returning.)
//
#define RETURN(v) \
    return Move_Value(D_OUT, (v))


// Shared code for type checking the return result.  It's used by the
// Returner_Dispatcher(), but custom dispatchers use it to (e.g. JS-NATIVE)
//
inline static void FAIL_IF_BAD_RETURN_TYPE(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    REBVAL *typeset = ACT_PARAMS_HEAD(phase);
    assert(VAL_PARAM_SYM(typeset) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a function.
    //
    if (not Typecheck_Including_Constraints(typeset, f->out))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));
}
