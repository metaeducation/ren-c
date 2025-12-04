//
//  file: %sys-level.h
//  summary: "Accessors and Argument Pushers/Poppers for Trampoline Levels"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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


// There are 8 flags in a level header that are reserved for the use of the
// level executor.  A nice idea was to make generic Get_Executor_Flag()
// routines that could check to make sure the right flags were only tested
// on levels with the right executor.
//
// Using this pervasively was punishingly slow.  Testing level flags is
// supposed to be "hot and fast" and this wound up costing 3% of runtime...
// due to just how often the flags are fiddled.
//
// A compromise is that each executor file defines its own set of uniquely
// named macros that are applicable just in that file.  This is relatively
// safe, as they should rarely examine flags for other executors.  Then, the
// rarer operations in other parts of the codebase use the generic and
// checked forms.
//
#if DEBUG_ENSURE_EXECUTOR_FLAGS
    INLINE Level* ensure_executor(Executor *executor, Level* L) {
        if (L->executor != executor) {
            if (
                executor == &Stepper_Executor
                and L->executor == &Evaluator_Executor
            ){
                // See Evaluator_Executor(), this is allowed
            }
            else
                assert(!"Wrong executor for flag tested");
        }
        return L;
    }
#else
    #define ensure_executor(executor,f) (f)  // no-op in release build
#endif


#define Get_Executor_Flag(executor,L,name) \
    ((ensure_executor(EXECUTOR_##executor, (L))->flags.bits \
        & executor##_EXECUTOR_FLAG_##name) != 0)

#define Not_Executor_Flag(executor,L,name) \
    ((ensure_executor(EXECUTOR_##executor, (L))->flags.bits \
        & executor##_EXECUTOR_FLAG_##name) == 0)

#define Set_Executor_Flag(executor,L,name) \
    (ensure_executor(EXECUTOR_##executor, (L))->flags.bits \
        |= executor##_EXECUTOR_FLAG_##name)

#define Clear_Executor_Flag(executor,L,name) \
    (ensure_executor(EXECUTOR_##executor, (L))->flags.bits \
        &= ~executor##_EXECUTOR_FLAG_##name)


//=////////////////////////////////////////////////////////////////////////=//
//
//  LEVEL ACCESSORS
//
//=////////////////////////////////////////////////////////////////////////=//


#define Is_Action_Level(L) \
    ((L)->executor == &Action_Executor)


// 1. When Drop_Action() happens, it currently sets the action executor to be
//    nullptr in order to tell the GC not to mark the state variables.  This
//    may not be the right way to do it, instead reflecting the dropped state
//    in the executor state struct and leaving the executor alone.  But this
//    routine helps find places that turn the level back into one that the
//    Push_Action() function is legal on.
//
// 2. CASCADE has a strange implementation detail where it steals the frame
//    data built for the cascade and gives it to the function at the head of
//    the pipeline.  Then it replaces the executor for the original frame to
//    the &Cascader_Executor.  Hence Drop_Action() is never called on such
//    functions to null out the executor.  These mechanisms are the ones
//    most likely to break when code is rearranged, so it's good to call
//    out the weirdness.
//
INLINE void Restart_Action_Level(Level* L) {
    assert(
        L->executor == nullptr  // Drop_Action() sets to nullptr [1]
        or L->executor == &Cascader_Executor   // Weird exception [2]
    );
    L->executor = &Action_Executor;
}


#define LEVEL_MASK_CRUMB \
    (ACTION_EXECUTOR_FLAG_INFIX_A | ACTION_EXECUTOR_FLAG_INFIX_B)

STATIC_ASSERT(LEVEL_MASK_CRUMB == CELL_MASK_CRUMB);

#define Get_Level_Crumb(L) \
    (FOURTH_BYTE(&(L)->flags.bits))

#define FLAG_LEVEL_CRUMB(crumb) \
    FLAG_FOURTH_BYTE(crumb)

INLINE void Set_Level_Crumb(Level* L, Crumb crumb) {
    L->flags.bits &= ~(LEVEL_MASK_CRUMB);
    L->flags.bits |= FLAG_LEVEL_CRUMB(crumb);
}

INLINE Option(InfixMode) Get_Level_Infix_Mode(Level* L) {
    assert(Is_Action_Level(L));
    return u_cast(InfixMode, Get_Level_Crumb(L));
}

INLINE void Set_Level_Infix_Mode(Level* L, Option(InfixMode) mode) {
    assert(Is_Action_Level(L));
    Set_Level_Crumb(L, opt mode);
}

INLINE bool Is_Level_Infix(Level* L) {  // a bit faster than != PREFIX_0
    assert(Is_Action_Level(L));
    return (did (L->flags.bits & LEVEL_MASK_CRUMB));
}


INLINE bool Level_Is_Variadic(Level* L) {
    return FEED_IS_VARIADIC(L->feed);
}

INLINE const Source* Level_Array(Level* L) {
    assert(not Level_Is_Variadic(L));
    return Feed_Array(L->feed);
}

INLINE Context* Level_Binding(Level *L) {
    return Feed_Binding(L->feed);
}


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

INLINE REBLEN Level_Expression_Index(Level* L) {  // !!! Not called?
    assert(L->executor == &Stepper_Executor);
    assert(not Level_Is_Variadic(L));
    return L->u.eval.expr_index - 1;
}

INLINE Option(const Strand*) File_Of_Level(Level* L) {
    if (Level_Is_Variadic(L))
        return nullptr;
    return Link_Filename(Level_Array(L));
}

INLINE const char* File_UTF8_Of_Level(Level* L) {
    //
    // !!! Note: Too early in boot at the moment to use CANON(ANONYMOUS).
    //
    Option(const Strand*) str = File_Of_Level(L);
    return str ? Strand_Utf8(unwrap str) : "~anonymous~";
}

INLINE Option(LineNumber) Line_Number_Of_Level(Level* L) {
    if (Level_Is_Variadic(L))
        return 0;
    return MISC_SOURCE_LINE(Level_Array(L));
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
    u_cast(Atom*, &(L)->spare)

#define Level_Scratch(L) \
    u_cast(Atom*, &(L->scratch))

INLINE Element* Evaluator_Level_Current(Level* L) {
    assert(L->executor == &Stepper_Executor);
    return u_cast(Element*, &(L->scratch));
}


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
// Note we don't use Set_Flex_Managed() here, because the varlist was never
// put in the "untracked manuals" list... created as unmanaged/untracked.
//
#define Force_Level_Varlist_Managed(L) \
    Set_Base_Managed_Bit((L)->varlist)

INLINE ParamList* Level_Varlist(Level* L) {
    assert(Is_Level_Dispatching(L));
    return u_cast(ParamList*, L->varlist);
}


// The Level's Phase is tracked by the Rootvar slot of the L->varlist (that
// slot is cached in L->rootvar for performance).  It is updated as the
// execution of an action unfolds through each phase.
//
#define Level_Phase(L) \
    Frame_Phase((L)->rootvar)

// !!! This is just hacked in for compatibility with oldgeneric.  The old
// generic dispatcher will override whatever the actual label was used
// to do the dispatch with the verb.  This will potentially lead to confusing
// errors if the call was done through an aliased name, until things get
// sorted out.
//
#define Level_Verb(L)  (unwrap (L)->u.action.label)


INLINE Details* Ensure_Level_Details(Level* L) {
    Phase* phase = Level_Phase(L);
    assert(Is_Stub_Details(phase));
    return cast(Details*, phase);
}

INLINE void Tweak_Level_Phase_Core(Level* L, Phase* phase) {
    assert(Is_Stub_Details(phase) or Is_Stub_Varlist(phase));
    CELL_FRAME_PAYLOAD_1_PHASE(L->rootvar) = phase;
}

#define Tweak_Level_Phase(L,phase) \
    Tweak_Level_Phase_Core(L, ensure(Phase*, (phase)))

INLINE void Tweak_Level_Coupling(Level* L, Option(VarList*) coupling)
  { Tweak_Frame_Coupling(L->rootvar, coupling); }  // also fast

// Each ACTION! cell for things like RETURN/BREAK/CONTINUE has a piece of
// information in it that can can be unique (the "coupling").  When invoked,
// coupling is held in the Level*.  Generic dispatchers for things like RETURN
// interprets that coupling as the FRAME! which the instance is specifically
// intended to return from (break out of, etc.)
//
#define Level_Coupling(L) \
    Frame_Coupling((L)->rootvar)

INLINE Option(const Symbol*) Level_Label(Level* L) {
    assert(Is_Action_Level(L));
  #if DEBUG_LEVEL_LABELS
    assert(L->label_utf8);  // should be non-nullptr if label valid
  #endif
    return L->u.action.label;
}

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define LEVEL_STATE_BYTE(L) \
        SECOND_BYTE(ensure(Level*, L))
#else
    INLINE Byte& LEVEL_STATE_BYTE(Level* L) {
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        return SECOND_BYTE(L);
    }
#endif



// ARGS is the parameters and refinements
// 1-based indexing into the arglist (0 slot is for FRAME! value)

#define Level_Args_Head(L) \
    (u_cast(Atom*, (L)->rootvar) + 1)

#if NO_RUNTIME_CHECKS
    #define Level_Arg(L,n) \
        (u_cast(Atom*, (L)->rootvar) + (n))
#else
    INLINE Atom* Level_Arg(Level* L, REBLEN n) {
        assert(n != 0 and n <= Level_Num_Args(L));
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        possibly(Is_Endlike_Unset(u_cast(Atom*, L->rootvar) + n));
        return u_cast(Atom*, L->rootvar) + n;  // 1-indexed
    }
#endif


#define At_Level(L)                 At_Feed((L)->feed)
#define Try_At_Level(L)             Try_At_Feed((L)->feed)
#define Is_Level_At_End(L)          Is_Feed_At_End((L)->feed)
#define Not_Level_At_End(L)         Not_Feed_At_End((L)->feed)

// When evaluative contexts ask "Is_Level_At_End()" and see [], they might
// think that is the end of the level.  But [, , ,] would be an end of the
// level after an evaluation step with no result (e.g. Is_Endlike_Unset()
// coming back from Stepper_Executor()).  So you don't want to get
// lulled into a false sense of security that [] is the only way a level ends.
//
// Additionally, if you skip a call into the stepper because you see [] then
// in the general case you're not providing visibility of a step into a list
// that is concretely *there*, albeit empty.  The person debugging may want
// to know about it.  So this test will return false in debug mode, and
// in RUNTIME_CHECKS mode it will sporadically return false as well.
//
#define Try_Is_Level_At_End_Optimization(L) \
    (In_Debug_Mode(32) ? false : Is_Feed_At_End((L)->feed))

INLINE ParamList* Varlist_Of_Level_Maybe_Unmanaged(Level* L) {
    assert(Is_Level_Dispatching(L));
    return L->varlist;
}

INLINE ParamList* Varlist_Of_Level_Force_Managed(Level* L) {
    assert(Is_Level_Dispatching(L));
    Force_Level_Varlist_Managed(L);  // may already be managed
    return L->varlist;
}


//=//// FRAME LABELING ////////////////////////////////////////////////////=//

// How good levels are at retaining their labels may vary in the future.  It's
// only a Symbol for now (e.g. the symbol of the last WORD! in a TUPLE! that
// ran the action).  But it could perhaps store full TUPLE! information,
// maybe only in a Debug mode.
//
// We abstract that idea by writing a cell.  This also gives us a chance to
// make sure you're only asking about Action levels, or levels that are
// dispatching intrinsics.
//
// 1. If LEVEL_FLAG_DISPATCHING_INTRINSIC is true, then we could get label
//    information here.  But it's probably better to have the very few callers
//    that can deal with intrinsics do so, to avoid giving the impression that
//    the label we give back is for the level itself (it may be that Action
//    levels can call intrinsics using their level, and we don't want to wind
//    up skipping the Action level in a stack trace by thinking its label has
//    been accounted for by the intrinsic).

INLINE Option(const Symbol*) Try_Get_Action_Level_Label(Level* L) {
    assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));  // be cautious [1]
    assert(Is_Action_Level(L));
  #if DEBUG_LEVEL_LABELS
    assert(L->label_utf8);
  #endif
    return L->u.action.label;
}

INLINE const char* Level_Label_Or_Anonymous_UTF8(Level* L) {
    assert(Is_Action_Level(L));
    if (L->u.action.label)
        return Strand_Utf8(unwrap L->u.action.label);
    return "~anonymous~";
}

INLINE void Set_Action_Level_Label(Level* L, Option(const Symbol*) label) {
    L->u.action.label = label;
  #if DEBUG_LEVEL_LABELS
    assert(L->label_utf8 == nullptr);
    L->label_utf8 = Level_Label_Or_Anonymous_UTF8(L);
  #endif
}



//=//// LEVEL ALLOCATION AND FREEING //////////////////////////////////////=//

// Note: Free_Feed() will handle feeding a feed through to its end (which may
// release handles/etc), so no requirement Level_At(L) be at END.
//
// 1. Exactly how and when the varlist is detached from the level has been
//    evolving, but it's generally the case that Drop_Action() at the end
//    of the Action_Executor() will do it.  There's an exception when the
//    ACTION_EXECUTOR_FLAG_FULFILL_ONLY flag is used at the moment, where
//    not nulling it out is how it gets returned.  Also, abrupt panics
//    jump up to the trampoline so that Drop_Action() has to be run by
//    the trampoline when it drops the levels automatically.
//
INLINE void Free_Level_Internal(Level* L) {
    possibly(L != TOP_LEVEL);  // e.g. called by Plug_Handle_Cleaner()

  #if RUNTIME_CHECKS
    Base* b = L->alloc_value_list;
    while (b != L) {
        Stub* stub = cast(Stub*, b);
        printf("API handle was allocated but not freed, crashing on leak\n");
        crash (stub);
    }
  #endif

    Release_Feed(L->feed);  // frees if refcount goes to 0

    if (L->varlist) {  // !!! Can be not null if abrupt panic [1]
        assert(Misc_Runlevel(L->varlist) == nullptr);  // Drop_Action() nulls
        if (Not_Base_Managed(L->varlist))
            GC_Kill_Flex(L->varlist);
    }

    Corrupt_If_Needful(L->varlist);
    Corrupt_If_Needful(L->alloc_value_list);

  #if TRAMPOLINE_COUNTS_TICKS
    L->tick = TICK;
  #endif

    Raw_Pooled_Free(LEVEL_POOL, L);
}

// 1. Push_Level() takes an Atom* for the output.  It is a Need() and not a
//    Sink() because we may not want to corrupt the cell we are given (e.g.
//    if we're pushing a level to do infix processing on an already calculated
//    result).
//
//    Taking an Atom* is important, as we don't want to evaluate into variables
//    or array slots.  Not only can they have their memory moved during an
//    evaluation, but we don't want unstable antiforms being put into variables
//    (or any antiforms being put in array cells).
//
//    We want the cell to be easy to cheaply erase with Erase_Cell() for
//    performance reasons.  But also, if we allowed API cells as evaluation
//    targets that would create some confusion with wanting API cells to
//    be freed when they are return targets (there might be some way to say
//    that cells in L->out don't count for the rule, but this would confuse
//    some invariants...and it's better to just say that evaluations into
//    API cells must leave those cells erased until the eval is complete,
//    and put BASE_FLAG_ROOT on afterward).
//
// 2. The commitment of an Intrinsic is that if it runs without a Level, then
//    it won't perform evaluations or use continuations.  Those mechanics are
//    not available when being called directly from the Stepper_Executor.
//
// 3. Levels are pushed to reuse for several sequential operations like ANY,
//    ALL, CASE, REDUCE.  It is allowed to change the output cell for each
//    evaluation.  But the GC expects initialized bits in the output slot at
//    all times.
//
// 4. Uninterruptibility is inherited by default by Push_Level(), but
//    interruptibility is not.
//
INLINE void Push_Level_Dont_Inherit_Interruptibility(
    Need(Atom*) out,  // prohibits passing Element or Value as output [1]
    Level* L
){
    assert(not TOP_LEVEL or Not_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC));

    L->out = out;  // must be a valid cell for GC [3]
  #if RUNTIME_CHECKS
    if (L->out)
        assert(
            Is_Cell_Erased(L->out)
            or Not_Cell_Readable(L->out)
            or not Is_Atom_Api_Value(L->out)
        );
  #endif

    assert(
        not (L->out->header.bits &
            (BASE_FLAG_ROOT | BASE_FLAG_MARKED | BASE_FLAG_MANAGED)
        )
    );
    if (not (L->flags.bits & FLAG_STATE_BYTE(255)))  // no FLAG_STATE_BYTE()
        Erase_Cell(L->out);  // STATE_0 requires erased cell [1]

  #if RUNTIME_CHECKS
    //
    // !!! TBD: the relevant file and line update when L->feed->array changes
    //
    L->file = File_UTF8_Of_Level(L);
    L->line = opt Line_Number_Of_Level(L);
  #endif

    L->prior = TOP_LEVEL;
    g_ts.top_level = L;

    assert(L->alloc_value_list == L);
}

INLINE void Push_Level_Erase_Out_If_State_0(  // inherits uninterruptibility [4]
    Need(Atom*) out,  // prohibits passing `unstable` Cell* for output [1]
    Level* L
){
    Push_Level_Dont_Inherit_Interruptibility(out, L);
    L->flags.bits |= L->prior->flags.bits & LEVEL_FLAG_UNINTERRUPTIBLE;  // [4]
}

INLINE void Update_Expression_Start(Level* L) {
    assert(
        L->executor == &Stepper_Executor
        or L->executor == &Evaluator_Executor
    );
    if (not Level_Is_Variadic(L))
        L->u.eval.expr_index = Level_Array_Index(L);
}


INLINE void Drop_Level_Unbalanced(Level* L) {
    assert(TOP_LEVEL == L);
    g_ts.top_level = L->prior;
    Free_Level_Internal(L);
}

INLINE void Drop_Level(Level* L)
{
  #if DEBUG_BALANCE_STATE
    //
    // To avoid slowing down checked builds, Eval_Core() doesn't check on
    // every cycle, just on drop.  But if it's hard to find the cycle
    // causing problems, see BALANCE_CHECK_EVERY_EVALUATION_STEP.
    //
    Assert_State_Balanced(&L->baseline);
  #else
    assert(TOP_INDEX == L->baseline.stack_base);  // Cheaper check
  #endif

    Drop_Level_Unbalanced(L);
}


// 1. The evaluator executor uses some of its fixed-size storage in the Level
//    for a cell, which the GC marks when it sees &Evaluator_Executor as what
//    runs that level.  But recycling is done in the trampoline before the
//    level gets a chance to run.  So it's hard for the GC to know if it's
//    okay to mark at "current" cell.  We cheaply erase the cell in case the
//    executor is the evaluator (it's just writing a single zero).  Review.
//
// 2. Previously just TOP_STACK was captured in L->baseline.stack_base, but
//    then redundantly captured via a Snap_State() in Push_Level().  The
//    responsibilities of Prep_Level() vs Push_Level() aren't clearly laid
//    out, but some clients do depend on the StackIndex being captured before
//    Push_Level() is called, so this snaps the whole baseline here.
//
INLINE Result(Level*) Prep_Level_Core(
    Executor* executor,
    Result(void*) preallocated,
    Result(Feed*) feed,
    Flags flags
){
    trap (
      Level* L = u_downcast preallocated
    );
    L->flags.bits = flags | LEVEL_FLAG_0_IS_TRUE | LEVEL_FLAG_4_IS_TRUE;

    L->feed = feed except (Error* e) {
        Raw_Pooled_Free(LEVEL_POOL, L);
        return fail (e);
    }

    Add_Feed_Reference(L->feed);

    Force_Erase_Cell(&L->spare);
    Force_Erase_Cell(&L->scratch);
    Corrupt_If_Needful(L->out);

    L->varlist = nullptr;
    L->executor = executor;

    L->alloc_value_list = L;  // doubly link list, terminates in `L`

    Corrupt_If_Needful(L->u);

  #if DEBUG_LEVEL_LABELS  // only applicable to L->u.action.label levels...
    L->label_utf8 = nullptr;  // ...but in Level for easy C watchlisting
  #endif

    Snap_State(&L->baseline);  // [2] (also see notes on `baseline` in Level)

  #if TRAMPOLINE_COUNTS_TICKS
    L->tick = g_tick;
  #endif

    return L;
}

#define Make_Level(executor,feed,flags) \
    Prep_Level_Core(executor, Raw_Pooled_Alloc(LEVEL_POOL), (feed), (flags))

#define Make_Level_At_Inherit_Const(executor,list,binding,level_flags) \
    Make_Level( \
        (executor), \
        Prep_At_Feed( \
            Alloc_Feed(), \
            (list), \
            (binding), \
            TOP_LEVEL->feed->flags.bits \
        ), \
        (level_flags) \
    )

#define Make_Level_At_Core(executor,list,binding,level_flags) \
    Make_Level( \
        (executor), \
        Prep_At_Feed( \
            Alloc_Feed(), \
            (list), \
            (binding), \
            FEED_MASK_DEFAULT  /* don't inherit TOP_LEVEL's const feed bit */ \
        ), \
        (level_flags) \
    )

#define Make_Level_At(executor,list,flags) \
    Make_Level_At_Core((executor), (list), SPECIFIED, (flags))

#define Make_End_Level(executor,flags) \
    Make_Level((executor), TG_End_Feed, (flags))


//=//// ARGUMENT AND PARAMETER ACCESS HELPERS ////=///////////////////////////
//
// These accessors are what is behind the INCLUDE_PARAMS_OF_XXX macros that
// are used in natives.  They capture the implicit Level* passed to every
// DECLARE_NATIVE ('level_') and read the information out cleanly, like this:
//
//     DECLARE_PARAM(1, FOO);
//     DECLARE_PARAM(2, BAR);
//
//     if (Is_Integer(ARG(FOO)) and Bool_ARG(BAR)) { ... }
//
// ARG() gives a mutable pointer to the argument's cell.  Bool_ARG() is typically
// used with refinements, and gives a const reference where NULLED cells are
// turned into C nullptr.
//
// By contract, Rebol functions are allowed to mutate their arguments and
// refinements just as if they were locals...guaranteeing only their return
// result as externally visible.  Hence the ARG() cells provide a GC-safe
// slot for natives to hold values once they are no longer needed.
//
// It is also possible to get the typeset-with-symbol for a particular
// parameter or refinement, e.g. with `PARAM(FOO)` or `PARAM(BAR)`.

#define DECLARE_PARAM(n,name) \
    static const int param_##name##_ = n

#define DECLARE_INTRINSIC_PARAM(name)  /* was used, not used at the moment */ \
    NOOP  // the INCLUDE_PARAMS_OF_XXX macros still make this, may find a use

#define Erase_ARG(name) \
    Erase_Cell(Level_Arg(level_, param_##name##_))

#define ARG(name) \
    Known_Stable(Level_Arg(level_, param_##name##_))

#define Element_ARG(name) \
    Known_Element(Level_Arg(level_, param_##name##_))  // checked build asserts

#define Atom_ARG(name) \
    Level_Arg(level_, param_##name##_)

#define Bool_ARG(name) \
    (not Is_Nulled(Known_Stable(Level_Arg(level_, param_##name##_))))


INLINE Option(Element*) Optional_Element_Level_Arg(Level* L, REBLEN n)
{
    Value* arg = Known_Stable(Level_Arg(L, n));
    if (Is_Nulled(arg))
        return nullptr;
    return Known_Element(arg);
}

#define Optional_Element_ARG(name) \
    Optional_Element_Level_Arg(level_, param_##name##_)

#define LOCAL(name) /* alias for ARG() when slot is <local> */ \
    Level_Arg(level_, (param_##name##_))  // initialized to unset state!

#define Element_LOCAL(name) \
    Known_Element(Level_Arg(level_, (param_##name##_)))

#define Value_LOCAL(name) \
    Known_Stable(Level_Arg(level_, (param_##name##_)))

#define PARAM(name) \
    Phase_Param(Level_Phase(level_), (param_##name##_))  // a TYPESET!


#define ARG_N(n) \
    Known_Stable(Level_Arg(level_, (n)))

#define Element_ARG_N(n) \
    Known_Element(Level_Arg(level_, (n)))

#define PARAM_N(n) \
    Phase_Param(Level_Phase(level_), (n))


// 1. In the case of an abrupt panic, the stack may be unwound without the
//    participation of code that protected the output Cell as a sanity check,
//    so it will still be protected.  We could Force_Erase_Cell(), but that
//    might cover up other bugs.
//
INLINE Bounce Native_Thrown_Result(Level* L) {
    Clear_Lingering_Out_Cell_Protect_If_Debug(L);  // for abrupt panics [1]

    Erase_Cell(L->out);
    assert(Is_Throwing(L));

    while (TOP_LEVEL != L) {  // convenience
        Drop_Level(TOP_LEVEL);
        Erase_Cell(TOP_LEVEL->out);
    }

    return BOUNCE_THROWN;
}


// Dispatchers like Lambda_Dispatcher() etc. have their own knowledge of
// whether they are `return: []` or not, based on where they keep the return
// information.  So they can't use the `return TRASH;` idiom.
//
INLINE Value* Init_Trash_Named_From_Level(Sink(Value) out, Level* level_) {
    Option(const Symbol*) label = Level_Label(level_);
    if (label) {
        Init_Utf8_Non_String_From_Strand(out, TYPE_RUNE, label);
        Stably_Antiformize_Unbound_Fundamental(out);
    }
    else
        Init_Tripwire_Untracked(out);
    return out;
}


// Functions that `return: []` actually make a TRASH! with the label of the
// level that produced it.  This is usually done in typechecking, but natives
// don't run typechecks in the release build...so the `return TRASH;` has to
// do it.
//
// 1. If you say `return: [trash!]` then type checking doesn't distort the
//    contents of the trash, so we don't want natives using `return TRASH;`
//    to have the behavior on accident.
//
INLINE Atom* Native_Trash_Result_Untracked(
    Level* level_
){
    assert(not THROWING);

  #if RUNTIME_CHECKS
    Details* details = Ensure_Level_Details(level_);
    Value* param = Details_At(details, IDX_RAW_NATIVE_RETURN);
    assert(Is_Parameter_Spec_Empty(param));  // not for `return: [trash!]` [1]
  #endif

    return Init_Trash_Named_From_Level(level_->out, level_);
}

INLINE Bounce Native_Unlift_Result(Level* level_, const Element* v) {
    assert(not THROWING);
    Copy_Cell(level_->out, v);
    require (
      Atom* atom = Unliftify_Undecayed(level_->out)
    );
    return atom;
}


// Convenience routine for returning a value which is *not* located in OUT.
// (If at all possible, it's better to build values directly into OUT and
// then return the OUT pointer...this is the fastest form of returning.)
//
// Note: We do not allow direct `return v` of arbitrary values to be copied
// in the dispatcher because it's too easy to think that will work for an
// arbitrary local variable, which would be dead after the return.
//
INLINE Bounce Native_Copy_Result_Untracked(
    Atom* out,  // have to pass; comma at callsite -> "operand has no effect"
    Level* level_,
    const Atom* v
){
    assert(out == level_->out);
    UNUSED(out);
    assert(v != level_->out);   // Copy_Cell() would fail; don't tolerate
    assert(not Is_Atom_Api_Value(v));  // too easy to not release()
    Copy_Cell_Untracked(level_->out, v, CELL_MASK_COPY);
    return level_->out;
}

INLINE Bounce Native_Branched_Result(Level* level_, Atom* atom) {
    assert(atom == level_->out);  // wouldn't be zero cost if we supported copy
    if (Is_Light_Null(atom))
        Init_Heavy_Null(atom);  // box up for THEN reactivity [2]
    return level_->out;
}

// It's necessary to be able to tell from the outside of a loop whether it
// had a BREAK or not, e.g.
//
//   flag: 'true
//   while [true? flag] [flag: 'false, null]
//
// We don't want that to evaluate to NULL--because NULL is reserved for a
// break signal.  So we make a ~[~null~]~ "heavy null" antiform PACK!.
//
// Also, returning VOID is reserved for if-and-only-if the loop never ran.
// That's crucial for implementing loop compositions that give correct result
// values.  For instance, we want these loops to have parity:
//
//     >> for-both 'x [1 2 3 4] [] [x * 10]
//     == 40
//
//     >> for-each 'x [1 2 3 4] [x * 10]
//     == 40
//
// If FOR-BOTH is implemented in terms of two FOR-EACH loops, then we want to
// know the second FOR-EACH loop never produced a result (without having to
// look at the input and hinge on the semantics of the loop).  But if VOID
// is this signal, we have to worry about:
//
//     >> for-both 'x [1 2] [3 4] [if x = 4 [void]]
//     == 20  ; if second FOR-EACH gave VOID, and we assumed "never ran"
//
// So instead, TRASH is produced for VOID if the body ever ran.  This can be
// worked around with meta-result protocols if it's truly needed.
//
INLINE Bounce Native_Looped_Result(Level* level_, Atom* atom) {
    assert(atom == level_->out);  // wouldn't be zero cost if we supported copy
    if (Is_Light_Null(atom))
        Init_Heavy_Null_Untracked(atom);  // distinguish from null for BREAK
    else if (Is_Void(atom))
        Init_Tripwire_Untracked(atom);  // distinguish from loop that never ran
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
// 1. Once it was relatively trivial to say `return nullptr;` vs to say
//    `return Init_Nulled(OUT);`, so it wasn't considered to be any big
//    savings to not return the output cell pointer directly.  That was
//    before Result(T), where more complex discernment is needed on the 0
//    bounce state to decide if it's an error or not.  It's now worth it to
//    `return NULLED;` to avoid the extra checking.
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
    #define SCRATCH Level_Scratch(level_)
    #define STATE   LEVEL_STATE_BYTE(level_)

    #define SUBLEVEL    (assert(TOP_LEVEL->prior == level_), TOP_LEVEL)

    #define VOID        x_cast(Bounce, Init_Void(OUT))
    #define GHOST       x_cast(Bounce, Init_Ghost(OUT))
    #define NULLED      x_cast(Bounce, Init_Nulled(OUT))  // nontrivial [1]

    #define TRASH       TRACK(Native_Trash_Result_Untracked(level_))
    #define THROWN      Native_Thrown_Result(level_)
    #define COPY(v)     Native_Copy_Result_Untracked(TRACK(OUT), level_, (v))
    #define UNLIFT(v)   Native_Unlift_Result(level_, (v))

    #define BRANCHED(v)  Native_Branched_Result(level_, (v))

    #define VETOING_NULL  u_cast(Bounce, nullptr)

    #define LOOPED(v)      Native_Looped_Result(level_, (v))
    #define BREAKING_NULL  VETOING_NULL  // does break it need to be distinct?

    // Note: For efficiency, intrinsic typecheckers must return BOUNCE_OKAY
    // or nullptr.  This means that trying to make LOGIC(b) "more efficient"
    // by doing Init_Okay(OUT) or Init_Nulled(OUT) will break things.
    //
    #define OKAY        BOUNCE_OKAY
    #define LOGIC(b)    ((b) == true ? BOUNCE_OKAY : nullptr)

    // `panic (UNHANDLED);` is a shorthand for something that's written often
    // enough in IMPLEMENT_GENERIC() handlers that it seems worthwhile.
    //
    // !!! Once it was customized based on the "verb" of a generic, but that
    // mechanism has been removed.  Review what generic dispatch might do
    // to make this better (distinct BOUNCE_UNHANDLED that only the generic
    // dispatch mechanism understands, and slipstream verb into the generic
    // somehow?)
    //
    #define UNHANDLED \
        Error_Unhandled(level_)

    #define BASELINE   (&level_->baseline)
    #define STACK_BASE (level_->baseline.stack_base)
#endif


// There's an optimization trick which lets an executor stay on the stack
// and run another executor for a level it replaced.  While that could just
// return BOUNCE_CONTINUE and go back to the trampoline, it was noticed that
// if the trampoline cooperated and reset the top level in these cases then
// time could be saved.
//
// This may not be a great optimization for the complexity it introduces, but
// it's being tried out.  It does mean you incur one C stack level for each
// downshift, and some pathological case (like Cascaders calling Cascaders
// in some big regress) may be bad, but it seems rare.  May be removed.
//
#if NO_RUNTIME_CHECKS
    #define Adjust_Level_For_Downshift(L)  TOP_LEVEL
#else
    INLINE Level* Adjust_Level_For_Downshift(Level* L) {
        Level* temp = TOP_LEVEL;
        while (temp != L) {  // Cascaders can downshift Cascaders, etc.
            temp = temp->prior;
            assert(
                temp->executor == &To_Or_As_Checker_Executor
                or temp->executor == &Cascader_Executor
                or temp->executor == &Copy_Quoter_Executor
            );
        }
        return TOP_LEVEL;
    }
#endif


INLINE void Enable_Dispatcher_Catching_Of_Throws(Level* L)
{
    assert(LEVEL_STATE_BYTE(L) != STATE_0);
    assert(Not_Executor_Flag(ACTION, L, DISPATCHER_CATCHES));
    Set_Executor_Flag(ACTION, L, DISPATCHER_CATCHES);
}

INLINE void Disable_Dispatcher_Catching_Of_Throws(Level* L)
{
    assert(LEVEL_STATE_BYTE(L) != STATE_0);
    assert(Get_Executor_Flag(ACTION, L, DISPATCHER_CATCHES));
    Clear_Executor_Flag(ACTION, L, DISPATCHER_CATCHES);
}


// Shared code for putting a definitional RETURN or YIELD into the first slot
// (or second slot) of a Level's frame.
//
INLINE void Inject_Definitional_Returner(
    Level* L,
    const Value* definitional,  // LIB(DEFINITIONAL_RETURN), or YIELD
    SymId id  // SYM_YIELD, SYM_RETURN
){
    Details* details = Ensure_Level_Details(L);

    Index slot_num = Get_Details_Flag(details, METHODIZED) ? 2 : 1;

    assert(Key_Id(Varlist_Key(L->varlist, slot_num)) == id);
    assert(Is_Base_Managed(L->varlist));

    Atom* returner = Level_Arg(L, slot_num);  // should start out specialized
    Assert_Quotified_Parameter(returner);

    Init_Action(
        returner,
        Frame_Phase(definitional),  // DEFINITIONAL-RETURN or YIELD
        Canon_Symbol(id),  // relabel as plain RETURN or YIELD
        L->varlist  // so knows where to RETURN/YIELD from
    );
}

// If DETAILS_FLAG_METHODIZED is set, we need to initialize the `.` slot in
// the frame with the coupling object.  It will always be the first frame
// slot if it's there, because Pop_Paramlist() ensures that.
//
INLINE void Inject_Methodization_If_Any(Level* L)
{
    Details* details = Ensure_Level_Details(L);

    if (Not_Details_Flag(details, METHODIZED))
        return;

    assert(Key_Id(Phase_Keys_Head(L->varlist)) == SYM_DOT_1);

    Atom* methodization = Level_Args_Head(L);

    Context* coupling = opt Level_Coupling(L);

    // !!! TBD: apply typecheck of methodization against coupled object

    Init_Object(methodization, cast(VarList*, coupling));
}
