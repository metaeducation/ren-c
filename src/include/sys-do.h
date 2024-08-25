//
//  File: %sys-do.h
//  Summary: {DO-until-end (of block or variadic feed) evaluation API}
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
// The "DO" helpers have names like Do_XXX(), and are a convenience layer
// over making repeated calls into the Eval_XXX() routines.  DO-ing things
// always implies running to the end of an input.  It also implies returning
// void if nothing can be synthesized, otherwise let the last value fall out:
//
//     >> eval [1 + 2]
//     == 3
//
//     >> eval []
//     == ~void~  ; anti
//
//     >> eval [1 + 2 comment "hi"]
//     == 3
//
// See %sys-eval.h for the lower level routines if this isn't enough control.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//


#define rebRunThrows(out,...) \
    rebRunCoreThrows( \
        (out), \
        EVAL_EXECUTOR_FLAG_NO_RESIDUE, \
        __VA_ARGS__ \
    )



INLINE bool Do_Any_List_At_Core_Throws(
    Atom* out,
    Flags flags,
    const Cell* list,
    Specifier* specifier
){
    Init_Void(Alloc_Evaluator_Primed_Result());
    Level* L = Make_Level_At_Core(
        &Evaluator_Executor,
        list, specifier,
        flags
    );

    return Trampoline_Throws(out, L);
}

#define Do_Any_List_At_Throws(out,list,specifier) \
    Do_Any_List_At_Core_Throws(out, LEVEL_MASK_NONE, (list), (specifier))



INLINE Bounce Run_Generic_Dispatch_Core(
    const Value* first_arg,  // !!! Is this always same as Level_Arg(L, 1)?
    Level* L,
    const Symbol* verb
){
    GENERIC_HOOK *hook;
    switch (QUOTE_BYTE(first_arg)) {
      case ANTIFORM_0:
        hook = &T_Antiform;
        break;
      case NOQUOTE_1:
        hook = Generic_Hook_For_Type_Of(first_arg);
        break;
      case QUASIFORM_2:
        hook = &T_Quasiform;
        break;
      default:
        hook = &T_Quoted;  // a few things like COPY are supported by QUOTED!
        break;
    }

    return hook(L, verb);  // Note QUOTED! has its own hook & handling;
}


// Some routines invoke Run_Generic_Dispatch(), go ahead and reduce the
// cases they have to look at by moving any ordinary outputs into L->out, and
// make throwing the only exceptional case they have to handle.
//
INLINE bool Run_Generic_Dispatch_Throws(
    const Value* first_arg,  // !!! Is this always same as Level_Arg(L, 1)?
    Level* L,
    const Symbol* verb
){
    Bounce b = Run_Generic_Dispatch_Core(first_arg, L, verb);

    if (b == L->out) {
         // common case
    }
    else if (b == nullptr) {
        Init_Nulled(L->out);
    }
    else if (Is_Bounce_An_Atom(b)) {
        Atom* r = Atom_From_Bounce(b);
        assert(Is_Api_Value(r));
        Copy_Cell(L->out, r);
        Release_Api_Value_If_Unmanaged(r);
    }
    else {
        if (b == BOUNCE_THROWN)
            return true;
        assert(!"Unhandled return signal from Run_Generic_Dispatch_Core");
    }
    return false;
}


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

INLINE Bounce Continue_Sublevel_Helper(
    Level* L,
    bool catches,
    Level* sub
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
        (sub_flags) | (level_->flags.bits & LEVEL_FLAG_RAISED_RESULT_OK), \
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



INLINE bool Do_Branch_Throws(  // !!! Legacy code, should be phased out
    Atom* out,
    const Value* branch
){
    if (not Pushed_Continuation(
        out,
        LEVEL_FLAG_BRANCH,
        SPECIFIED, branch,
        nullptr
    )){
        return false;
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Level(TOP_LEVEL);
    return threw;
}
