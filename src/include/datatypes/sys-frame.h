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
inline static bool ANY_ESCAPABLE_GET(const RELVAL *v) {
    if (IS_GET_BLOCK(v))
        fail ("GET-BLOCK! in escapable parameter slots currently reserved");
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


// While a function frame is fulfilling its arguments, the `f->key` will
// be pointing to a typeset.  The invariant that is maintained is that
// `f->key` will *not* be a typeset when the function is actually in the
// process of running.  (So no need to set/clear/test another "mode".)
//
// Some cases in debug code call this all the way up the call stack, and when
// the debug build doesn't inline functions it's best to use as a macro.

inline static bool Is_Action_Frame_Fulfilling(REBFRM *f) {
    assert(Is_Action_Frame(f));
    return f->key != f->key_tail;
}


inline static bool FRM_IS_VARIADIC(REBFRM *f) {
    return FEED_IS_VARIADIC(f->feed);
}

inline static const REBARR *FRM_ARRAY(REBFRM *f) {
    assert(IS_END(f->feed->value) or not FRM_IS_VARIADIC(f));
    return FEED_ARRAY(f->feed);
}

inline static REBSPC *FRM_SPECIFIER(REBFRM *f) {
    return FEED_SPECIFIER(f->feed);
}


// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `option(head)` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBLEN FRM_INDEX(REBFRM *f) {
    if (IS_END(f->feed->value))
        return ARR_LEN(FRM_ARRAY(f));

    assert(not FRM_IS_VARIADIC(f));
    return FEED_INDEX(f->feed) - 1;
}

inline static REBLEN FRM_EXPR_INDEX(REBFRM *f) {
    assert(not FRM_IS_VARIADIC(f));
    return f->expr_index - 1;
}

inline static const REBSTR* FRM_FILE(REBFRM *f) {
    if (FRM_IS_VARIADIC(f))
        return nullptr;
    if (NOT_SUBCLASS_FLAG(ARRAY, FRM_ARRAY(f), HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return LINK(Filename, FRM_ARRAY(f));
}

inline static const char* FRM_FILE_UTF8(REBFRM *f) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(__ANONYMOUS__).
    //
    const REBSTR *str = FRM_FILE(f);
    return str ? STR_UTF8(str) : "(anonymous)";
}

inline static int FRM_LINE(REBFRM *f) {
    if (FRM_IS_VARIADIC(f))
        return 0;
    if (NOT_SUBCLASS_FLAG(ARRAY, FRM_ARRAY(f), HAS_FILE_LINE_UNMASKED))
        return 0;
    return FRM_ARRAY(f)->misc.line;
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

inline static void INIT_FRM_PHASE(REBFRM *f, REBACT *phase)  // check types
  { INIT_VAL_FRAME_PHASE_OR_LABEL(f->rootvar, phase); }  // ...only

inline static void INIT_FRM_BINDING(REBFRM *f, REBCTX *binding)
  { mutable_BINDING(f->rootvar) = binding; }  // also fast

#define FRM_BINDING(f) \
    cast(REBCTX*, BINDING((f)->rootvar))

inline static option(const REBSTR*) FRM_LABEL(REBFRM *f) {
    assert(Is_Action_Frame(f));
    return f->label;
}


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
#define f_specifier FEED_SPECIFIER(f->feed)
#define f_spare FRM_SPARE(f)
#define f_gotten f->feed->gotten
#define f_index FRM_INDEX(f)
#define f_array FRM_ARRAY(f)


inline static REBCTX *Context_For_Frame_May_Manage(REBFRM *f) {
    assert(not Is_Action_Frame_Fulfilling(f));
    SET_SERIES_FLAG(f->varlist, MANAGED);
    return CTX(f->varlist);
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

inline static void Get_Frame_Label_Or_Blank(RELVAL *out, REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->label)
        Init_Word(out, unwrap(f->label));  // WORD!, PATH!, or stored invoke
    else
        Init_Blank(out);  // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->label)
        return STR_UTF8(unwrap(f->label));
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
// simple examples like `x: 0 repeat 1000000 [x: x + 1]` by at least 20%.
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
    TG_Reuse = LINK(ReuseNext, TG_Reuse);
    f->rootvar = cast(REBVAL*, f->varlist->content.dynamic.data);
    mutable_LINK(KeySource, f->varlist) = f;
    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(SER_FLAVOR(f->varlist) == FLAVOR_VARLIST);
    return true;
}

inline static void Conserve_Varlist(REBARR *varlist)
{
  #if !defined(NDEBUG)
    assert(NOT_SERIES_FLAG(varlist, INACCESSIBLE));
    assert(NOT_SERIES_FLAG(varlist, MANAGED));
    assert(NOT_SUBCLASS_FLAG(VARLIST, varlist, FRAME_HAS_BEEN_INVOKED));

    RELVAL *rootvar = ARR_HEAD(varlist);
    assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == varlist);
    INIT_VAL_FRAME_PHASE_OR_LABEL(rootvar, nullptr);  // can't trash
    TRASH_POINTER_IF_DEBUG(mutable_BINDING(rootvar));
  #endif

    mutable_LINK(ReuseNext, varlist) = TG_Reuse;
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
    if (GET_EVAL_FLAG(f, ALLOCATED_FEED))
        Free_Feed(f->feed);  // didn't inherit from parent, and not END_FRAME

    if (f->varlist and NOT_SERIES_FLAG(f->varlist, MANAGED))
        Conserve_Varlist(f->varlist);
    TRASH_POINTER_IF_DEBUG(f->varlist);

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));

    Free_Node(FRM_POOL, f);
}


inline static void Push_Frame(
    REBVAL *out,  // type check prohibits passing `unstable` cells for output
    REBFRM *f
){
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
        if (Is_Action_Frame_Fulfilling(ftemp))
            continue;
        if (GET_SERIES_FLAG(ftemp->varlist, INACCESSIBLE))
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

    TRASH_OPTION_IF_DEBUG(f->label);
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

  #if defined(DEBUG_BALANCE_STATE)
    SNAP_STATE(&f->state); // to make sure stack balances, etc.
    f->state.dsp = f->dsp_orig;
  #endif

    assert(f->varlist == nullptr);  // Prep_Frame_Core() set to nullptr

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));
    f->alloc_value_list = f;  // doubly link list, terminates in `f`
}


inline static void UPDATE_EXPRESSION_START(REBFRM *f) {
    if (not FRM_IS_VARIADIC(f))
        f->expr_index = FRM_INDEX(f);
}


#define Literal_Next_In_Frame(out,f) \
    Literal_Next_In_Feed((out), (f)->feed)

inline static void Abort_Frame(REBFRM *f) {
    //
    // If a frame is aborted, then we allow its API handles to leak.
    //
    REBNOD *n = f->alloc_value_list;
    while (n != f) {
        REBARR *a = ARR(n);
        n = LINK(ApiNext, a);
        REFORMAT_CELL_IF_DEBUG(ARR_SINGLE(a));
        GC_Kill_Series(a);
    }
    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    // Abort_Frame() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    //
    if (IS_END(f->feed->value))
        goto pop;

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

    assert(TG_Top_Frame == f);

    REBNOD *n = f->alloc_value_list;
    while (n != f) {
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
    REBFED *feed,
    REBFLGS flags
){
   if (f == nullptr)  // e.g. a failed allocation
       fail (Error_No_Memory(sizeof(REBFRM)));

    assert(
        (flags & EVAL_MASK_DEFAULT) ==
            (EVAL_FLAG_0_IS_TRUE | EVAL_FLAG_7_IS_TRUE)
    );
    f->flags.bits = flags;

    f->feed = feed;
    Prep_Cell(&f->spare);
    Init_Trash(&f->spare);
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
    DECLARE_FRAME (name, name##feed, (flags) | EVAL_FLAG_ALLOCATED_FEED)

#define DECLARE_FRAME_AT_CORE(name,any_array,specifier,flags) \
    DECLARE_FEED_AT_CORE (name##feed, (any_array), (specifier)); \
    DECLARE_FRAME (name, name##feed, (flags) | EVAL_FLAG_ALLOCATED_FEED)

#define DECLARE_END_FRAME(name,flags) \
    DECLARE_FRAME (name, TG_End_Feed, flags)


inline static void Begin_Action_Core(
    REBFRM *f,
    option(const REBSYM*) label,
    bool enfix
){
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));
    assert(NOT_FEED_FLAG(f->feed, DEFERRING_ENFIX));

    assert(NOT_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED));
    SET_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

    assert(not f->original);
    f->original = FRM_PHASE(f);

    // f->key_tail = v-- set here
    f->key = ACT_KEYS(&f->key_tail, f->original);
    f->param = ACT_PARAMS_HEAD(f->original);
    f->arg = f->rootvar + 1;

    assert(IS_OPTION_TRASH_DEBUG(f->label));  // ACTION! makes valid
    assert(not label or IS_SYMBOL(unwrap(label)));
    f->label = label;
  #if defined(DEBUG_FRAME_LABELS) // helpful for looking in the debugger
    f->label_utf8 = cast(const char*, Frame_Label_Or_Anonymous_UTF8(f));
  #endif

    // Cache the feed lookahead state so it can be restored in the event that
    // the evaluation turns out to be invisible.
    //
    STATIC_ASSERT(FEED_FLAG_NO_LOOKAHEAD == EVAL_FLAG_CACHE_NO_LOOKAHEAD);
    assert(NOT_EVAL_FLAG(f, CACHE_NO_LOOKAHEAD));
    f->flags.bits |= f->feed->flags.bits & FEED_FLAG_NO_LOOKAHEAD;

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

#define Begin_Enfix_Action(f,label) \
    Begin_Action_Core((f), (label), true)

#define Begin_Prefix_Action(f,label) \
    Begin_Action_Core((f), (label), false)


// Allocate the series of REBVALs inspected by a function when executed (the
// values behind ARG(name), REF(name), D_ARG(3),  etc.)
//
// This only allocates space for the arguments, it does not initialize.
// Eval_Core initializes as it goes, and updates f->key so the GC knows how
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
    REBCTX *binding  // actions may only be bound to contexts ATM
){
    assert(NOT_EVAL_FLAG(f, FULFILL_ONLY));
    assert(NOT_EVAL_FLAG(f, RUNNING_ENFIX));

    STATIC_ASSERT(EVAL_FLAG_FULFILLING_ARG == DETAILS_FLAG_IS_BARRIER);
    REBARR *details = ACT_DETAILS(act);
    if (f->flags.bits & details->leader.bits & DETAILS_FLAG_IS_BARRIER)
        fail (Error_Expression_Barrier_Raw());

    REBLEN num_args = ACT_NUM_PARAMS(act);  // includes specialized + locals

    REBSER *s;
    if (
        f->varlist  // !!! May be going to point of assuming nullptr
        or Did_Reuse_Varlist_Of_Unknown_Size(f, num_args)  // want `num_args`
    ){
        s = f->varlist;
      #ifdef DEBUG_TERM_ARRAYS
        if (s->content.dynamic.rest >= num_args + 1 + 1)  // +rootvar, +end
            goto sufficient_allocation;
      #else
        if (s->content.dynamic.rest >= num_args + 1)  // +rootvar
            goto sufficient_allocation;
      #endif


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
        SER_INFO(s) = SERIES_INFO_MASK_NONE;
        INIT_LINK_KEYSOURCE(ARR(s), f);  // maps varlist back to f
        mutable_MISC(VarlistMeta, s) = nullptr;
        mutable_BONUS(Patches, s) = nullptr;
        f->varlist = ARR(s);
    }

    if (not Did_Series_Data_Alloc(s, num_args + 1 + 1)) {  // +rootvar, +end
        SET_SERIES_FLAG(s, INACCESSIBLE);
        GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE
        f->varlist = nullptr;
        fail (Error_No_Memory(sizeof(REBVAL) * (num_args + 1 + 1)));
    }

    f->rootvar = cast(REBVAL*, s->content.dynamic.data);
    USED(TRACK_CELL_IF_DEBUG(f->rootvar));
    f->rootvar->header.bits =
        NODE_FLAG_NODE
            | NODE_FLAG_CELL
            | CELL_FLAG_PROTECTED  // payload/binding tweaked, but not by user
            | CELL_MASK_CONTEXT
            | FLAG_KIND3Q_BYTE(REB_FRAME)
            | FLAG_HEART_BYTE(REB_FRAME);
    INIT_VAL_CONTEXT_VARLIST(f->rootvar, f->varlist);

  sufficient_allocation:

    INIT_VAL_FRAME_PHASE(f->rootvar, act);  // FRM_PHASE()
    INIT_VAL_FRAME_BINDING(f->rootvar, binding);  // FRM_BINDING()

    s->content.dynamic.used = num_args + 1;

  #if !defined(NDEBUG)  // poison cells past usable range
  blockscope {
    RELVAL *tail = ARR_TAIL(f->varlist);
    RELVAL *prep = ARR_AT(f->varlist, s->content.dynamic.rest - 1);
    for (; prep >= tail; --prep) {
        USED(TRACK_CELL_IF_DEBUG(prep));
        prep->header.bits =
            FLAG_KIND3Q_BYTE(REB_T_UNSAFE)
            | FLAG_HEART_BYTE(REB_T_UNSAFE); // unreadable
    }
  }
  #endif

  #ifdef DEBUG_TERM_ARRAYS  // expects cell is trash (e.g. a cell) not poison
    Init_Trash(Prep_Cell(ARR_TAIL(f->varlist)));
  #endif

    // Each layer of specialization of a function can only add specializations
    // of arguments which have not been specialized already.  For efficiency,
    // the act of specialization merges all the underlying layers of
    // specialization together.  This means only the outermost specialization
    // is needed to fill the specialized slots contributed by later phases.
    //
    // f->param here will either equal f->key (to indicate normal argument
    // fulfillment) or the head of the "exemplar".
    //
    // !!! It is planned that exemplars will be unified with paramlist, making
    // the context keys something different entirely.
    //
    REBARR *partials = try_unwrap(ACT_PARTIALS(act));
    if (partials) {
        const RELVAL *word_tail = ARR_TAIL(partials);
        const REBVAL *word = SPECIFIC(ARR_HEAD(partials));
        for (; word != word_tail; ++word)
            Copy_Cell(DS_PUSH(), word);
    }

    assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
    assert(NOT_SERIES_FLAG(f->varlist, INACCESSIBLE));
}


inline static void Drop_Action(REBFRM *f) {
    assert(not f->label or IS_SYMBOL(unwrap(f->label)));

    if (NOT_EVAL_FLAG(f, FULFILLING_ARG))
        CLEAR_FEED_FLAG(f->feed, BARRIER_HIT);

    if (f->out->header.bits & CELL_FLAG_OUT_NOTE_STALE) {
        //
        // If the whole evaluation of the action turned out to be invisible,
        // then refresh the feed's NO_LOOKAHEAD state to whatever it was
        // before that invisible evaluation ran.
        //
        STATIC_ASSERT(FEED_FLAG_NO_LOOKAHEAD == EVAL_FLAG_CACHE_NO_LOOKAHEAD);
        f->feed->flags.bits &= ~FEED_FLAG_NO_LOOKAHEAD;
        f->feed->flags.bits |= f->flags.bits & EVAL_FLAG_CACHE_NO_LOOKAHEAD;
    }
    CLEAR_EVAL_FLAG(f, CACHE_NO_LOOKAHEAD);

    CLEAR_EVAL_FLAG(f, RUNNING_ENFIX);
    CLEAR_EVAL_FLAG(f, FULFILL_ONLY);

    assert(
        GET_SERIES_FLAG(f->varlist, INACCESSIBLE)
        or LINK(KeySource, f->varlist) == f
    );

    if (GET_SERIES_FLAG(f->varlist, INACCESSIBLE)) {
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
            Free_Node(SER_POOL, f->varlist);
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
                f->original  // degrade keysource from f
            )
        );
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));
        INIT_LINK_KEYSOURCE(f->varlist, f);
      #endif

        INIT_LINK_KEYSOURCE(f->varlist, ACT_KEYLIST(f->original));
        f->varlist = nullptr;
    }
    else {
        // We can reuse the varlist and its data allocation, which may be
        // big enough for ensuing calls.
        //
        // But no series bits we didn't set should be set...and right now,
        // only DETAILS_FLAG_IS_NATIVE sets HOLD.  Clear that.
        //
        CLEAR_SERIES_INFO(f->varlist, HOLD);
        CLEAR_SUBCLASS_FLAG(VARLIST, f->varlist, FRAME_HAS_BEEN_INVOKED);

        assert(
            0 == (SER_INFO(f->varlist) & ~(  // <- note bitwise not
                SERIES_INFO_0_IS_FALSE
                    | FLAG_USED_BYTE(255)  // mask out non-dynamic-len
        )));
    }

  #if !defined(NDEBUG)
    if (f->varlist) {
        assert(NOT_SERIES_FLAG(f->varlist, INACCESSIBLE));
        assert(NOT_SERIES_FLAG(f->varlist, MANAGED));

        RELVAL *rootvar = ARR_HEAD(f->varlist);
        assert(CTX_VARLIST(VAL_CONTEXT(rootvar)) == f->varlist);
        INIT_VAL_FRAME_PHASE_OR_LABEL(rootvar, nullptr);  // can't trash ptr
        TRASH_POINTER_IF_DEBUG(mutable_BINDING(rootvar));
    }
  #endif

    f->original = nullptr; // signal an action is no longer running

    TRASH_OPTION_IF_DEBUG(f->label);
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
    return ACT_HAS_RETURN(FRM_PHASE(f))
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
    return Copy_Cell(D_OUT, (v))

#define RETURN_INVISIBLE \
    do { \
        assert(D_OUT->header.bits & CELL_FLAG_OUT_NOTE_STALE); \
        return D_OUT; \
    } while (0)

// Shared code for type checking the return result.  It's used by the
// Returner_Dispatcher(), but custom dispatchers use it to (e.g. JS-NATIVE)
//
inline static void FAIL_IF_BAD_RETURN_TYPE(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    const REBPAR *param = ACT_PARAMS_HEAD(phase);
    assert(KEY_SYM(ACT_KEYS_HEAD(phase)) == SYM_RETURN);

    // Typeset bits for locals in frames are usually ignored, but the RETURN:
    // local uses them for the return types of a function.
    //
    if (not Typecheck_Including_Constraints(param, f->out))
        fail (Error_Bad_Return_Type(f, VAL_TYPE(f->out)));
}

inline static void FAIL_IF_NO_INVISIBLE_RETURN(REBFRM *f) {
    REBACT *phase = FRM_PHASE(f);
    const REBPAR *param = ACT_PARAMS_HEAD(phase);
    assert(KEY_SYM(ACT_KEYS_HEAD(phase)) == SYM_RETURN);

    if (ACT_DISPATCHER(phase) == &Opaque_Dispatcher)
        return;  // allow plain RETURN in <none> functions

    if (not TYPE_CHECK(param, REB_TS_ENDABLE))
        fail (Error_Bad_Invisible(f));
}
