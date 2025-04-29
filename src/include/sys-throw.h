//
//  file: %sys-throw.h
//  summary: "Simulated datatype for throws, in lieu of TYPE_THROWN"
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
// All THROWN values have two parts: the Atom arg being thrown and a Value
// indicating the "label" of the throw.
//
// You cannot fit both values into a single value's bits of course.  One way
// to approach the problem would be to create a new TYPE_THROWN type with
// two fields (like a PAIR!).  But since there can only be one thrown value
// on an evaluator thread at a time, trampoline-globals are used instead.
//
// ALL calls into the evaluator to generate values must check for the thrown
// flag.  This is helped by naming conventions, e.g. `XXX_Throws()` to remind
// callers they have to handle it, pass it up the stack, or raise an uncaught
// throw exception.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * When an abrupt failure occurs, it is intercepted by the trampoline and
//   converted into a throw state with an ERROR! as the label.  This state
//   is bubbled up the stack much like a throw, however it cannot be
//   intercepted by CATCH or definitional-error handlers like TRY.  Only
//   special routines like SYS.UTIL/RESCUE can catch abrupt failures, as
//   what they mean is too nebulous for arbitrary stacks to assume they
//   know how to handle them.
//


// 1. An original constraint on asking if something was throwing was that only
//    the top frame could be asked about.  But Action_Executor() is called to
//    re-dispatch when there may be a frame above (kept there by request from
//    something like REDUCE).  We relax the constraint to only be able to
//    return true *if* there are no frames above on the stack.
//
INLINE bool Is_Throwing(Level* level_) {
    if (not Is_Cell_Erased(&g_ts.thrown_arg)) {
        possibly(level_ == TOP_LEVEL);  // don't enforce this for now [1]
        possibly(Is_Cell_Erased(level_->out));  // not enforced at present
        UNUSED(level_);
        return true;
    }
    return false;
}

#define THROWING Is_Throwing(level_)

INLINE const Value* VAL_THROWN_LABEL(Level* level_) {
    UNUSED(level_);
    assert(not Is_Cell_Erased(&g_ts.thrown_label));
    return &g_ts.thrown_label;
}

#define Is_Throwing_Failure(level_) \
    Is_Error(VAL_THROWN_LABEL(level_))  // non-definitional errors [1]

INLINE Bounce Init_Thrown_With_Label(  // assumes `arg` in g_ts.thrown_arg
    Level* L,
    const Atom* arg,
    const Value* label
){
    possibly(label == L->out);
    possibly(arg == L->out);

    assert(not Is_Throwing(L));

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    Copy_Cell(&g_ts.thrown_arg, arg);

    assert(Is_Cell_Erased(&g_ts.thrown_label));
    Copy_Cell(&g_ts.thrown_label, label);
    Deactivate_If_Action(&g_ts.thrown_label);

    Erase_Cell(L->out);

    assert(Is_Throwing(L));

    return BOUNCE_THROWN;
}

// When failures are put in the throw state, they are the label--not the value.
//
INLINE Bounce Init_Thrown_Failure(Level* L, Error* error) {
    UNUSED(L);
    return Init_Thrown_With_Label(
        TOP_LEVEL, LIB(NULL), Varlist_Archetype(error)  // error is the "label"
    );
}

INLINE void CATCH_THROWN(
    Atom* arg_out,
    Level* L
){
    assert(Is_Throwing(L));

    Move_Atom(arg_out, &g_ts.thrown_arg);

    Erase_Cell(&g_ts.thrown_label);

    assert(not Is_Throwing(L));

    UNUSED(L);

    g_ts.unwind_level = nullptr;
}
