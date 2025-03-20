//
//  File: %sys-intrinsic.h
//  Summary: "Helpers related to processing intrinsic arguments"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2024 Ren-C Open Source Contributors
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
// If a native is declared as `native:intrinsic`, then it will carry the
// DETAILS_FLAG_CAN_DISPATCH_AS_INTRINSIC flag.  This means that when it
// takes only one argument, it's capable of running without a Level allocation
// of its own...being instead passed the Level* of the intrinsic's caller.
//
// The trick is that the argument and action are multiplexed onto the parent
// Level, by making it give up its SPARE and SCRATCH cells.  The SPARE holds
// the single argument, and the SCRATCH holds the action--so that any instance
// data can be accessed (e.g. a Typechecker can find the TypesetByte that
// applies to that Action instance, even though they all use a common
// C function Dispatcher).
//
// Intrinsics can also be run with their own Level and FRAME!--either when
// being called with refinements (thus needing more than one argument), for
// purposes that need frames (like specialization), or in the future it may
// be for debugging modes that want to see reified levels for all steps.
// Detecting whether or not the dispatch is intrinsic is done by checking
// LEVEL_FLAG_DISPATCHING_INTRINSIC.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Intrinsics can only return Bounce values of: nullptr, BOUNCE_OKAY,
//   BOUNCE_FAIL, and the Level's out pointer.



//=//// HELPERS TO PROCESS UNPROCESSED ARGUMENTS //////////////////////////=//
//
// ** WHEN RUN AS AN INTRINSIC, THE ARG IN THE SPARE CELL CONTAINS A FULLY NON
// TYPECHECKED, NON-META, NON-DECAYED, NON-NOOP-IF-VOID ATOM...AND THE NATIVE
// IS RESPONSIBLE FOR ALL ARGUMENT PROCESSING.**
//
// Not only that, but the special case of typechecking intrinsics (that
// return LOGIC?) is that they can't write to L->out...because if you were
// typechecking the argument in the output cell, checking would overwrite it.
// Instead they have to communicate their result with BOUNCE_OKAY or nullptr
// as the native return result.  Plus, they can't modify the arg in SPARE,
// since type checks are applied multiple times to the same value!  :-/
//
// The goal is making intrinsic dispatch cheap.  And quite simply, it won't
// be cheap if you turn around and have to do typechecking on the argument,
// because that would entail calling more natives.  Furthermore, more natives
// could not use the intrinsic trick...because the SPARE and SCRATCH are
// already committed to the intrinsic that's running.  It would undermine
// the whole point of intrinsics to typecheck their argument.
//
// It might seem that since the intrinsic has to do all the work of writing
// a type check for the first argument, that the case where dispatch is
// being done with a frame should use that same fast check code.  But this
// would create bad invariants...like making varlists with unstable antiforms
// in them, and the frame may be created for tweaking by other routines
// before running the native (if it ever runs it).  So we can't have a
// corrupted frame, so better to branch on LEVEL_FLAG_DISPATCHING_INTRINSIC
// and assume the first argument is checked if that's not set.
//
// These helpers are used to perform the argument processing.
//


// If the intrinsic just wants to look at the heart byte and quote byte of
// an unconstrained ^META parameter, that can be done without making
// another Cell at the callsite.
//
INLINE void Get_Heart_And_Quote_Of_Atom_Intrinsic(
    Sink(Heart) heart,
    Sink(QuoteByte) quote_byte,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Value* arg = Level_Arg(L, 1);  // already checked
        *heart = Cell_Heart(arg);
        assert(QUOTE_BYTE(arg) >= QUASIFORM_2);
        *quote_byte = QUOTE_BYTE(arg) - Quote_Shift(1);  // calculate "unmeta"
        return;
    }

    Atom* arg = Level_Spare(L);  // typecheckers can't modify SPARE (!)
    *heart = Cell_Heart(arg);
    *quote_byte = QUOTE_BYTE(arg);  // not meta, as-is
    return;
}

// 1. While nullptr typically is handled as a dispatcher result meaning
//    Init_Nulled(OUT), the caller checks the return result of this routine
//    and considers nullptr to mean that the Element was successfully
//    extracted.  So if we actually want to return a null cell, we use
//    `return Init_Nulled(OUT)` here.
//
// 2. The <maybe> parameter convention has to be handled by the intrinsic,
//    so we test for void here.
//
INLINE Option(Bounce) Trap_Bounce_Maybe_Element_Intrinsic(
    Sink(Element) e,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Copy_Cell(e, u_cast(Element*, Level_Arg(L, 1)));
        return nullptr;  // already checked
    }

    Atom* arg = Level_Spare(L);

    if (Is_Raised(arg))
        return BOUNCE_BAD_INTRINSIC_ARG;

    Copy_Cell(u_cast(Atom*, e), arg);
    Decay_If_Unstable(u_cast(Atom*, e));  // not necessarily Element, yet...

    if (QUOTE_BYTE(e) != ANTIFORM_0)  // it's an Element if this is true
        return nullptr;  // means "no bounce" in this case [1]

    if (Is_Void(u_cast(Value*, arg)))  // do PARAMETER_FLAG_NOOP_IF_VOID [2]
        return Init_Nulled(L->out);  // can't return nullptr [1]

    return BOUNCE_BAD_INTRINSIC_ARG;
}

INLINE void Get_Meta_Atom_Intrinsic(  // can't modify arg of intrinsic!
    Sink(Element) meta,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Value* arg = Level_Arg(L, 1);  // already checked, already meta
        assert(QUOTE_BYTE(arg) >= QUASIFORM_2);
        Copy_Cell(meta, cast(Element*, arg));
        return;
    }

    Copy_Meta_Cell(meta, Level_Spare(L));  // typecheckers can't modify SPARE
    return;
}

INLINE Option(Bounce) Trap_Bounce_Decay_Value_Intrinsic(
    Sink(Value) v,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Copy_Cell(v, Level_Arg(L, 1));  // already checked
        return nullptr;
    }

    Atom* arg = Level_Spare(L);  // typecheckers can't modify SPARE

    if (Is_Raised(arg))
        return BOUNCE_BAD_INTRINSIC_ARG;

    Copy_Cell(u_cast(Atom*, v), arg);
    Decay_If_Unstable(u_cast(Atom*, v));
    return nullptr;
}

INLINE Option(Bounce) Trap_Bounce_Meta_Decay_Value_Intrinsic(
    Sink(Element) meta,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Copy_Cell(meta, u_cast(Element*, Level_Arg(L, 1)));  // already checked
        return nullptr;
    }

    Atom* arg = Level_Spare(L);  // typecheckers can't modify SPARE

    if (Is_Raised(arg))
        return Native_Fail_Result(L, Cell_Error(arg));

    Copy_Cell(u_cast(Atom*, meta), arg);
    Decay_If_Unstable(u_cast(Atom*, meta));
    Meta_Quotify(meta);

    return nullptr;
}
