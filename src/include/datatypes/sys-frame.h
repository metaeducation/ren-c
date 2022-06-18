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
inline static bool ANY_ESCAPABLE_GET(const Cell *v) {
    //
    // !!! Note: GET-BLOCK! is used to mean reduce, e.g.
    //
    //     >> if true :[1 + 2 10 + 20]
    //     == [3 30]
    //
    // This is a useful enough concept in branching that it makes more sense
    // than "escaping a block", whatever that would be.
    //
    return IS_GET_GROUP(v) or IS_GET_WORD(v)
        or IS_GET_PATH(v) or IS_GET_TUPLE(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOW-LEVEL FRAME ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


#define Is_Action_Frame(f) \
    ((f)->executor == &Action_Executor)


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
    // !!! Note: Too early in boot at the moment to use Canon(ANONYMOUS).
    //
    const REBSTR *str = FRM_FILE(f);
    return str ? STR_UTF8(str) : "~anonymous~";
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

inline static option(const Symbol*) FRM_LABEL(REBFRM *f) {
    assert(Is_Action_Frame(f));
    return f->label;
}


#define FRM_DSP_ORIG(f) \
    ((f)->dsp_orig + 0) // prevent assignment via this macro


#if (! CPLUSPLUS_11)
    #define FRM_STATE_BYTE(f) \
        mutable_SECOND_BYTE((f)->flags)
#else
    // Having a special accessor in the C++ build serves two purposes.  One,
    // it can actually type check that `f` is a frame.  But secondly, it also
    // is a good place to inject an assertion that you're not ignoring the
    // fact that a frame "self-errored" and was notified of an abrupt failure.
    //
    inline static REBYTE& FRM_STATE_BYTE(REBFRM *f) {
        assert(Not_Eval_Flag(f, ABRUPT_FAILURE));
        return mutable_SECOND_BYTE(f->flags);
    }
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

inline static void Get_Frame_Label_Or_Nulled(Cell *out, REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->label)
        Init_Word(out, unwrap(f->label));  // WORD!, PATH!, or stored invoke
    else
        Init_Nulled(out);  // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(REBFRM *f) {
    assert(Is_Action_Frame(f));
    if (f->label)
        return STR_UTF8(unwrap(f->label));
    return "[anonymous]";
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
    if (Get_Eval_Flag(f, ALLOCATED_FEED))
        Free_Feed(f->feed);  // didn't inherit from parent, and not END_FRAME

    if (f->varlist and NOT_SERIES_FLAG(f->varlist, MANAGED))
        GC_Kill_Series(f->varlist);
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
    // not on each Eval_Step() for `reduce [a | b | ... | z]`.
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

  #if DEBUG_EXPIRED_LOOKBACK
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

    TRASH_POINTER_IF_DEBUG(f->original);
    TRASH_OPTION_IF_DEBUG(f->label);
  #if DEBUG_FRAME_LABELS
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

    // If the frame is trying to use the NEXT_ARG_FROM_OUT trick, then it
    // means the feed needs to keep the same output cell in a subframe to
    // reuse that feed.
    if (
        GET_FEED_FLAG(f->feed, NEXT_ARG_FROM_OUT)
        and f->prior->feed == f->feed
    ){
        assert(f->out == f->prior->out);
    }

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
        RESET(ARR_SINGLE(a));
        GC_Kill_Series(a);
    }
    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    // Abort_Frame() handles any work that wouldn't be done done naturally by
    // feeding a frame to its natural end.
    //
    if (IS_END(f->feed->value))
        goto pop;

  pop:
    Rollback_Globals_To_State(&f->baseline);

    assert(TG_Top_Frame == f);
    TG_Top_Frame = f->prior;

    Free_Frame_Internal(f);
}


inline static void Drop_Frame_Core(REBFRM *f) {
  #if DEBUG_ENSURE_FRAME_EVALUATES
    assert(f->was_eval_called);  // must call evaluator--even on empty array
  #endif

  #if DEBUG_EXPIRED_LOOKBACK
    free(f->stress);
  #endif

    assert(TG_Top_Frame == f);

    REBNOD *n = f->alloc_value_list;
    while (n != f) {
        REBARR *a = ARR(n);
      #if !defined(NDEBUG)
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
    assert(not Is_Throwing(f));  // !!! We could decide to abort or not on this

  #if DEBUG_BALANCE_STATE
    //
    // To avoid slowing down the debug build a lot, Eval_Core() doesn't
    // check this every cycle, just on drop.  But if it's hard to find which
    // exact cycle caused the problem, see BALANCE_CHECK_EVERY_EVALUATION_STEP
    //
    ASSERT_STATE_BALANCED(&f->baseline);
  #else
    assert(DSP == f->baseline.dsp);  // Cheaper check
  #endif

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
    Prep_Void(&f->spare);
    TRASH_POINTER_IF_DEBUG(f->out);

  #if DEBUG_ENSURE_FRAME_EVALUATES
    f->was_eval_called = false;
  #endif

    f->varlist = nullptr;
    f->executor = &Evaluator_Executor;  // compatible default (for now)

    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    // !!! Previously only the DSP was captured in f->baseline.dsp, but then
    // redundantly captured via a SNAP_STATE() in Push_Frame().  The
    // responsibilities of DECLARE_FRAME vs Push_Frame() aren't clearly laid
    // out, but some clients do depend on the DSP being captured before
    // Push_Frame() is called, so this snaps the whole baseline here.
    //
    SNAP_STATE(&f->baseline);  // see notes on `baseline` in Reb_Frame

  #if DEBUG_COUNT_TICKS
    f->tick = TG_Tick;
  #endif
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


#define Begin_Enfix_Action(f,label) \
    Begin_Action_Core((f), (label), true)

#define Begin_Prefix_Action(f,label) \
    Begin_Action_Core((f), (label), false)


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
// There is an option to not define them due to conflicts with OUT as defined
// by the Windows.h headers.  This makes it easier for people who don't want
// to #undef the Windows versions and would rather pick their own shorthands,
// (if any).
//
#if REBOL_FRAME_SHORTHAND_MACROS
    #define FRAME   frame_
    #define OUT     FRM_OUT(frame_)         // GC-safe slot for output value
    #define SPARE   FRM_SPARE(frame_)       // scratch GC-safe cell
    #define STATE   FRM_STATE_BYTE(frame_)

    #define VOID    Native_Void_Result(frame_)
    #define NONE    Native_None_Result(frame_)
    #define THROWN  Native_Thrown_Result(frame_)
    #define BASELINE   (&frame_->baseline)
#endif


// !!! Numbered arguments got more complicated with the idea of moving the
// definitional returns into the first slot (if applicable).  This makes it
// more important to use the named ARG() and REF() macros.  As a stopgap
// measure, we just sense whether the phase has a return or not.
//
inline static REBVAL *D_ARG_Core(REBFRM *f, REBLEN n) {  // 1 for first arg
    REBPAR *param = ACT_PARAMS_HEAD(FRM_PHASE(f));
    REBVAL *arg = FRM_ARG(f, 1);
    while (
        VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN
        or VAL_PARAM_CLASS(param) == PARAM_CLASS_OUTPUT
    ){
        ++param;
        ++arg;
    }
    return arg + n - 1;
}
#define D_ARG(n) \
    D_ARG_Core(frame_, (n))


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


inline static bool Eval_Value_Core_Throws(
    REBVAL *out,
    REBFLGS flags,
    const Cell *value,  // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
);

// Conveniences for returning a continuation.  The concept is that when a
// R_CONTINUATION comes back via the C `return` for a native, that native's
// C stack variables are all gone.  But the heap-allocated Rebol frame stays
// intact and in the Rebol stack trace.  It will be resumed when the
// continuation finishes.
//
inline static bool Push_Continuation_Throws(
    REBVAL *out,
    REBFRM *frame_,
    REBFLGS flags,  // EVAL_FLAG_BRANCH, etc. for pushed frames
    const Cell *branch,
    REBSPC *branch_specifier,
    const REBVAL *with  // gets copied if not END_VALUE, need not be GC-safe
){
    assert(out == &frame_->spare or out == frame_->out);  // anywhere else?
    assert(branch != out);  // it's legal for `with` to be the same as out

    // We don't want to force frames like IF that only do a delegation to
    // muck with the state byte.  But 0 means "initial entry", and there are
    // places to leverage this.  EVAL_FLAG_DELEGATE_CONTROL should be used with
    // setting the state byte to something nonzero.
    //
    assert(FRM_STATE_BYTE(frame_) != 0);
    UNUSED(frame_);

    // !!! This code came from Do_Branch_XXX_Throws() which was not
    // continuation-based, and hence holds some reusable logic for
    // branch types that do not require evaluation...like REB_QUOTED.
    // It's the easiest way to reuse the logic for the time being,
    // though some performance gain could be achieved for instance
    // if it were reused in a way that allowed something like IF to
    // not bother running again (like if `CONTINUE()` would do a plain
    // return in those cases).  But more complex scenarios may have
    // broken control flow if such shortcuts were taken.  Review.

    DECLARE_LOCAL (temp);
    if (IS_GROUP(branch)) {
        REBFLGS group_flags = EVAL_MASK_DEFAULT;
        if (Eval_Value_Core_Throws(temp, group_flags, branch, branch_specifier))
            return true;

        branch = temp;
        branch_specifier = SPECIFIED;
    }

    enum Reb_Kind kind = VAL_TYPE(branch);
    switch (kind) {
      case REB_BLANK:
        if (flags & EVAL_FLAG_BRANCH)
            Init_Null_Isotope(out);
        else
            Init_Nulled(out);
        goto just_use_out;

      case REB_QUOTED:
        Unquotify(Derelativize(out, branch, branch_specifier), 1);
        if (IS_NULLED(out) and (flags & EVAL_FLAG_BRANCH))
            Init_Null_Isotope(out);
        goto just_use_out;

      case REB_META_BLOCK:
      case REB_BLOCK: {
        DECLARE_FRAME_AT_CORE (
            f,
            branch,
            branch_specifier,
            flags | EVAL_MASK_DEFAULT | EVAL_FLAG_TO_END
        );
        if (kind == REB_META_BLOCK)
            Set_Eval_Flag(f, META_RESULT);

        Push_Frame(out, f);
        return false; }  // trampoline manages EVAL_FLAG_BRANCH atm.

      case REB_GET_BLOCK: {  // effectively REDUCE
        DECLARE_END_FRAME (
            f,
            EVAL_MASK_DEFAULT | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
        );
        Push_Frame(out, f);

        const REBVAL *action = Lib(REDUCE);
        Push_Action(f, VAL_ACTION(action), VAL_ACTION_BINDING(action));
        Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

        const REBKEY *key = f->key;
        const REBPAR *param = f->param;
        REBVAL *arg = f->arg;
        for (; key != f->key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Init_None(arg);
        }

        arg = First_Unspecialized_Arg(&param, f);
        Derelativize(arg, branch, branch_specifier);
        mutable_HEART_BYTE(arg) = REB_BLOCK;  // :[1 + 2] => [3], not :[3]
        return false; }

      case REB_ACTION : {
        DECLARE_END_FRAME (
            f,
            EVAL_MASK_DEFAULT | FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING)
        );
        Push_Frame(out, f);
        Push_Action(f, VAL_ACTION(branch), VAL_ACTION_BINDING(branch));
        Begin_Prefix_Action(f, VAL_ACTION_LABEL(branch));

        const REBKEY *key = f->key;
        const REBPAR *param = f->param;
        REBVAL *arg = f->arg;
        for (; key != f->key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Init_None(arg);
        }

        if (NOT_END(with)) do {
            arg = First_Unspecialized_Arg(&param, f);
            if (not arg)
                break;

            if (Is_Void(with))
              { Init_Void_Isotope(arg); break; }

            Copy_Cell(arg, Pointer_To_Decayed(with));

            if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
              { Meta_Quotify(arg); break; }

            if (Is_Isotope(arg))
                fail ("Can't pass isotope to non-META parameter");
        } while (0);

        return false; }

      default:
        //
        // !!! Things like CASE currently ask for a branch-based continuation
        // on types they haven't checked, but encounter via evaluation.
        // Hence we FAIL here instead of panic()...but that suggests this
        // should be narrowed to the kinds of types branching permits.
        //
        fail ("Bad branch type");
    }

  just_use_out:
    return false;
}


#define continue_core(o,flags,branch,specifier,with) \
    do { \
        if (Push_Continuation_Throws( \
                RESET(o), \
                frame_, \
                (flags), \
                (branch), \
                (specifier), \
                (with) \
        )){ \
            return R_THROWN;  /* no frame pushed, just get callback */ \
        } \
        return R_CONTINUATION; \
    } while (0)

#define delegate_core(o,flags,branch,specifier,with) \
    do { \
        Set_Eval_Flag(frame_, DELEGATE_CONTROL); \
        FRM_STATE_BYTE(frame_) = 1; \
        continue_core((o), (flags), (branch), (specifier), (with)); \
    } while (0)


// !!! Delegation doesn't want to use the frame that's pushed.  It leaves it
// on the stack for sanity of debug tracing, but it could be more optimal
// if the delegating frame were freed before running what's underneath it...
// at least it could be collapsed into a more primordial state.  Review.
//
#define delegate(o,value,with) \
    do { \
        assert((o) == frame_->out); \
        FRM_STATE_BYTE(frame_) = 255; /* 0 reserved for initial entry */ \
        Set_Eval_Flag(frame_, DELEGATE_CONTROL); \
        continue_core( \
            frame_->out, EVAL_MASK_DEFAULT, (value), SPECIFIED, (with) \
        ); \
    } while (0)

#define delegate_branch(o,branch,with) \
    do { \
        assert((o) == frame_->out); \
        FRM_STATE_BYTE(frame_) = 255; /* 0 reserved for initial entry */ \
        Set_Eval_Flag(frame_, DELEGATE_CONTROL); \
        continue_core( \
            frame_->out, EVAL_FLAG_BRANCH, (branch), SPECIFIED, (with) \
        ); \
    } while (0)

// Normal continuations come in catching and non-catching forms; they evaluate
// without tampering with the result.

#define continue_uncatchable(o,value,with) \
    continue_core((o), EVAL_MASK_DEFAULT, (value), SPECIFIED, (with))

#define continue_catchable(o,value,with) \
    do { \
        assert(Is_Action_Frame(frame_)); \
        Set_Eval_Flag(frame_, DISPATCHER_CATCHES); \
        continue_core((o), EVAL_MASK_DEFAULT, (value), SPECIFIED, (with)); \
    } while (0)

// Branch continuations enforce the result not being pure null or void

#define continue_uncatchable_branch(o,branch,with) \
    continue_core((o), EVAL_FLAG_BRANCH, (branch), SPECIFIED, (with))

#define continue_catchable_branch(o,branch,with) \
    do { \
        assert(Is_Action_Frame(frame_)); \
        Set_Eval_Flag(frame_, DISPATCHER_CATCHES); \
        continue_core((o), EVAL_FLAG_BRANCH, (branch), SPECIFIED, (with)); \
    } while (0)


inline static REB_R Continue_Subframe_Helper(
    REBFRM *f,
    bool must_be_dispatcher,
    REBFLGS catches_flag,
    REBFRM *sub
){
    if (must_be_dispatcher)
        assert(Is_Action_Frame(f) and not Is_Action_Frame_Fulfilling(f));
    else
        assert(not Is_Action_Frame(f) or Is_Action_Frame_Fulfilling(f));

    assert(FRM_STATE_BYTE(f) != 0);  // reserved for initial entry

    if (catches_flag != 0) {
        assert(catches_flag == EVAL_FLAG_DISPATCHER_CATCHES);
        f->flags.bits |= catches_flag;
    }

    assert(sub == FS_TOP);  // currently subframe must be pushed and top frame
    return R_CONTINUATION;
}

#define continue_subframe(sub) \
    return Continue_Subframe_Helper(frame_, false, 0, (sub))

#define continue_catchable_subframe(sub) \
    return Continue_Subframe_Helper( \
        _frame, true, EVAL_FLAG_DISPATCHER_CATCHES, (sub))

#define continue_uncatchable_subframe(sub) \
    return Continue_Subframe_Helper(frame_, true, 0, (sub))

#define delegate_subframe(sub) \
    do { \
        FRM_STATE_BYTE(frame_) = 255;  /* 0 reserved for initial entry */ \
        Set_Eval_Flag(frame_, DELEGATE_CONTROL); \
        return Continue_Subframe_Helper(frame_, true, 0, (sub)); \
    } while (0)
