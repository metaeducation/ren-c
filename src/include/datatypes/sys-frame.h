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
inline static bool ANY_ESCAPABLE_GET(Cell(const*) v) {
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


// Some cases in debug code call this all the way up the call stack; it may
// be helpful to inline this test some of those places.
//
inline static bool Is_Action_Frame_Fulfilling(Frame(*) f) {
    assert(Is_Action_Frame(f));
    return f->u.action.key != f->u.action.key_tail;
}


inline static bool FRM_IS_VARIADIC(Frame(*) f) {
    return FEED_IS_VARIADIC(f->feed);
}

inline static Array(const*) FRM_ARRAY(Frame(*) f) {
    assert(Is_End(f->feed->value) or not FRM_IS_VARIADIC(f));
    return FEED_ARRAY(f->feed);
}

inline static REBSPC *FRM_SPECIFIER(Frame(*) f) {
    return FEED_SPECIFIER(f->feed);
}


// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `option(head)` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBLEN FRM_INDEX(Frame(*) f) {
    if (Is_End(f->feed->value))
        return ARR_LEN(FRM_ARRAY(f));

    assert(not FRM_IS_VARIADIC(f));
    return FEED_INDEX(f->feed) - 1;
}

inline static REBLEN FRM_EXPR_INDEX(Frame(*) f) {
    assert(not FRM_IS_VARIADIC(f));
    return f->expr_index - 1;
}

inline static String(const*) FRM_FILE(Frame(*) f) {
    if (FRM_IS_VARIADIC(f))
        return nullptr;
    if (Not_Subclass_Flag(ARRAY, FRM_ARRAY(f), HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return LINK(Filename, FRM_ARRAY(f));
}

inline static const char* FRM_FILE_UTF8(Frame(*) f) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(ANONYMOUS).
    //
    String(const*) str = FRM_FILE(f);
    return str ? STR_UTF8(str) : "~anonymous~";
}

inline static int FRM_LINE(Frame(*) f) {
    if (FRM_IS_VARIADIC(f))
        return 0;
    if (Not_Subclass_Flag(ARRAY, FRM_ARRAY(f), HAS_FILE_LINE_UNMASKED))
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
// If a frame value is non-archetypal, this slot may be occupied by a String(*)
// which represents the cached name of the action from which the frame
// was created.  This FRAME! value is archetypal, however...which never holds
// such a cache.  For performance (even in the debug build, where this is
// called *a lot*) this is a macro and is unchecked.
//
#define FRM_PHASE(f) \
    cast(Action(*), VAL_FRAME_PHASE_OR_LABEL_NODE((f)->rootvar))

inline static void INIT_FRM_PHASE(Frame(*) f, Action(*) phase)  // check types
  { INIT_VAL_FRAME_PHASE_OR_LABEL(f->rootvar, phase); }  // ...only

inline static void INIT_FRM_BINDING(Frame(*) f, Context(*) binding)
  { mutable_BINDING(f->rootvar) = binding; }  // also fast

#define FRM_BINDING(f) \
    cast(Context(*), BINDING((f)->rootvar))

inline static option(Symbol(const*)) FRM_LABEL(Frame(*) f) {
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
    inline static Byte& FRM_STATE_BYTE(Frame(*) f) {
        assert(Not_Frame_Flag(f, ABRUPT_FAILURE));
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
    inline static REBVAL *FRM_ARG(Frame(*) f, REBLEN n) {
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


inline static Context(*) Context_For_Frame_May_Manage(Frame(*) f) {
    assert(not Is_Action_Frame_Fulfilling(f));
    SET_SERIES_FLAG(f->varlist, MANAGED);
    return CTX(f->varlist);
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

inline static void Get_Frame_Label_Or_Nulled(Cell(*) out, Frame(*) f) {
    assert(Is_Action_Frame(f));
    if (f->label)
        Init_Word(out, unwrap(f->label));  // WORD!, PATH!, or stored invoke
    else
        Init_Nulled(out);  // anonymous invocation
}

inline static const char* Frame_Label_Or_Anonymous_UTF8(Frame(*) f) {
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

inline static void Free_Frame_Internal(Frame(*) f) {
    if (Get_Frame_Flag(f, ALLOCATED_FEED))
        Free_Feed(f->feed);  // didn't inherit from parent, and not END_FRAME

    if (f->varlist and NOT_SERIES_FLAG(f->varlist, MANAGED))
        GC_Kill_Series(f->varlist);
    TRASH_POINTER_IF_DEBUG(f->varlist);

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));

    Free_Node(FRM_POOL, f);
}


inline static void Push_Frame(
    REBVAL *out,  // type check prohibits passing `unstable` cells for output
    Frame(*) f
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

    // For convenience, the frame output is always reset to a void if stale
    // values are not wanted.  The operation is very fast (just clears bits
    // out of the header) and this keeps callers from needing to worry about
    // it--or dealing with asserts that they hadn't done it.
    //
    if (Not_Frame_Flag(f, MAYBE_STALE))
        RESET(out);

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
    Frame(*) ftemp = TOP_FRAME;
    for (; ftemp != BOTTOM_FRAME; ftemp = ftemp->prior) {
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
        Get_Feed_Flag(f->feed, NEXT_ARG_FROM_OUT)
        and f->prior->feed == f->feed
    ){
        assert(f->out == f->prior->out);
    }

    assert(IS_POINTER_TRASH_DEBUG(f->alloc_value_list));
    f->alloc_value_list = f;  // doubly link list, terminates in `f`
}


inline static void UPDATE_EXPRESSION_START(Frame(*) f) {
    if (not FRM_IS_VARIADIC(f))
        f->expr_index = FRM_INDEX(f);
}


#define Literal_Next_In_Frame(out,f) \
    Literal_Next_In_Feed((out), (f)->feed)


inline static void Drop_Frame_Core(Frame(*) f) {
  #if DEBUG_EXPIRED_LOOKBACK
    free(f->stress);
  #endif

    assert(TG_Top_Frame == f);

    if (Is_Throwing(f) or (f->out and Is_Failure(f->out))) {
        //
        // On normal completion with a return result, we do not allow API
        // handles attached to a frame to leak--you are expected to release
        // everything.  But definitional failure and throw cases are exempt.
        //
        REBNOD *n = f->alloc_value_list;
        while (n != f) {
            Array(*) a = ARR(n);
            n = LINK(ApiNext, a);
            RESET(ARR_SINGLE(a));
            GC_Kill_Series(a);
        }
        TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

        // There could be outstanding values on the data stack, or data in the
        // mold buffer...we clean it up automatically in these cases.
        //
        Rollback_Globals_To_State(&f->baseline);
    }
    else {
      #if !defined(NDEBUG)
        REBNOD *n = f->alloc_value_list;
        while (n != f) {
            Array(*) a = ARR(n);
            printf("API handle was allocated but not freed, panic'ing leak\n");
            panic (a);
        }
        TRASH_POINTER_IF_DEBUG(f->alloc_value_list);
      #endif
    }

    TG_Top_Frame = f->prior;

    // Note: Free_Feed() will handle feeding a frame through to its end (which
    // may release handles/etc), so no requirement f->feed->value be at END.

    Free_Frame_Internal(f);
}

inline static void Drop_Frame_Unbalanced(Frame(*) f) {
    Drop_Frame_Core(f);
}

inline static void Drop_Frame(Frame(*) f)
{
    if (
        not Is_Throwing(f)
        and not (f->out and Is_Failure(f->out))
    ){
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
    }

    Drop_Frame_Unbalanced(f);
}


inline static Frame(*) Prep_Frame_Core(
    Frame(*) f,
    Feed(*) feed,
    Flags flags
){
   if (f == nullptr)  // e.g. a failed allocation
       fail (Error_No_Memory(sizeof(struct Reb_Frame)));

    f->flags.bits = flags | FRAME_FLAG_0_IS_TRUE | FRAME_FLAG_7_IS_TRUE;

    f->feed = feed;
    Prep_Void(&f->spare);
    TRASH_POINTER_IF_DEBUG(f->out);

    f->varlist = nullptr;
    f->executor = &Evaluator_Executor;  // compatible default (for now)

    TRASH_POINTER_IF_DEBUG(f->alloc_value_list);

    TRASH_IF_DEBUG(f->u);  // fills with garbage bytes in debug build

    TRASH_OPTION_IF_DEBUG(f->label);
  #if DEBUG_FRAME_LABELS
    TRASH_POINTER_IF_DEBUG(f->label_utf8);
  #endif

    // !!! Previously only the DSP was captured in f->baseline.dsp, but then
    // redundantly captured via a SNAP_STATE() in Push_Frame().  The
    // responsibilities of Prep_Frame() vs Push_Frame() aren't clearly laid
    // out, but some clients do depend on the DSP being captured before
    // Push_Frame() is called, so this snaps the whole baseline here.
    //
    SNAP_STATE(&f->baseline);  // see notes on `baseline` in Reb_Frame

  #if DEBUG_COUNT_TICKS
    f->tick = TG_Tick;
  #endif

    return f;
}

#define Make_Frame(feed,flags) \
    Prep_Frame_Core(cast(Frame(*), Alloc_Node(FRM_POOL)), (feed), (flags))

#define Make_Frame_At_Core(any_array,specifier,frame_flags) \
    Make_Frame( \
        Prep_Feed_At( \
            Alloc_Feed(), \
            (any_array), \
            (specifier), \
            TOP_FRAME->feed->flags.bits \
        ), \
        (frame_flags) | FRAME_FLAG_ALLOCATED_FEED \
    )

#define Make_Frame_At(any_array,flags) \
    Make_Frame_At_Core((any_array), SPECIFIED, (flags))

#define Make_End_Frame(flags) \
    Make_Frame(TG_End_Feed, (flags))


#define Begin_Enfix_Action(f,label) \
    Begin_Action_Core((f), (label), true)

#define Begin_Prefix_Action(f,label) \
    Begin_Action_Core((f), (label), false)


//=//// ARGUMENT AND PARAMETER ACCESS HELPERS ////=///////////////////////////
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Reb_Frame* passed to every
// DECLARE_NATIVE ('frame_') and read the information out cleanly, like this:
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

    #define SUBFRAME    (assert(TOP_FRAME->prior == frame_), TOP_FRAME)

    #define VOID        Native_Void_Result(frame_)
    #define NONE        Native_None_Result(frame_)
    #define THROWN      Native_Thrown_Result(frame_)
    #define FAIL(p)     Native_Failure_Result(frame_, (p))
    #define UNMETA(v)   Native_Unmeta_Result(frame_, (v))

    #define BASELINE   (&frame_->baseline)
#endif


// !!! Numbered arguments got more complicated with the idea of moving the
// definitional returns into the first slot (if applicable).  This makes it
// more important to use the named ARG() and REF() macros.  As a stopgap
// measure, we just sense whether the phase has a return or not.
//
inline static REBVAL *D_ARG_Core(Frame(*) f, REBLEN n) {  // 1 for first arg
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
inline static void FAIL_IF_BAD_RETURN_TYPE(Frame(*) f) {
    Action(*) phase = FRM_PHASE(f);
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
    Flags flags,
    Cell(const*) value,  // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
);

// Conveniences for returning a continuation.  The concept is that when a
// BOUNCE_CONTINUE comes back via the C `return` for a native, that native's
// C stack variables are all gone.  But the heap-allocated Rebol frame stays
// intact and in the Rebol stack trace.  It will be resumed when the
// continuation finishes.
//
// Conditional constructs allow branches that are either BLOCK!s or ACTION!s.
// If an action, the triggering condition is passed to it as an argument:
// https://trello.com/c/ay9rnjIe
//
// Allowing other values was deemed to do more harm than good:
// https://forum.rebol.info/t/backpedaling-on-non-block-branches/476
//
// !!! Review if @word, @pa/th, @tu.p.le would make good branch types.  :-/
//
//////////////////////////////////////////////////////////////////////////////
//
// 2. GET-GROUP! is handled here although it isn't in the ANY-BRANCH! typeset.
//    This is because some instances (like CASE) don't have this handled
//    automatically by a parameter convention, the way IF does.  To make it
//    easier for them, the GET-GROUP! type is allowed to act like GROUP!, to
//    save on having to transform the cell in hand to a plain GROUP!.
//
// 3. Things like CASE currently ask for a branch-based continuation on types
//    they haven't checked, but encounter via evaluation.  Hence we FAIL here
//    instead of panic()...but that suggests this should be narrowed to the
//    kinds of types branching permits.
//
// 4. If branch function argument isn't "meta" then we decay any isotopes.
//    Do the decay test first to avoid needing to scan parameters unless it's
//    one of those cases.
//
//    (The theory here is that we're not throwing away any safety, as the
//     isotopification process was usually just for the purposes of making the
//     branch trigger or not.  With that addressed, it's just inconvenient to
//     force functions to be meta to get things like NULL.)
//
//         if true [null] then x -> [
//             ;
//             ; Why would we want to have to make it ^x, when we know any
//             ; nulls that triggered the branch would have been isotopic?
//         ]
//
inline static bool Pushed_Continuation(
    REBVAL *out,
    Flags flags,  // FRAME_FLAG_BRANCH, etc. for pushed frames
    Cell(const*) branch,
    REBSPC *branch_specifier,
    const REBVAL *with  // can be same as out or not GC-safe, copied if needed
){
    assert(branch != out);  // it's legal for `with` to be the same as out

    if (IS_GROUP(branch) or IS_GET_GROUP(branch)) {  // see [2] for GET-GROUP!
        assert(flags & FRAME_FLAG_BRANCH);  // needed for trick
        assert(not (flags & FRAME_FLAG_MAYBE_STALE));  // OUT used as WITH
        Frame(*) grouper = Make_Frame_At_Core(
            branch,
            branch_specifier,
            FRAME_FLAG_MAYBE_STALE | (flags & (~ FRAME_FLAG_BRANCH))
        );
        grouper->executor = &Group_Branch_Executor;  // evaluates to get branch
        if (Is_End(with))
            Mark_Eval_Out_Stale(out);
        else if (Is_Void(with))
            RESET(out);
        else
            Copy_Cell(out, with);  // need lifetime preserved
        Push_Frame(out, grouper);
        return true;  // don't do pushed_continuation, may reset out
    }

    switch (VAL_TYPE(branch)) {
      case REB_BLANK:
        if (flags & FRAME_FLAG_BRANCH)
            Init_Null_Isotope(out);
        else
            Init_Nulled(out);
        goto just_use_out;

      case REB_QUOTED:
        Unquotify(Derelativize(out, branch, branch_specifier), 1);
        if (Is_Nulled(out) and (flags & FRAME_FLAG_BRANCH))
            Init_Null_Isotope(out);
        goto just_use_out;

      case REB_META_BLOCK:
      case REB_BLOCK: {
        Frame(*) f = Make_Frame_At_Core(branch, branch_specifier, flags);
        if (CELL_HEART_UNCHECKED(branch) == REB_META_BLOCK) {
            Set_Frame_Flag(f, META_RESULT);
            Set_Frame_Flag(f, FAILURE_RESULT_OK);
        }

        Push_Frame(out, f);
        goto pushed_continuation; }  // trampoline manages FRAME_FLAG_BRANCH atm.

      case REB_GET_BLOCK: {  // effectively REDUCE
        Frame(*) f = Make_End_Frame(FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING));

        const REBVAL *action = Lib(REDUCE);
        Push_Action(f, VAL_ACTION(action), VAL_ACTION_BINDING(action));
        Begin_Prefix_Action(f, VAL_ACTION_LABEL(action));

        const REBKEY *key = f->u.action.key;
        const REBPAR *param = f->u.action.param;
        REBVAL *arg = f->u.action.arg;
        for (; key != f->u.action.key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Init_None(arg);
        }

        arg = First_Unspecialized_Arg(&param, f);
        Derelativize(arg, branch, branch_specifier);
        mutable_HEART_BYTE(arg) = REB_BLOCK;  // :[1 + 2] => [3], not :[3]

        Push_Frame(out, f);
        goto pushed_continuation; }

      case REB_ACTION : {
        Frame(*) f = Make_End_Frame(
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
        );
        Push_Action(f, VAL_ACTION(branch), VAL_ACTION_BINDING(branch));
        Begin_Prefix_Action(f, VAL_ACTION_LABEL(branch));

        const REBKEY *key = f->u.action.key;
        const REBPAR *param = f->u.action.param;
        REBVAL *arg = f->u.action.arg;
        for (; key != f->u.action.key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Init_None(arg);
        }

        if (Not_End(with)) do {
            arg = First_Unspecialized_Arg(&param, f);
            if (not arg)
                break;

            if (Is_Void(with))
              { Init_Void_Isotope(arg); break; }

            Copy_Cell(arg, Pointer_To_Decayed(with));

            if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
              { Meta_Quotify(arg); break; }

            Decay_If_Isotope(arg);  // decay when normal parameter, see [4]

            if (Is_Isotope(arg))
                fail ("Can't pass isotope to non-META parameter");
        } while (0);

        Push_Frame(out, f);
        goto pushed_continuation; }

      case REB_FRAME: {
        if (IS_FRAME_PHASED(branch))  // see REDO for tail-call recursion
            fail ("Use REDO to restart a running FRAME! (not DO)");

        Context(*) c = VAL_CONTEXT(branch);  // checks for INACCESSIBLE

        if (Get_Subclass_Flag(VARLIST, CTX_VARLIST(c), FRAME_HAS_BEEN_INVOKED))
            fail (Error_Stale_Frame_Raw());

        Frame(*) f = Make_End_Frame(
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
        );
        f->executor = &Action_Executor;  // usually done by Push_Action()s

        Array(*) varlist = CTX_VARLIST(c);
        f->varlist = varlist;
        f->rootvar = CTX_ROOTVAR(c);
        INIT_BONUS_KEYSOURCE(varlist, f);

        assert(FRM_PHASE(f) == CTX_FRAME_ACTION(c));
        INIT_FRM_BINDING(f, VAL_FRAME_BINDING(branch));

        Begin_Prefix_Action(f, VAL_FRAME_LABEL(branch));

        Push_Frame(out, f);
        goto pushed_continuation; }

      default:
        break;
    }

    fail (Error_Bad_Branch_Type_Raw());  // narrow input types? see [3]

  pushed_continuation:
    return true;

  just_use_out:
    return false;
}


//=//// CONTINUATION HELPER MACROS ////////////////////////////////////////=//
//
// Normal continuations come in catching and non-catching forms; they evaluate
// without tampering with the result.
//
// Branch continuations enforce the result not being pure null or void.

#define continue_core(o,flags,branch,specifier,with) \
    do { \
        Pushed_Continuation((o), (flags), (branch), (specifier), (with)); \
        /* don't heed result, because callback needed frame or not */ \
        return BOUNCE_CONTINUE; \
    } while (0)

#define continue_uncatchable(o,value,with) \
    continue_core((o), FRAME_MASK_NONE, (value), SPECIFIED, (with))

#define continue_catchable(o,value,with) \
    do { \
        Set_Executor_Flag(ACTION, frame_, DISPATCHER_CATCHES); \
        continue_core((o), FRAME_MASK_NONE, (value), SPECIFIED, (with)); \
    } while (0)

#define continue_uncatchable_branch(o,branch,with) \
    continue_core((o), FRAME_FLAG_BRANCH, (branch), SPECIFIED, (with))

#define continue_catchable_branch(o,branch,with) \
    do { \
        Set_Executor_Flag(ACTION, frame_, DISPATCHER_CATCHES); \
        continue_core((o), FRAME_FLAG_BRANCH, (branch), SPECIFIED, (with)); \
    } while (0)


inline static Bounce Continue_Subframe_Helper(
    Frame(*) f,
    bool must_be_dispatcher,
    Flags catches_flag,
    Frame(*) sub
){
    if (must_be_dispatcher)
        assert(Is_Action_Frame(f) and not Is_Action_Frame_Fulfilling(f));
    else
        assert(not Is_Action_Frame(f) or Is_Action_Frame_Fulfilling(f));

    if (catches_flag != 0) {
        assert(catches_flag == ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES);
        f->flags.bits |= catches_flag;
    }

    assert(sub == TOP_FRAME);  // currently subframe must be pushed and top frame
    UNUSED(sub);
    return BOUNCE_CONTINUE;
}

#define continue_subframe(sub) \
    return Continue_Subframe_Helper(frame_, false, 0, (sub))

#define continue_catchable_subframe(sub) \
    return Continue_Subframe_Helper( \
        frame_, true, ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES, (sub))

#define continue_uncatchable_subframe(sub) \
    return Continue_Subframe_Helper(frame_, true, 0, (sub))


//=//// DELEGATION HELPER MACROS ///////////////////////////////////////////=//
//
// Delegation is when a frame wants to hand over the work to do to another
// frame, and not receive any further callbacks.  This gives the opportunity
// for an optimization to not go through with a continuation at all and just
// use the output if it is simple to do.
//
// !!! Delegation doesn't want to use the frame that's pushed.  It leaves it
// on the stack for sanity of debug tracing, but it could be more optimal
// if the delegating frame were freed before running what's underneath it...
// at least it could be collapsed into a more primordial state.  Review.

#define delegate_core(o,fs,branch,specifier,with) \
    do { \
        assert((o) == frame_->out); \
        if (Pushed_Continuation( \
            frame_->out, \
            (fs) | (frame_->flags.bits & FRAME_FLAG_FAILURE_RESULT_OK), \
            (branch), \
            (specifier), \
            (with) \
        )){ \
            return BOUNCE_DELEGATE; \
        } \
        return frame_->out; /* no need to give callback to delegator */ \
    } while (0)


#define delegate(o,value,with) \
    delegate_core(frame_->out, FRAME_MASK_NONE, (value), SPECIFIED, (with))

#define delegate_branch(o,branch,with) \
    delegate_core(frame_->out, FRAME_FLAG_BRANCH, (branch), SPECIFIED, (with))

#define delegate_maybe_stale(o,branch,with) \
    delegate_core( \
        frame_->out, FRAME_FLAG_MAYBE_STALE, (branch), SPECIFIED, (with) \
    )


#define delegate_subframe(sub) \
    do { \
        Continue_Subframe_Helper(frame_, true, 0, (sub)); \
        return BOUNCE_DELEGATE; \
    } while (0)
