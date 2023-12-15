//
//  File: %sys-level.h
//  Summary: {Accessors and Argument Pushers/Poppers for Trampoline Levels}
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
INLINE bool ANY_ESCAPABLE_GET(const Cell* v) {
    //
    // !!! Note: GET-BLOCK! is earmarked for isotope generation.
    //
    //     >> append [a b c] :[d e]
    //     == [a b c d e]
    //
    // Makes more sense than "escaping a block", whatever that would be.
    //
    return Is_Get_Group(v) or Is_Get_Word(v)
        or Is_Get_Path(v) or Is_Get_Tuple(v);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LEVEL ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


#define Is_Action_Level(L) \
    ((L)->executor == &Action_Executor)


INLINE bool Level_Is_Variadic(Level* L) {
    return FEED_IS_VARIADIC(L->feed);
}

INLINE const Array* Level_Array(Level* L) {
    assert(not Level_Is_Variadic(L));
    return FEED_ARRAY(L->feed);
}

#define Level_Specifier(L) \
    FEED_SPECIFIER(ensure(Level*, (L))->feed)


// !!! Though the evaluator saves its `index`, the index is not meaningful
// in a valist.  Also, if `Option(head)` values are used to prefetch before an
// array, those will be lost too.  A true debugging mode would need to
// convert these cases to ordinary arrays before running them, in order
// to accurately present any errors.
//
INLINE REBLEN Level_Array_Index(Level* L) {
    if (Is_Feed_At_End(L->feed))
        return Array_Len(Level_Array(L));

    assert(not Level_Is_Variadic(L));
    return FEED_INDEX(L->feed) - 1;
}

INLINE REBLEN Level_Expression_Index(Level* L) {
    assert(not Level_Is_Variadic(L));
    return L->expr_index - 1;
}

INLINE const String* File_Of_Level(Level* L) {
    if (Level_Is_Variadic(L))
        return nullptr;
    if (Not_Array_Flag(Level_Array(L), HAS_FILE_LINE_UNMASKED))
        return nullptr;
    return LINK(Filename, Level_Array(L));
}

INLINE const char* File_UTF8_Of_Level(Level* L) {
    //
    // !!! Note: Too early in boot at the moment to use Canon(ANONYMOUS).
    //
    Option(const String*) str = File_Of_Level(L);
    return str ? String_UTF8(unwrap(str)) : "~anonymous~";
}

INLINE LineNumber LineNumber_Of_Level(Level* L) {
    if (Level_Is_Variadic(L))
        return 0;
    if (Not_Array_Flag(Level_Array(L), HAS_FILE_LINE_UNMASKED))
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
    ((L)->varlist->content.dynamic.used - 1)  // minus rootvar

#define Level_Spare(L) \
    u_cast(Atom(*), &(L)->spare)


// A level's varlist is unmanaged by default, because when running code like
// a native there's typically no way to get access to the frame directly.
// e.g. no variables are getting bound to the native's arguments, because
// it's just C code running.
//
// However, things like usermode FUNC do hand out their frame variables.  But
// they can't assert that the varlist hasn't already been managed, because
// something like ADAPT may come along and manage the varlist first.  So this
// macro captures the intent and provides a place to read this comment.
//
// Note we don't use Set_Series_Managed() here, because the varlist was never
// put in the "untracked manuals" list... created as unmanaged/untracked.
//
#define Force_Level_Varlist_Managed(L) \
    Set_Node_Managed_Bit((L)->varlist)


// The "phase" slot of a FRAME! value is the second node pointer in PAYLOAD().
// If a frame value is non-archetypal, this slot may be occupied by a String*
// which represents the cached name of the action from which the frame
// was created.  This FRAME! value is archetypal, however...which never holds
// such a cache.  For performance (even in the debug build, where this is
// called *a lot*) this is a macro and is unchecked.
//
#define Level_Phase(L) \
    cast(Phase*, VAL_FRAME_PHASE_OR_LABEL_NODE((L)->rootvar))

INLINE void INIT_LVL_PHASE(Level* L, Phase* phase)  // check types
  { INIT_VAL_FRAME_PHASE_OR_LABEL(L->rootvar, phase); }  // ...only

INLINE void INIT_LVL_BINDING(Level* L, Context* binding)
  { BINDING(L->rootvar) = binding; }  // also fast

// Each ACTION! cell for things like RETURN/BREAK/CONTINUE has a piece of
// information in it that can can be unique (the binding).  When invoked, that
// binding is held in the Level*.  Generic dispatchers for things like RETURN
// interprets that binding as the FRAME! which the instance is specifically
// intended to return from (break out of, etc.)
//
#define Level_Binding(L) \
    cast(Context*, BINDING((L)->rootvar))

INLINE Option(const Symbol*) Level_Label(Level* L) {
    assert(Is_Action_Level(L));
    return L->label;
}


#if (! CPLUSPLUS_11)
    #define Level_State_Byte(L) \
        SECOND_BYTE(ensure(Level*, L))
#else
    // Having a special accessor in the C++ build serves two purposes.  One,
    // it can actually type check that `L` is a level.  But secondly, it also
    // is a good place to inject an assertion that you're not ignoring the
    // fact that a level "self-errored" and was notified of an abrupt failure.
    //
    INLINE Byte& Level_State_Byte(Level* L) {
        assert(Not_Level_Flag(L, ABRUPT_FAILURE));
        return SECOND_BYTE(L);
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
    INLINE REBVAL *Level_Arg(Level* L, REBLEN n) {
        assert(n != 0 and n <= Level_Num_Args(L));
        return L->rootvar + n;  // 1-indexed
    }
#endif


#define At_Level(L)                 At_Feed((L)->feed)
#define Try_At_Level(L)             Try_At_Feed((L)->feed)
#define Is_Level_At_End(L)          Is_Feed_At_End((L)->feed)
#define Not_Level_At_End(L)         Not_Feed_At_End((L)->feed)


INLINE Context* Context_For_Level_May_Manage(Level* L) {
    assert(not Is_Level_Fulfilling(L));
    Force_Level_Varlist_Managed(L);  // may already be managed
    return cast(Context*, L->varlist);
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

INLINE void Get_Level_Label_Or_Nulled(Sink(Value(*)) out, Level* L) {
    assert(Is_Action_Level(L));
    if (L->label)
        Init_Word(out, unwrap(L->label));  // WORD!, PATH!, or stored invoke
    else
        Init_Nulled(out);  // anonymous invocation
}

INLINE const char* Level_Label_Or_Anonymous_UTF8(Level* L) {
    assert(Is_Action_Level(L));
    if (L->label)
        return String_UTF8(unwrap(L->label));
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

INLINE void Free_Level_Internal(Level* L) {
    Release_Feed(L->feed);  // frees if refcount goes to 0

    if (L->varlist and Not_Node_Managed(L->varlist))
        GC_Kill_Series(L->varlist);
    Corrupt_Pointer_If_Debug(L->varlist);

    assert(Is_Pointer_Corrupt_Debug(L->alloc_value_list));

    Free_Pooled(LEVEL_POOL, L);
}

// * Push_Level() takes an Atom() for the output.  This is important, as
//   we don't want to evaluate into arbitrary array Cell*, since the array
//   could have its memory moved during an evaluation.  Also we don't want
//   to take a Value(*) that could be a variable in an object--because the
//   unstable intermediate states of the evaluation could be exposed by
//   an object (this applies to the ARG() of the function too, as these could
//   be seen by debugging code).  So typically evaluations are done into the
//   OUT or SPARE cells (or SCRATCH if in the evaluator).  Note that a
//   special exception is made by LOCAL() in frames, based on the belief
//   that local state for a native will never be exposed by a debugger.
//
INLINE void Push_Level(
    Atom(*) out,  // typecheck prohibits passing `unstable` Cell* for output
    Level* L
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
  #if DEBUG
    if (L->out)
        assert(not Is_Api_Value(L->out));
  #endif

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

    L->prior = TOP_LEVEL;
    g_ts.top_level = L;

    assert(Is_Pointer_Corrupt_Debug(L->alloc_value_list));
    L->alloc_value_list = L;  // doubly link list, terminates in `L`
}


INLINE void Update_Expression_Start(Level* L) {
    if (not Level_Is_Variadic(L))
        L->expr_index = Level_Array_Index(L);
}


INLINE void Drop_Level_Unbalanced(Level* L) {
    Drop_Level_Core(L);
}

INLINE void Drop_Level(Level* L)
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


INLINE Level* Prep_Level_Core(
    Level* L,
    Feed* feed,
    Flags flags
){
   if (L == nullptr)  // e.g. a failed allocation
       fail (Error_No_Memory(sizeof(Level)));

    L->flags.bits = flags | LEVEL_FLAG_0_IS_TRUE | LEVEL_FLAG_7_IS_TRUE;

    L->feed = feed;
    Erase_Cell(&L->spare);
    Corrupt_Pointer_If_Debug(L->out);

    L->varlist = nullptr;
    L->executor = &Evaluator_Executor;  // compatible default (for now)

    Corrupt_Pointer_If_Debug(L->alloc_value_list);

    Corrupt_If_Debug(L->u);  // fills with garbage bytes in debug build

    // !!! Recycling is done in the trampoline before the level gets a chance
    // to run.  So it's hard for the GC to know if it's okay to mark the
    // scratch cell.  We cheaply erase the cell in case it stays as the
    // evaluator executor (it's just writing a single zero).  Review.
    //
    Erase_Cell(&L->u.eval.scratch);

    Corrupt_Pointer_If_Debug(L->label);
  #if DEBUG_LEVEL_LABELS
    Corrupt_Pointer_If_Debug(L->label_utf8);
  #endif

    // !!! Previously just TOP_STACK was captured in L->baseline.stack_base,
    // but then redundantly captured via a Snap_State() in Push_Level().  The
    // responsibilities of Prep_Level() vs Push_Level() aren't clearly laid
    // out, but some clients do depend on the StackIndex being captured before
    // Push_Level() is called, so this snaps the whole baseline here.
    //
    Snap_State(&L->baseline);  // see notes on `baseline` in Level

  #if DEBUG_COUNT_TICKS
    L->tick = TG_tick;
  #endif

    return L;
}

#define Make_Level(feed,flags) \
    Prep_Level_Core(u_cast(Level*, Alloc_Pooled(LEVEL_POOL)), \
        Add_Feed_Reference(feed), (flags))

#define Make_Level_At_Core(any_array,specifier,level_flags) \
    Make_Level( \
        Prep_At_Feed( \
            Alloc_Feed(), \
            (any_array), \
            (specifier), \
            TOP_LEVEL->feed->flags.bits \
        ), \
        (level_flags) \
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
// are used in natives.  They capture the implicit Level* passed to every
// DECLARE_NATIVE ('level_') and read the information out cleanly, like this:
//
//     DECLARE_PARAM(1, foo);
//     DECLARE_PARAM(2, bar);
//
//     if (Is_Integer(ARG(foo)) and REF(bar)) { ... }
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


INLINE Bounce Native_Thrown_Result(Level* level_) {
    assert(THROWING);
    FRESHEN(level_->out);
    return BOUNCE_THROWN;
}

INLINE Bounce Native_Void_Result_Untracked(
    Atom(*) out,  // have to pass; comma at callsite -> "operand has no effect"
    Level* level_
){
    assert(out == level_->out);
    UNUSED(out);
    assert(not THROWING);
    return Init_Void_Untracked(level_->out, UNQUOTED_1);
}

INLINE Bounce Native_Unmeta_Result(Level* level_, const REBVAL *v) {
    assert(not THROWING);
    return Meta_Unquotify_Undecayed(Copy_Cell(level_->out, v));
}

INLINE Bounce Native_Trash_Result_Untracked(
    Atom(*) out,  // have to pass; comma at callsite -> "operand has no effect"
    Level* level_
){
    assert(out == level_->out);
    UNUSED(out);
    assert(not THROWING);
    return Init_Void_Untracked(level_->out, ISOTOPE_0);
}

INLINE Bounce Native_Raised_Result(Level* level_, const void *p) {
    assert(not THROWING);

    Context* error;
    switch (Detect_Rebol_Pointer(p)) {
      case DETECTED_AS_UTF8:
        error = Error_User(c_cast(char*, p));
        break;
      case DETECTED_AS_SERIES: {
        error = cast(Context*, m_cast(void*, p));
        break; }
      case DETECTED_AS_CELL: {  // note: can be Is_Raised()
        Value(const*) cell = c_cast(REBVAL*, p);
        assert(Is_Error(cell));
        error = VAL_CONTEXT(cell);
        break; }
      default:
        assert(false);
        error = nullptr;  // avoid uninitialized variable warning
    }

    assert(CTX_TYPE(error) == REB_ERROR);
    Force_Location_Of_Error(error, level_);

    while (TOP_LEVEL != level_)  // cancel sublevels as default behavior
        Drop_Level_Unbalanced(TOP_LEVEL);  // Note: won't seem like THROW/Fail

    Init_Error(level_->out, error);
    return Raisify(level_->out);
}

// Convenience routine for returning a value which is *not* located in OUT.
// (If at all possible, it's better to build values directly into OUT and
// then return the OUT pointer...this is the fastest form of returning.)
//
// Note: We do not allow direct `return v` of arbitrary values to be copied
// in the dispatcher because it's too easy to think that will work for an
// arbitrary local variable, which would be dead after the return.
//
INLINE Atom(*) Native_Copy_Result_Untracked(
    Atom(*) out,  // have to pass; comma at callsite -> "operand has no effect"
    Level* level_,
    Atom(const*) v
){
    assert(out == level_->out);
    UNUSED(out);
    assert(v != level_->out);   // Copy_Cell() would fail; don't tolerate
    assert(not Is_Api_Value(v));  // too easy to not release()
    Copy_Cell_Untracked(level_->out, v, CELL_MASK_COPY);
    return level_->out;
}



// Quick access functions from natives (or compatible functions that name a
// Level* pointer `level_`) to get some of the common public fields.
//
// There is an option to not define them due to conflicts with OUT as defined
// by the Windows.h headers.  This makes it easier for people who don't want
// to #undef the Windows versions and would rather pick their own shorthands,
// (if any).
//
#if REBOL_LEVEL_SHORTHAND_MACROS
    //
    // To make it clearer why you are defining the `level_` alias, use this
    // macro...so you don't have to comment every time.  The const constraint
    // helps check you're not expecting it to change when updating L.  (If
    // we weren't worried about building with C we could use a C++ reference
    // and it could update... but... we still build as C.)
    //
  #if CPLUSPLUS_11
    #define USE_LEVEL_SHORTHANDS(L) \
        static_assert(std::is_const<decltype(L)>::value, "L must be const"); \
        Level* const level_ = L
  #else
    #define USE_LEVEL_SHORTHANDS(L) \
        Level* const level_ = L
  #endif

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
    #define TRASH       Native_Trash_Result_Untracked(TRACK(OUT), level_)
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
INLINE REBVAL *D_ARG_Core(Level* L, REBLEN n) {  // 1 for first arg
    Param* param = ACT_PARAMS_HEAD(Level_Phase(L));
    REBVAL *arg = Level_Arg(L, 1);
    while (
        Is_Specialized(param)  // e.g. slots for saving multi-return variables
        or Cell_ParamClass(param) == PARAMCLASS_RETURN
        or Cell_ParamClass(param) == PARAMCLASS_OUTPUT
    ){
        ++param;
        ++arg;
    }
    return arg + n - 1;
}
#define D_ARG(n) \
    D_ARG_Core(level_, (n))


enum {
    ST_GROUP_BRANCH_ENTRY_DONT_ERASE_OUT = 1,  // STATE_0 erases OUT
    ST_GROUP_BRANCH_RUNNING_GROUP
};
