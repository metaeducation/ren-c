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
// All THROWN values have two parts: the Value arg being thrown and a Value
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
// * When an abrupt panic occurs, it is intercepted by the trampoline and
//   converted into a throw state with an ERROR! as the label.  This state
//   is bubbled up the stack much like a throw, however it cannot be
//   intercepted by CATCH or definitional-error handlers like TRY.  Only
//   special routines like SYS.UTIL/RESCUE can catch abrupt panics, as
//   what they mean is too nebulous for arbitrary stacks to assume they
//   know how to handle them.
//


// 1. Since this is a macro, UNUSED would corrupt the variable.  Use USED().
//
// 2. An original constraint on asking if something was throwing was that only
//    the top frame could be asked about.  But Action_Executor() is called to
//    re-dispatch when there may be a frame above (kept there by request from
//    something like REDUCE).  We relax the constraint to only be able to
//    return true *if* there are no frames above on the stack.

#define Is_Throwing(L) \
    (USED(known(Level*, (L))),  /* don't UNUSED() [1] not used for now [2] */ \
        Not_Cell_Erased(&g_ts.thrown_arg))

#define THROWING Is_Throwing(level_)

INLINE const Element* VAL_THROWN_LABEL(Level* level_) {
    UNUSED(level_);
    assert(Is_Cell_Readable(&g_ts.thrown_label));
    return &g_ts.thrown_label;
}

#define Is_Throwing_Panic(level_) \
    Is_Error(VAL_THROWN_LABEL(level_))  // non-definitional errors [1]


#define Level_Out(L) \
    (&known(Level*, (L))->output)  // should be in sys-level.h


INLINE void Init_Thrown_With_Label(  // assumes `arg` in g_ts.thrown_arg
    Level* L,
    const Value* arg,
    const Element* label
){
    possibly(label == Level_Out(L));
    possibly(arg == Level_Out(L));

    assert(not Is_Throwing(L));

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    Copy_Cell(&g_ts.thrown_arg, arg);

    assert(Is_Cell_Erased(&g_ts.thrown_label));
    Copy_Cell(&g_ts.thrown_label, label);

    Erase_Cell(Level_Out(L));

    assert(Is_Throwing(L));
}

// When failures are put in the throw state, they are the label--not the value.
//
INLINE void Init_Thrown_Panic(Level* L, Error* error) {
    Init_Thrown_With_Label(
        TOP_LEVEL,
        LIB(NULL),
        Init_Error_Cell(Level_Out(L), error)  // error is the "label"
    );
}

INLINE void CATCH_THROWN(
    Value* arg_out,
    Level* L
){
    assert(Is_Throwing(L));

    Move_Cell(arg_out, &g_ts.thrown_arg);
    Erase_Cell(&g_ts.thrown_arg);

    Erase_Cell(&g_ts.thrown_label);

    assert(not Is_Throwing(L));

    UNUSED(L);

    g_ts.unwind_level = nullptr;
}
