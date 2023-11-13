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
    // !!! Note: GET-BLOCK! is earmarked for isotope generation.
    //
    //     >> append [a b c] :[d e]
    //     == [a b c d e]
    //
    // Makes more sense than "escaping a block", whatever that would be.
    //
    return IS_GET_GROUP(v) or IS_GET_WORD(v)
        or IS_GET_PATH(v) or IS_GET_TUPLE(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LEVEL ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


#define Is_Action_Level(L) \
    ((L)->executor == &Action_Executor)


inline static bool Level_Is_Variadic(Level(*) L) {
    return FEED_IS_VARIADIC(L->feed);
}

inline static Array(const*) Level_Array(Level(*) L) {
    assert(not Level_Is_Variadic(L));
    return FEED_ARRAY(L->feed);
}

#define Level_Specifier(L) \
    FEED_SPECIFIER(ensure(Level(*), (L))->feed)


// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `option(head)` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
inline static REBLEN Level_Array_Index(Level(*) L) {
    if (Is_Feed_At_End(L->feed))
        return ARR_LEN(Level_Array(L));

    assert(not Level_Is_Variadic(L));
    return FEED_INDEX(L->feed) - 1;
}

inline static REBLEN Level_Expression_Index(Level(*) L) {
    assert(not Level_Is_Variadic(L));
    return L->expr_index - 1;
}

inline static String(const*) File_Of_Level(Level(*) L) {
    if (Level_Is_Variadic(L))
        return nullptr;
    if (Not_Subclass_Flag(ARRAY, Level_Array(L), HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return LINK(Filename, Level_Array(L));
}

inline static const char* File_UTF8_Of_Level(Level(*) L) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(ANONYMOUS).
    //
    String(const*) str = File_Of_Level(L);
    return str ? STR_UTF8(str) : "~anonymous~";
}

inline static LineNumber LineNumber_Of_Level(Level(*) L) {
    if (Level_Is_Variadic(L))
        return 0;
    if (Not_Subclass_Flag(ARRAY, Level_Array(L), HAS_FILE_LINE_UNMASKED))
        return 0;
    return Level_Array(L)->misc.line;
}


// Note about Level_Num_Args: A native should generally not detect the arity it
// was invoked with, (and it doesn't make sense as most implementations get
// the full list of arguments and refinements).  However, ACTION! dispatch
// has several different argument counts piping through a switch, and often
// "cheats" by using the arity instead of being conditional on which action
// ID ran.  Consider when reviewing the future of ACTION!.
//
#define Level_Num_Args(L) \
    (cast(REBSER*, (L)->varlist)->content.dynamic.used - 1)  // minus rootvar

#define Level_Spare(L) \
    cast(Atom(*), &(L)->spare)


// The "phase" slot of a FRAME! value is the second node pointer in PAYLOAD().
// If a frame value is non-archetypal, this slot may be occupied by a String(*)
// which represents the cached name of the action from which the frame
// was created.  This FRAME! value is archetypal, however...which never holds
// such a cache.  For performance (even in the debug build, where this is
// called *a lot*) this is a macro and is unchecked.
//
#define Level_Phase(L) \
    cast(Phase(*), VAL_FRAME_PHASE_OR_LABEL_NODE((L)->rootvar))

inline static void INIT_LVL_PHASE(Level(*) L, Phase(*) phase)  // check types
  { INIT_VAL_FRAME_PHASE_OR_LABEL(L->rootvar, phase); }  // ...only

inline static void INIT_LVL_BINDING(Level(*) L, Context(*) binding)
  { mutable_BINDING(L->rootvar) = binding; }  // also fast

#define Level_Binding(L) \
    cast(Context(*), BINDING((L)->rootvar))

inline static option(Symbol(const*)) Level_Label(Level(*) L) {
    assert(Is_Action_Level(L));
    return L->label;
}


#if (! CPLUSPLUS_11)
    #define Level_State_Byte(L) \
        mutable_SECOND_BYTE((L)->flags)
#else
    // Having a special accessor in the C++ build serves two purposes.  One,
    // it can actually type check that `L` is a level.  But secondly, it also
    // is a good place to inject an assertion that you're not ignoring the
    // fact that a level "self-errored" and was notified of an abrupt failure.
    //
    inline static Byte& Level_State_Byte(Level(*) L) {
        assert(Not_Level_Flag(L, ABRUPT_FAILURE));
        return mutable_SECOND_BYTE(L->flags);
    }
#endif



// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define Level_Args_Head(L) \
    ((L)->rootvar + 1)

#ifdef NDEBUG
    #define Level_Arg(L,n) \
        ((L)->rootvar + (n))
#else
    inline static REBVAL *Level_Arg(Level(*) L, REBLEN n) {
        assert(n != 0 and n <= Level_Num_Args(L));
        return L->rootvar + n;  // 1-indexed
    }
#endif


#define At_Level(L)                 At_Feed((L)->feed)
#define Try_At_Level(L)             Try_At_Feed((L)->feed)
#define Is_Level_At_End(L)          Is_Feed_At_End((L)->feed)
#define Not_Level_At_End(L)         Not_Feed_At_End((L)->feed)


inline static Context(*) Context_For_Level_May_Manage(Level(*) L) {
    assert(not Is_Level_Fulfilling(L));
    SET_SERIES_FLAG(L->varlist, MANAGED);
    return CTX(L->varlist);
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

inline static void Get_Level_Label_Or_Nulled(Sink(Value(*)) out, Level(*) L) {
    assert(Is_Action_Level(L));
    if (L->label)
        Init_Word(out, unwrap(L->label));  // WORD!, PATH!, or stored invoke
    else
        Init_Nulled(out);  // anonymous invocation
}

inline static const char* Level_Label_Or_Anonymous_UTF8(Level(*) L) {
    assert(Is_Action_Level(L));
    if (L->label)
        return STR_UTF8(unwrap(L->label));
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
// up a stack level (L), and threading that level's state through successive
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
// L->value with a valid "fetched" REBVAL*.
//
// This privileged level of access can be used by natives that feel they can
// optimize performance by working with the evaluator directly.

inline static void Free_Level_Internal(Level(*) L) {
    if (Get_Level_Flag(L, ALLOCATED_FEED))
        Free_Feed(L->feed);  // didn't inherit from parent, and not END_FRAME

    if (L->varlist and NOT_SERIES_FLAG(L->varlist, MANAGED))
        GC_Kill_Series(L->varlist);
    TRASH_POINTER_IF_DEBUG(L->varlist);

    assert(IS_POINTER_TRASH_DEBUG(L->alloc_value_list));

    Free_Pooled(LEVEL_POOL, L);
}

// * Push_Level() takes an Atom() for the output.  This is important, as
//   we don't want to evaluate into arbitrary array Cell(*), since the array
//   could have its memory moved during an evaluation.  Also we don't want
//   to take a Value(*) that could be a variable in an object--because the
//   unstable intermediate states of the evaluation could be exposed by
//   an object (this applies to the ARG() of the function too, as these could
//   be seen by debugging code).  So typically evaluations are done into the
//   OUT or SPARE cells (or SCRATCH if in the evaluator).  Note that a
//   special exception is made by LOCAL() in frames, based on the belief
//   that local state for a native will never be exposed by a debugger.
//
inline static void Push_Level(
    Atom(*) out,  // typecheck prohibits passing `unstable` Cell(*) for output
    Level(*) L
){
    // All calls through to Eval_Core() are assumed to happen at the same C
    // stack level for a pushed Level (though this is not currently enforced).
    // Hence it's sufficient to check for C stack overflow only once, e.g.
    // not on each Eval_Step() for `reduce [a | b | ... | z]`.
    //
    // !!! This method is being replaced by "stackless", as there is no
    // reliable platform independent method for detecting stack overflows.
    //
    if (C_STACK_OVERFLOWING(&L)) {
        Free_Level_Internal(L);  // not in stack, feed + level wouldn't free
        Fail_Stack_Overflow();
    }

    // Levels are pushed to reuse for several sequential operations like
    // ANY, ALL, CASE, REDUCE.  It is allowed to change the output cell for
    // each evaluation.  But the GC expects initialized bits in the output
    // slot at all times; use null until first eval call if needed
    //
    L->out = out;

  #if DEBUG_EXPIRED_LOOKBACK
    L->stress = nullptr;
  #endif

  #if !defined(NDEBUG)
    //
    // !!! TBD: the relevant file/line update when L->feed->array changes
    //
    L->file = File_UTF8_Of_Level(L);
    L->line = LineNumber_Of_Level(L);
  #endif

    L->prior = TG_Top_Level;
    TG_Top_Level = L;

    assert(IS_POINTER_TRASH_DEBUG(L->alloc_value_list));
    L->alloc_value_list = L;  // doubly link list, terminates in `L`
}


inline static void UPDATE_EXPRESSION_START(Level(*) L) {
    if (not Level_Is_Variadic(L))
        L->expr_index = Level_Array_Index(L);
}


inline static void Drop_Level_Core(Level(*) L) {
  #if DEBUG_EXPIRED_LOOKBACK
    free(L->stress);
  #endif

    assert(TG_Top_Level == L);

    if (Is_Throwing(L) or (L->out and Is_Raised(L->out))) {
        //
        // On normal completion with a return result, we do not allow API
        // handles attached to a level to leak--you are expected to release
        // everything.  But definitional failure and throw cases are exempt.
        //
        Node* n = L->alloc_value_list;
        while (n != L) {
            Raw_Array* a = ARR(n);
            n = LINK(ApiNext, a);
            FRESHEN(ARR_SINGLE(a));
            GC_Kill_Series(a);
        }
        TRASH_POINTER_IF_DEBUG(L->alloc_value_list);

        // There could be outstanding values on the data stack, or data in the
        // mold buffer...we clean it up automatically in these cases.
        //
        Rollback_Globals_To_State(&L->baseline);
    }
    else {
      #if !defined(NDEBUG)
        Node* n = L->alloc_value_list;
        while (n != L) {
            Raw_Array* a = ARR(n);
            printf("API handle was allocated but not freed, panic'ing leak\n");
            panic (a);
        }
        TRASH_POINTER_IF_DEBUG(L->alloc_value_list);
      #endif
    }

    TG_Top_Level = L->prior;

    // Note: Free_Feed() will handle feeding a feed through to its end (which
    // may release handles/etc), so no requirement Level_At(L) be at END.

    Free_Level_Internal(L);
}

inline static void Drop_Level_Unbalanced(Level(*) L) {
    Drop_Level_Core(L);
}

inline static void Drop_Level(Level(*) L)
{
    if (
        not Is_Throwing(L)
        and not (L->out and Is_Raised(L->out))
    ){
      #if DEBUG_BALANCE_STATE
        //
        // To avoid slowing down debug builds, Eval_Core() doesn't check this
        // every cycle, just on drop.  But if it's hard to find the cycle
        // causing problems, see BALANCE_CHECK_EVERY_EVALUATION_STEP.
        //
        ASSERT_STATE_BALANCED(&L->baseline);
      #else
        assert(TOP_INDEX == L->baseline.stack_base);  // Cheaper check
      #endif
    }

    Drop_Level_Unbalanced(L);
}


inline static Level(*) Prep_Level_Core(
    Level(*) L,
    Feed(*) feed,
    Flags flags
){
   if (L == nullptr)  // e.g. a failed allocation
       fail (Error_No_Memory(sizeof(struct Reb_Level)));

    L->flags.bits = flags | LEVEL_FLAG_0_IS_TRUE | LEVEL_FLAG_7_IS_TRUE;

    L->feed = feed;
    Erase_Cell(&L->spare);
    TRASH_POINTER_IF_DEBUG(L->out);

    L->varlist = nullptr;
    L->executor = &Evaluator_Executor;  // compatible default (for now)

    TRASH_POINTER_IF_DEBUG(L->alloc_value_list);

    TRASH_IF_DEBUG(L->u);  // fills with garbage bytes in debug build

    // !!! Recycling is done in the trampoline before the level gets a chance
    // to run.  So it's hard for the GC to know if it's okay to mark the
    // scratch cell.  We cheaply erase the cell in case it stays as the
    // evaluator executor (it's just writing a single zero).  Review.
    //
    Erase_Cell(&L->u.eval.scratch);

    TRASH_POINTER_IF_DEBUG(L->label);
  #if DEBUG_LEVEL_LABELS
    TRASH_POINTER_IF_DEBUG(L->label_utf8);
  #endif

    // !!! Previously just TOP_STACK was captured in L->baseline.stack_base,
    // but then redundantly captured via a SNAP_STATE() in Push_Level().  The
    // responsibilities of Prep_Level() vs Push_Level() aren't clearly laid
    // out, but some clients do depend on the StackIndex being captured before
    // Push_Level() is called, so this snaps the whole baseline here.
    //
    SNAP_STATE(&L->baseline);  // see notes on `baseline` in Reb_Level

  #if DEBUG_COUNT_TICKS
    L->tick = TG_tick;
  #endif

    return L;
}

#define Make_Level(feed,flags) \
    Prep_Level_Core(cast(Level(*), Alloc_Pooled(LEVEL_POOL)), (feed), (flags))

#define Make_Level_At_Core(any_array,specifier,level_flags) \
    Make_Level( \
        Prep_At_Feed( \
            Alloc_Feed(), \
            (any_array), \
            (specifier), \
            TOP_LEVEL->feed->flags.bits \
        ), \
        (level_flags) | LEVEL_FLAG_ALLOCATED_FEED \
    )

#define Make_Level_At(any_array,flags) \
    Make_Level_At_Core((any_array), SPECIFIED, (flags))

#define Make_End_Level(flags) \
    Make_Level(TG_End_Feed, (flags))


#define Begin_Enfix_Action(L,label) \
    Begin_Action_Core((L), (label), true)

#define Begin_Prefix_Action(L,label) \
    Begin_Action_Core((L), (label), false)


//=//// ARGUMENT AND PARAMETER ACCESS HELPERS ////=///////////////////////////
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Level(*) passed to every
// DECLARE_NATIVE ('level_') and read the information out cleanly, like this:
//
//     DECLARE_PARAM(1, foo);
//     DECLARE_PARAM(2, bar);
//
//     if (IS_INTEGER(ARG(foo)) and REF(bar)) { ... }
//
// The DECLARE_PARAM macro uses token pasting to name indexes being declared as
// `p_name` instead of just `name`.  This prevents collisions with C/C++
// identifiers, so DECLARE_PARAM(case) and DECLARE_PARAM(new) would make
// `p_case` and `p_new` instead of just `case` and `new` as the variable names.
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
// parameter or refinement, e.g. with `PARAM(foo)` or `PARAM(bar)`.

#define DECLARE_PARAM(n,name) \
    static const int p_##name##_ = n

#define ARG(name) \
    Level_Arg(level_, (p_##name##_))

#define LOCAL(name) \
    cast(Atom(*), ARG(name))  // see Push_Level() for why this is allowed

#define PARAM(name) \
    ACT_PARAM(Level_Phase(level_), (p_##name##_))  // a TYPESET!

#define PARAM_SYMBOL(name) \
    KEY_SYMBOL(ACT_KEY(Level_Phase(level_), (p_##name##_)))

#define REF(name) \
    (not Is_Nulled(ARG(name)))


// Quick access functions from natives (or compatible functions that name a
// Level(*) pointer `level_`) to get some of the common public fields.
//
// There is an option to not define them due to conflicts with OUT as defined
// by the Windows.h headers.  This makes it easier for people who don't want
// to #undef the Windows versions and would rather pick their own shorthands,
// (if any).
//
#if REBOL_LEVEL_SHORTHAND_MACROS
    #define LEVEL   level_
    #define OUT     level_->out         // GC-safe slot for output value
    #define SPARE   Level_Spare(level_)       // scratch GC-safe cell
    #define STATE   Level_State_Byte(level_)
    #define PHASE   Level_Phase(level_)

    #define stable_SPARE            Stable_Unchecked(SPARE)
    #define stable_OUT              Stable_Unchecked(OUT)

    #define SUBLEVEL    (assert(TOP_LEVEL->prior == level_), TOP_LEVEL)

    #define STACK_BASE \
        (assert(Is_Action_Level(level_)), level_->u.action.dispatcher_base)

    #define VOID        Native_Void_Result_Untracked(TRACK(OUT), level_)
    #define NONE        (Native_None_Result_Untracked(TRACK(OUT), level_))
    #define THROWN      Native_Thrown_Result(level_)
    #define COPY(v)     (Native_Copy_Result_Untracked(TRACK(OUT), level_, (v)))
    #define RAISE(p)    Native_Raised_Result(level_, (p))
    #define UNMETA(v)   Native_Unmeta_Result(level_, (v))
    #define BRANCHED(v) Native_Branched_Result(level_, (v))

    // `fail (UNHANDLED)` is a shorthand for something that's written often
    // enough in REBTYPE() handlers that it seems worthwhile.
    //
    #define UNHANDLED   Error_Cannot_Use(verb, D_ARG(1))

    #define BASELINE   (&level_->baseline)
#endif

#define Proxy_Multi_Returns(L) \
    Proxy_Multi_Returns_Core((L), level_->out)



// !!! Numbered arguments got more complicated with the idea of moving the
// definitional returns into the first slot (if applicable).  This makes it
// more important to use the named ARG() and REF() macros.  As a stopgap
// measure, we just sense whether the phase has a return or not.
//
inline static REBVAL *D_ARG_Core(Level(*) L, REBLEN n) {  // 1 for first arg
    REBPAR *param = ACT_PARAMS_HEAD(Level_Phase(L));
    REBVAL *arg = Level_Arg(L, 1);
    while (
        Is_Specialized(param)  // e.g. slots for saving multi-return variables
        or VAL_PARAM_CLASS(param) == PARAM_CLASS_RETURN
        or VAL_PARAM_CLASS(param) == PARAM_CLASS_OUTPUT
    ){
        ++param;
        ++arg;
    }
    return arg + n - 1;
}
#define D_ARG(n) \
    D_ARG_Core(level_, (n))


inline static bool Eval_Value_Core_Throws(
    Atom(*) out,
    Flags flags,
    Cell(const*) value,  // e.g. a BLOCK! here would just evaluate to itself!
    REBSPC *specifier
);

enum {
    ST_GROUP_BRANCH_ENTRY_DONT_ERASE_OUT = 1,  // STATE_0 erases OUT
    ST_GROUP_BRANCH_RUNNING_GROUP
};

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
    Atom(*) out,
    Flags flags,  // LEVEL_FLAG_BRANCH, etc. for pushed levels
    REBSPC *branch_specifier,  // before branch forces non-empty variadic call
    Cell(const*) branch,
    option(Atom(const*)) with  // can be same as out or not GC-safe, may copy
){
    assert(branch != out);  // it's legal for `with` to be the same as out
    assert(not with or unwrap(with) == out or not Is_Api_Value(unwrap(with)));

    if (Is_Activation(branch))
        goto handle_action;

    if (IS_GROUP(branch) or IS_GET_GROUP(branch)) {  // see [2] for GET-GROUP!
        assert(flags & LEVEL_FLAG_BRANCH);  // needed for trick
        Level(*) grouper = Make_Level_At_Core(
            branch,
            branch_specifier,
            (flags & (~ LEVEL_FLAG_BRANCH))
                | FLAG_STATE_BYTE(ST_GROUP_BRANCH_ENTRY_DONT_ERASE_OUT)
        );
        grouper->executor = &Group_Branch_Executor;  // evaluates to get branch
        if (with == nullptr)
            FRESHEN(out);
        else
            Copy_Cell(out, unwrap(with));  // need lifetime preserved
        Push_Level(out, grouper);
        goto pushed_continuation;
    }

    switch (VAL_TYPE(branch)) {
      case REB_BLANK:
        if (flags & LEVEL_FLAG_BRANCH)
            Init_Heavy_Null(out);
        else
            Init_Nulled(out);
        goto just_use_out;

      case REB_QUOTED:
        Unquotify(Derelativize(out, branch, branch_specifier), 1);
        if (Is_Nulled(out) and (flags & LEVEL_FLAG_BRANCH))
            Init_Heavy_Null(out);
        goto just_use_out;

      case REB_META_BLOCK:
      case REB_BLOCK: {
        Level(*) L = Make_Level_At_Core(branch, branch_specifier, flags);
        if (CELL_HEART_UNCHECKED(branch) == REB_META_BLOCK) {
            Set_Level_Flag(L, META_RESULT);
            Set_Level_Flag(L, FAILURE_RESULT_OK);
        }
        L->executor = &Array_Executor;

        Push_Level(out, L);
        goto pushed_continuation; }  // trampoline handles LEVEL_FLAG_BRANCH

      case REB_GET_BLOCK: {  // effectively REDUCE
        Level(*) L = Make_End_Level(FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING));

        const REBVAL *action = Lib(REDUCE);
        Push_Action(L, VAL_ACTION(action), VAL_FRAME_BINDING(action));
        Begin_Prefix_Action(L, VAL_FRAME_LABEL(action));

        const REBKEY *key = L->u.action.key;
        const REBPAR *param = L->u.action.param;
        Atom(*) arg = L->u.action.arg;
        for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Finalize_None(arg);
        }

        arg = First_Unspecialized_Arg(&param, L);
        Derelativize(arg, branch, branch_specifier);
        mutable_HEART_BYTE(arg) = REB_BLOCK;  // :[1 + 2] => [3], not :[3]

        Push_Level(out, L);
        goto pushed_continuation; }

    handle_action: {
        Level(*) L = Make_End_Level(
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
        );
        Push_Action(L, VAL_ACTION(branch), VAL_FRAME_BINDING(branch));
        Begin_Prefix_Action(L, VAL_FRAME_LABEL(branch));

        const REBKEY *key = L->u.action.key;
        const REBPAR *param = L->u.action.param;
        Atom(*) arg = L->u.action.arg;
        for (; key != L->u.action.key_tail; ++key, ++param, ++arg) {
            if (Is_Specialized(param))
                Copy_Cell(arg, param);
            else
                Finalize_None(arg);
            assert(Is_Stable(arg));
        }

        if (with) do {
            arg = First_Unspecialized_Arg(&param, L);
            if (not arg)
                break;

            Copy_Cell(arg, unwrap(with));  // do not decay, see [4]

            if (VAL_PARAM_CLASS(param) == PARAM_CLASS_META)
                Meta_Quotify(arg);
            else
                Decay_If_Unstable(arg);
            break;
        } while (0);

        Push_Level(out, L);
        goto pushed_continuation; }

      case REB_FRAME: {
        if (Is_Frame_Details(branch))
            goto handle_action;

        if (IS_FRAME_PHASED(branch))  // see REDO for tail-call recursion
            fail ("Use REDO to restart a running FRAME! (not DO)");

        Context(*) c = VAL_CONTEXT(branch);  // checks for INACCESSIBLE

        if (Get_Subclass_Flag(VARLIST, CTX_VARLIST(c), FRAME_HAS_BEEN_INVOKED))
            fail (Error_Stale_Frame_Raw());

        Level(*) L = Make_End_Level(
            FLAG_STATE_BYTE(ST_ACTION_TYPECHECKING) | flags
        );
        L->executor = &Action_Executor;  // usually done by Push_Action()s

        Array(*) varlist = CTX_VARLIST(c);
        L->varlist = varlist;
        L->rootvar = CTX_ROOTVAR(c);
        INIT_BONUS_KEYSOURCE(varlist, L);

        assert(Level_Phase(L) == CTX_FRAME_PHASE(c));
        INIT_LVL_BINDING(L, VAL_FRAME_BINDING(branch));

        L->u.action.original = Level_Phase(L);

        Begin_Prefix_Action(L, VAL_FRAME_LABEL(branch));

        Push_Level(out, L);
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
//
// Uses variadic method to allow you to supply an argument to be passed to
// a branch continuation if it is a function.
//

#define CONTINUE_CORE_5(...) ( \
    Pushed_Continuation(__VA_ARGS__), \
    BOUNCE_CONTINUE)  /* ^-- don't heed result: want callback, push or not */

#define CONTINUE_CORE_4(...) ( \
    Pushed_Continuation(__VA_ARGS__, nullptr), \
    BOUNCE_CONTINUE)  /* ^-- don't heed result: want callback, push or not */

#define CONTINUE_CORE(...) \
    PP_CONCAT(CONTINUE_CORE_, PP_NARGS(__VA_ARGS__))(__VA_ARGS__)

#define CONTINUE(out,...) \
    CONTINUE_CORE((out), LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__)

#define CATCH_CONTINUE(out,...) ( \
    Set_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES), \
    CONTINUE_CORE((out), LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__))

#define CONTINUE_BRANCH(out,...) \
    CONTINUE_CORE((out), LEVEL_FLAG_BRANCH, SPECIFIED, __VA_ARGS__)

#define CATCH_CONTINUE_BRANCH(out,...) ( \
    Set_Executor_Flag(ACTION, level_, DISPATCHER_CATCHES), \
    CONTINUE_CORE((out), LEVEL_FLAG_BRANCH, SPECIFIED, __VA_ARGS__))

inline static Bounce Continue_Sublevel_Helper(
    Level(*) L,
    bool catches,
    Level(*) sub
){
    if (catches) {  // all executors catch, but action may or may not delegate
        if (Is_Action_Level(L) and not Is_Level_Fulfilling(L))
            L->flags.bits |= ACTION_EXECUTOR_FLAG_DISPATCHER_CATCHES;
    }
    else {  // Only Action_Executor() can let dispatchers avoid catching
        assert(Is_Action_Level(L) and not Is_Level_Fulfilling(L));
    }

    assert(sub == TOP_LEVEL);  // currently sub must be pushed & top level
    UNUSED(sub);
    return BOUNCE_CONTINUE;
}

#define CATCH_CONTINUE_SUBLEVEL(sub) \
    Continue_Sublevel_Helper(level_, true, (sub))

#define CONTINUE_SUBLEVEL(sub) \
    Continue_Sublevel_Helper(level_, false, (sub))


//=//// DELEGATION HELPER MACROS ///////////////////////////////////////////=//
//
// Delegation is when a level wants to hand over the work to do to another
// level, and not receive any further callbacks.  This gives the opportunity
// for an optimization to not go through with a continuation at all and just
// use the output if it is simple to do.
//
// !!! Delegation doesn't want to use the old level it had.  It leaves it
// on the stack for sanity of debug tracing, but it could be more optimal
// if the delegating level were freed before running what's underneath it...
// at least it could be collapsed into a more primordial state.  Review.

#define DELEGATE_CORE_3(o,sub_flags,...) ( \
    assert((o) == level_->out), \
    Pushed_Continuation( \
        level_->out, \
        (sub_flags) | (level_->flags.bits & LEVEL_FLAG_FAILURE_RESULT_OK), \
        __VA_ARGS__  /* branch_specifier, branch, and "with" argument */ \
    ) ? BOUNCE_DELEGATE \
        : level_->out)  // no need to give callback to delegator

#define DELEGATE_CORE_2(out,sub_flags,...) \
    DELEGATE_CORE_3((out), (sub_flags), __VA_ARGS__, nullptr)

#define DELEGATE_CORE(out,sub_flags,...) \
    PP_CONCAT(DELEGATE_CORE_, PP_NARGS(__VA_ARGS__))( \
        (out), (sub_flags), __VA_ARGS__)

#define DELEGATE(out,...) \
    DELEGATE_CORE((out), LEVEL_MASK_NONE, SPECIFIED, __VA_ARGS__)

#define DELEGATE_BRANCH(out,...) \
    DELEGATE_CORE((out), LEVEL_FLAG_BRANCH, SPECIFIED, __VA_ARGS__)

#define DELEGATE_SUBLEVEL(sub) ( \
    Continue_Sublevel_Helper(level_, false, (sub)), \
    BOUNCE_DELEGATE)
