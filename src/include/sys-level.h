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
    return logical (L->flags.bits & LEVEL_MASK_CRUMB);
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
#define Level_Num_Slots(L) \
    ((L)->varlist->content.dynamic.used - 1)  // minus rootvar

#define Level_Out(L) \
    known(Level*, (L))->out

#define Level_Spare(L) \
    u_cast(Value*, &(L)->spare)

#define Level_Scratch(L) \
    u_cast(Value*, &(L->scratch))

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


INLINE Option(const Symbol*) Level_Label(Level* L) {
    assert(Is_Action_Level(L));
  #if DEBUG_LEVEL_LABELS
    assert(L->label_utf8);  // should be non-nullptr if label valid
  #endif
    return L->u.action.label;
}


INLINE Details* Ensure_Level_Details(Level* L) {
    assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
    Phase* phase = Level_Phase(L);
    assert(Is_Stub_Details(phase));
    return cast(Details*, phase);
}

// Generally speaking, making derived actions (like specialization or adapt
// or enclose) will get the same purity or impurity as the original function.
// However, this can be overridden with PURE:OFF or IMPURE:OFF.  If it
// gets overridden then you can have an outer function (like an ADAPT) that is
// impure which updates to run an adaptee which is pure... or an outer
// function that is pure trying to run an impure adaptee.
//
// (We could prohibit this, but the flexibility seems like it might be useful
// in some cases.)
//
INLINE Result(None) Tweak_Level_Phase_Core(Level* L, Phase* phase) {
    if (Get_Level_Flag(L, PURE)) {
        if (Get_Stub_Flag(phase, PHASE_IMPURE)) {
            Option(const Symbol*) label = Frame_Label_Deep(
                Phase_Archetype(phase)
            );
            if (not label)
                label = Level_Label(L);
            return fail (Error_Impure_Call_Raw(label));
        }
    }
    else {  // inherit phase purity
        Flags action_pure_bit = (
            phase->header.bits & STUB_FLAG_PHASE_PURE
        );

        STATIC_ASSERT(STUB_FLAG_PHASE_PURE == LEVEL_FLAG_PURE);
        L->flags.bits |= action_pure_bit;

        STATIC_ASSERT(STUB_FLAG_PHASE_PURE == VARLIST_FLAG_PURE);
        L->varlist->header.bits |= action_pure_bit;
    }

    CELL_FRAME_PAYLOAD_1_PHASE(L->rootvar) = phase;
    return none;
}

#define Tweak_Level_Phase(L,phase) \
    Tweak_Level_Phase_Core(L, Known_Phase(phase))

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


#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define LEVEL_STATE_BYTE(L) \
        SECOND_BYTE(known(Level*, L))
#else
    INLINE StateByte& LEVEL_STATE_BYTE(Level* L) {
        assert(Not_Level_Flag(L, DISPATCHING_INTRINSIC));
        return SECOND_BYTE(L);
    }
#endif



#define At_Level(L)                 At_Feed((L)->feed)
#define Try_At_Level(L)             Try_At_Feed((L)->feed)
#define Is_Level_At_End(L)          Is_Feed_At_End((L)->feed)
#define Not_Level_At_End(L)         Not_Feed_At_End((L)->feed)


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

// 1. Push_Level() takes an Value* for the output.  It is a Exact() and not a
//    Sink() because we may not want to corrupt the cell we are given (e.g.
//    if we're pushing a level to do infix processing on an already calculated
//    result).
//
//    Taking a Value* is important: we don't want to evaluate antiforms into
//    array Element* cells, and also they might move (which object Slots
///   might also move as well).
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
    Exact(Value*) out,  // prohibit passing Element/Stable/Slot as output [1]
    Level* L
){
    assert(not TOP_LEVEL or Not_Level_Flag(TOP_LEVEL, DISPATCHING_INTRINSIC));

  #if RUNTIME_CHECKS
    assert(L->prior == nullptr);  // Prep_Level_Core() should null it
  #endif

  #if RUNTIME_CHECKS
    assert(out != &L->spare and out != &L->scratch);
    assert(
        Is_Cell_Erased(out)
        or Not_Cell_Readable(out)
        or not Is_Api_Value(out)
    );
    assert(
        not (out->header.bits &
            (BASE_FLAG_ROOT | BASE_FLAG_MARKED | BASE_FLAG_MANAGED)
        )
    );
  #endif

  #if DEBUG_TRACK_EXTEND_CELLS
    assert(out->track_flags.bits & TRACK_FLAG_VALID_EVAL_TARGET);
  #endif

    if (
        LEVEL_STATE_BYTE(L) == STATE_0
        and Not_Level_Flag(L, DEBUG_STATE_0_OUT_NOT_ERASED_OK)
    ){
        assert(Is_Cell_Erased(out));  // STATE_0 requires erased cell [1]
    }

    L->out = out;  // must be a valid cell for GC [3]

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

INLINE void Push_Level_Core(  // inherit interrupt and purity [4]
    Contra(Value) out,  // prohibit passing Element/Stable/Slot as output [1]
    Level* L
){
    Push_Level_Dont_Inherit_Interruptibility(out, L);
    L->flags.bits |= (
        L->prior->flags.bits & (  // [4]
            LEVEL_FLAG_UNINTERRUPTIBLE | LEVEL_FLAG_PURE
        )
    );
}

#define Push_Level(out,L) \
    Push_Level_Core(Possibly_Unstable(out), (L))

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


// 1. We separate Level pushing into two steps: the Make_Level() and the
//    Push_Level().  You have to do both...because the Level has to be put
//    into the Level stack in order to be safe for GC.  But the separation
//    buys us the ability to not cram too many parameters into one function
//    (e.g. we can pass the output cell separately, and have different
//    kinds of interruptibility indicated by different pushes).  But there
//    can't be any panic() between the Make and the Push...and the Push
//    itself cannot panic.
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

  #if RUNTIME_CHECKS
    L->prior = nullptr;
  #endif

    L->flags.bits = flags | LEVEL_FLAG_0_IS_TRUE | LEVEL_FLAG_4_IS_TRUE;

    L->feed = feed except (Error* e) {
        Raw_Pooled_Free(LEVEL_POOL, L);
        return fail (e);
    }

    Add_Feed_Reference(L->feed);

    FORCE_TRACK_VALID_EVAL_TARGET(Force_Erase_Cell_Untracked(&L->spare));
    FORCE_TRACK_VALID_EVAL_TARGET(Force_Erase_Cell_Untracked(&L->scratch));
    Corrupt_If_Needful(L->out);

    L->varlist = nullptr;
    L->executor = executor;

    L->alloc_value_list = L;  // doubly link list, terminates in `L`

  #if DEBUG_LEVEL_LABELS  // only applicable to L->u.action.label levels...
    L->label_utf8 = nullptr;  // ...but in Level for easy C watchlisting
  #endif

  #if TRAMPOLINE_COUNTS_TICKS
    L->tick = g_tick;
  #endif

  corrupt_union_that_is_custom_per_executor: {

  // 1. There's currently a situation where a Level can get pushed, and then
  //    the Trampoline can run a Recycle() before the Level gets a chance to
  //    run its STATE_0 `initial_entry`.  This means we can't leave bad bits
  //    in places the GC expects to check.
  //
  //    This should be reviewed, as there would be advantages to giving each
  //    Level a chance to initialize its executor-specific union storage
  //    before any GC can run.  But the problem is that in an `initial_entry`
  //    one Level may push another.  So this could hold off recycling
  //    indefinitely, which would be a problem.

    Corrupt_If_Needful(L->u);
    Force_Erase_Cell(&L->u.eval.primed);  // must be valid before Recycle [1]

} snapshot_baseline_state: {

  // Previously just TOP_STACK was captured in L->baseline.stack_base, but
  // then redundantly captured via a Snap_State() in Push_Level().  The
  // responsibilities of Prep_Level() vs Push_Level() aren't clearly laid out,
  // but some clients do depend on the StackIndex being captured before
  // Push_Level() is called, so this snaps the whole baseline here.

    Snap_State(&L->baseline);  // (also see notes on `baseline` in Level)

    return L;
}}

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
    Make_Level((executor), g_end_feed, (flags))


// 1. In the case of an abrupt panic, the stack may be unwound without the
//    participation of code that protected the output Cell as a sanity check,
//    so it will still be protected.  We could Force_Erase_Cell(), but that
//    might cover up other bugs.
//
INLINE Bounce Native_Thrown_Result(Level* L) {
    Clear_Lingering_Out_Cell_Shield_If_Debug(L);  // for abrupt panics [1]

    Erase_Cell(L->out);
    assert(Is_Throwing(L));

    while (TOP_LEVEL != L) {  // convenience
        Drop_Level(TOP_LEVEL);
        Erase_Cell(TOP_LEVEL->out);
    }

    return BOUNCE_THROWN;
}


// !!! Currently unused concept, Cooperative PANIC
//
// If possible, we want to avoid exceptions or longjmps crossing arbitray
// C stack frames.  It's generally dicey, can break C++ code by skipping
// destructors, and means that the only choice architectures that don't have
// longjmp or exception support is to crash.
//
// So the system tries to bubble errors up via the Result(T) system, where
// any return type that can carry a zero state can receive that state as
// a return result...which a global `g_failure` pointer set to indicate an
// error occurred.
//
// When this error state bubbles up to a native dispatcher, we have a choice
// that we could either treat it as a "definitional error" as if you had put
// a FAILURE! into the output cell--or we could treat it as a panic.  We want
// `return fail()` to work definitionally.  So it does not panic.
//
// But the only places in C stacks where we can `return` gracefully are at
// the native boundaries to the trampoline.  So any `require` or `panic` that
// is not in a native body would have to be "abrupt".  This should be avoided
// if at all possible.
//
// !!! UPDATE: It was at one point deemed too difficult to discern being in
// a top-level native from being in other C code, and the codebase had far
// too many panic()s in it to be able to reason about.  So the cooperative
// panic idea was shelved in favor of just making all panics abrupt.  I'd
// forgotten that it had been shelved on purpose...so started to reinvent it.
// Leaving behind to serve as commentary for potentially revisiting the idea.
//
INLINE Bounce Native_Panic_Result(Level* L, Error* e) {;
    Force_Location_Of_Error(e, L);
    return Native_Thrown_Result(L);
}


// Dispatchers like Lambda_Dispatcher() etc. have their own knowledge of
// whether they are `return: ~` or not, based on where they keep the return
// information.  So they can't use the `return TRASH_OUT;` idiom.
//
INLINE Value* Init_Trash_Named_From_Level(Sink(Value) out, Level* level_) {
    Option(const Symbol*) label = Level_Label(level_);
    if (label)
        Init_Trash(out, unwrap label);
    else
        Init_Tripwire(out);
    return out;
}


// Functions that `return: ~` actually make a TRASH! with the label of the
// level that produced it.  This is usually done in typechecking, but natives
// don't run typechecks in the release build...so the `return TRASH_OUT;` has to
// do it.
//
// 1. If you say `return: [trash!]` then type checking doesn't distort the
//    contents of the trash, so we don't want natives using `return TRASH_OUT;`
//    to have the behavior on accident.
//
INLINE Value* Native_Trash_Result_Untracked(
    Level* level_
){
    assert(not THROWING);

  #if RUNTIME_CHECKS
    Details* details = Ensure_Level_Details(level_);
    Element* param = As_Element(Details_At(details, IDX_RAW_NATIVE_RETURN));
    assert(Get_Parameter_Flag(param, AUTO_TRASH));  // only `return: ~` [1]
  #endif

    return Init_Trash_Named_From_Level(level_->out, level_);
}

INLINE Bounce Native_Unlift_Result(Level* level_, const Element* v) {
    assert(not THROWING);
    Copy_Cell(level_->out, v);
    require (
      Value* undecayed = Unlift_Cell_No_Decay(level_->out)
    );
    return undecayed;
}


// We *could* allow direct `return v;` of arbitrary cells, with semantics that
// the dispatch machinery would just copy the cell contents into L->out.
//
// This is not done for two reasons:
//
// 1. The function dispatch is optimized to notice the literal pointer L->out
//    as the default common return result.  Copying to that location and
//    returning the L->out pointer avoids having to do further tests like
//    "is this a Cell?" and then "is this an API handle I need to free" etc.
//    We exploit the native's knowledge vs. having to rediscover it.
//
// 2. It avoids the potential of returning C stack variables (DECLARE_VALUE)
//    that would be dead after the return (only FRAME! cells or SCRATCH/SPARE
//    would be legal, but there wouldn't be a cheap check that these were
//    all you were returning.)
//
// Hence you're only allowed to return API Cells, and they are freed upon
// the return.  Core natives must `return COPY_TO_OUT(v);` for non-API cells.
//
INLINE void Native_Copy_Result_Untracked(Level* L, const Value* v) {
    assert(not Is_Api_Value(v));  // too easy to not release()
    Copy_Cell_Untracked(L->out, v);
}


//=//// LEVEL SHORTHAND MACROS ////////////////////////////////////////////
//
// Quick access functions from natives (or compatible functions that name a
// Level* pointer `level_`) to get some of the common public fields.
//
// There is an option to not define them due to conflicts with OUT as defined
// by the Windows.h headers.  This makes it easier for people who don't want
// to #undef the Windows versions and would rather pick their own shorthands,
// (if any).
//
// 1. Once these names were shorter, e.g. just COPY() vs. COPY_TO_OUT(), or
//    VOID vs. VOID_OUT.  But these more complex names help clarify what's
//    going on: that it's an active process, not a Cell*.  Or one might think
//    VOID was LIB(VOID), but it's actively illegal to `return LIB(VOID);`
//    from a native.  See Native_Copy_Result_Untracked() for why.
//
// 2. The idea of NULL_OUT or VOID_OUT etc. is that there's an operation done
//    on the output cell (Init_Null(OUT), Init_Void(OUT)).  BRANCHED_OUT
//    might imply something simliar, like (Init_Branched(OUT)), but we break
//    the pattern to and say OUT_BRANCHED to help cue that what you're
//    returning is OUT's contents as-is, *asserting* that it was branched.
//
// 3. Once it was relatively trivial to say `return nullptr;` vs to say
//    `return Init_Null(OUT);`, so it wasn't considered to be any big
//    savings to not return the output cell pointer directly.  That was
//    before Result(T), where more complex discernment is needed on the 0
//    bounce state to decide if it's an error or not.  It's now worth it to
//    `return NULL_OUT;` to avoid the extra checking.
//
// 4. Intrinsic typecheckers must return BOUNCE_OKAY or nullptr, so that they
//    do not overwrite OUT.  This means that trying to make LOGIC(b) "more
//    efficient by doing Init_Okay(OUT) or Init_Null(OUT) will break things.
//
// 5. `panic (UNHANDLED);` is a shorthand for something that's written often
//    enough in IMPLEMENT_GENERIC() handlers that it seems worthwhile.
//
//    !!! Once it was customized based on the "verb" of a generic, but that
//    mechanism has been removed.  Review what generic dispatch might do to
//    make this better (distinct BOUNCE_UNHANDLED that only the generic
//    dispatch mechanism understands, and slipstream verb into the generic
//    somehow?)
//
#if REBOL_LEVEL_SHORTHAND_MACROS
    #define LEVEL           level_
    #define STATE           LEVEL_STATE_BYTE(level_)
    #define BASELINE        (&level_->baseline)
    #define STACK_BASE      (level_->baseline.stack_base)

    #define SUBLEVEL    (assert(TOP_LEVEL->prior == level_), TOP_LEVEL)

    #define SPARE       Level_Spare(level_)
    #define SCRATCH     Level_Scratch(level_)

    #define OUT  level_->out

    #define COPY_TO_OUT(v) /* not COPY(v)...verbosity is intentional [1] */ \
        (Native_Copy_Result_Untracked(level_, (v)), x_cast(Bounce, TRACK(OUT)))

    #define OUT_BRANCHED /* OUT_BRANCHED vs. BRANCHED_OUT for a reason [2] */ \
        (assert(not Is_Light_Null(OUT) and not Is_Void(OUT)), \
            x_cast(Bounce, OUT))

    #define VOID_OUT \
        x_cast(Bounce, Init_Void(OUT))

    #define VOID_OUT_UNBRANCHED \
        x_cast(Bounce, Init_Void_Signifying_Unbranched(OUT))

    #define NULL_OUT /* `return NULL_OUT` is better than `return nullptr` */ \
        x_cast(Bounce, Init_Null(OUT))  // ...see [3] for why it's better!

    #define NULL_OUT_VETOING /* Note that BREAK is implemented via VETO */ \
        x_cast(Bounce, Init_Null_Signifying_Vetoed(OUT))  // [3]

    #define LOGIC_OUT(b) /* no Init_Okay/Null()! [4] */ \
        (exactly(bool, (b)) ? BOUNCE_OKAY : nullptr)

    #define TRASH_OUT  TRACK(Native_Trash_Result_Untracked(level_))

    #define UNLIFT_TO_OUT(v)  Native_Unlift_Result(level_, (v))

    #define THROWN  Native_Thrown_Result(level_)

    #define PANIC(p) \
        return Native_Panic_Result(level_, Derive_Error_From_Pointer(p))

    #define REQUIRE(_stmt_) /* based on needful_require */ \
        NEEDFUL_SCOPE_GUARD; \
        Needful_Assert_Not_Failing(); \
        _stmt_  needful_postfix_extract_result; \
        if (Needful_Get_Failure()) { \
            return PANIC(Needful_Test_And_Clear_Failure()); \
        } NEEDFUL_NOOP  /* force require semicolon at callsite */

    #define UNHANDLED \
        Error_Unhandled(level_)  // !!! REVIEW [5]
#endif


//=//// USE_LEVEL_SHORTHANDS() MACRO //////////////////////////////////////=//
//
// In many functions that take a Level* parameter, it's convenient to have
// that under a simple name (like `L`) but still want to use the shorthand
// macros like SPARE to refer to (&L->spare).  This macro simply defines the
// level_ alias for you...but makes it clearer why you're defining it.
//
// 1. The const constraint helps check you're not expecting it to change when
//    updating L (references are possible in C++, but not in C).
//
#if REBOL_LEVEL_SHORTHAND_MACROS
  #if CPLUSPLUS_11  // can add constraint you defined as `Level* const L` [1]
    #define USE_LEVEL_SHORTHANDS(L) \
        static_assert(std::is_const<decltype(L)>::value, "L must be const"); \
        Level* const level_ = L
  #else
    #define USE_LEVEL_SHORTHANDS(L) \
        Level* const level_ = L
  #endif
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
// 1. If the Evaluator_Executor() runs the Stepper_Executor() and that stepper
//    has pushed a LEVEL_FLAG_TRAMPOLINE_KEEPALIVE Level, you wind up with
//    a deeper stack of Levels that triggers this assert.  This needs a review
//    because the mechanism here is confusing.
//
#if NO_RUNTIME_CHECKS
    #define Adjust_Level_For_Downshift(L)  TOP_LEVEL
#else
    INLINE Level* Adjust_Level_For_Downshift(Level* L) {
        Level* temp = TOP_LEVEL;
        while (temp != L) {  // Cascaders can downshift Cascaders, etc.
            temp = temp->prior;
            assert(
                temp->executor == &Evaluator_Executor  // stepper [1]
                or temp->executor == &To_Or_As_Checker_Executor
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
