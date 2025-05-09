//
//  file: %sys-intrinsic.h
//  summary: "Helpers related to processing intrinsic arguments"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
//   BOUNCE_PANIC, and the Level's out pointer.



//=//// HELPERS TO PROCESS UNPROCESSED ARGUMENTS //////////////////////////=//
//
// ** WHEN RUN AS AN INTRINSIC, THE ARG IN THE SPARE CELL CONTAINS A FULLY NON
// TYPECHECKED META REPRESENTATION, AND THE NATIVE IS RESPONSIBLE FOR ALL
// ARGUMENT PROCESSING (INCLUDING <opt-out> or <undo-opt>).**
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
// !!! Since the intrinsic has to do all the work of writing a type check for
// the first argument, the case where dispatch is being done with a frame
// should use that same fast check code.  This will be viable once all the
//
// These helpers are used to perform the argument processing.
//


// Intrinsics always receive their arguments as a meta representation.  Many
// are not allowed to modify the SPARE cell.  This requirement of not
// modifying is important for instance in type checking... the typechecks
// have to be applied multiple times to the same value.
//
INLINE const Element* Level_Intrinsic_Arg_Meta(Level* L) {
    assert(Get_Level_Flag(L, DISPATCHING_INTRINSIC));
    return Known_Element(Level_Spare(L));
}


// If the intrinsic just wants to look at the heart byte and quote byte of
// an unconstrained ^META parameter, that can be done without making
// another Cell at the callsite.
//
// 1. Typechecker intrinsics aren't allowed to modify SPARE, because it is
//    used multiple times in the same type check.
//
INLINE void Get_Heart_And_Quote_Of_Atom_Intrinsic(
    Sink(Option(Heart)) heart,
    Sink(QuoteByte) quote_byte,
    Level* L
){
    const Element* meta;
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        meta = Known_Element(Level_Arg(L, 1));  // already checked
    else
        meta = Level_Intrinsic_Arg_Meta(L);

    *heart = Heart_Of(meta);
    assert(QUOTE_BYTE(meta) >= QUASIFORM_2);
    *quote_byte = QUOTE_BYTE(meta) - Quote_Shift(1);
    return;
}


// 1. The <opt-out> parameter convention has to be handled by the intrinsic,
//    so we test for void here.
//
// 2. While nullptr typically is handled as a dispatcher result meaning
//    Init_Nulled(OUT), the caller checks the return result of this routine
//    and considers nullptr to mean that the Element was successfully
//    extracted.  So if we actually want to return a null cell, we use
//    `return Init_Nulled(OUT)` here.
//
INLINE Option(Bounce) Trap_Bounce_Maybe_Element_Intrinsic(
    Sink(Element) out,
    Level* L  // writing OUT and SPARE is allowed in this helper
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Copy_Cell(out, Known_Element(Level_Arg(L, 1)));  // already checked
        return nullptr;
    }

    const Element* meta = Level_Intrinsic_Arg_Meta(L);

    if (Is_Meta_Of_Void(meta))  // do PARAMETER_FLAG_OPT_OUT [1]
        return Init_Nulled(L->out);  // !!! overwrites out, illegal [2]

    if (Is_Quasiform(meta))  // antiform including
        return BOUNCE_BAD_INTRINSIC_ARG;

    Copy_Cell(out, meta);
    Unquotify(out);

    return nullptr;
}

INLINE const Element* Get_Meta_Atom_Intrinsic(Level* L) {
    const Element* meta;
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        meta = Known_Element(Level_Arg(L, 1));  // already checked, and meta
    else
        meta = Level_Intrinsic_Arg_Meta(L);  // intrinsic arg always meta

    assert(QUOTE_BYTE(meta) >= QUASIFORM_2);
    return meta;
}

INLINE Option(Bounce) Trap_Bounce_Decay_Value_Intrinsic(
    Sink(Value) v,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Copy_Cell(v, Level_Arg(L, 1));  // already checked
        return nullptr;
    }

    const Element* meta = Level_Intrinsic_Arg_Meta(L);

    if (Is_Meta_Of_Error(meta))
        return Native_Panic_Result(L, Cell_Error(meta));

    Copy_Cell(v, meta);
    Meta_Unquotify_Undecayed(u_cast(Atom*, v));
    Decay_If_Unstable(u_cast(Atom*, v));
    return nullptr;
}

INLINE Option(Bounce) Trap_Bounce_Meta_Decay_Value_Intrinsic(
    Sink(Element) out,
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC)) {
        Element* meta = Known_Element(Level_Arg(L, 1));  // already checked
        Copy_Cell(out, meta);
        return nullptr;
    }

    const Element* meta = Level_Intrinsic_Arg_Meta(L);

    if (Is_Meta_Of_Error(meta))
        return Native_Panic_Result(L, Cell_Error(meta));

    Copy_Cell(out, meta);
    Meta_Unquotify_Undecayed(u_cast(Atom*, out));
    Decay_If_Unstable(u_cast(Atom*, out));
    Meta_Quotify(out);

    return nullptr;
}
