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
//   and the Level's out pointer.  nullptr can mean NEEDFUL_RESULT_0
//   (fail/panic) or Init_Nulled(OUT) (falsey)



//=//// HELPERS TO PROCESS UNPROCESSED ARGUMENTS //////////////////////////=//
//
// ** WHEN RUN AS AN INTRINSIC, THE ARG IN THE SPARE CELL CONTAINS A FULLY NON
// TYPECHECKED META REPRESENTATION, AND THE NATIVE IS RESPONSIBLE FOR ALL
// ARGUMENT PROCESSING (INCLUDING <opt> or <opt-out>).**
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


// Intrinsics always receive their arguments as a Lifted representation.
//
// 1. It was once the case that typechecking intrinsics protected their args,
//    but the typechecking process proved destructive anyways (e.g. stripping
//    off quotes etc.) and always copied the checked value back into the
//    spare cell for each dispatch.  So now it's always mutable.
//
INLINE Value* Level_Dispatching_Intrinsic_Arg(Level* L) {
    assert(Get_Level_Flag(L, DISPATCHING_INTRINSIC));

    assert(Not_Cell_Flag(Level_Spare(L), PROTECTED));  // no longer true [1]
    return Level_Spare(L);
}

INLINE Details* Level_Intrinsic_Details(Level* L) {
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Ensure_Level_Details(L);

    Stable* frame = Known_Stable(Level_Scratch(L));
    possibly(Is_Antiform(frame));  // LIFT_BYTE() is not canonized
    return Ensure_Frame_Details(frame);
}

INLINE Option(const Symbol*) Level_Intrinsic_Label(Level* L) {
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Try_Get_Action_Level_Label(L);

    Stable* frame = Known_Stable(Level_Scratch(L));
    possibly(Is_Antiform(frame));  // LIFT_BYTE() is not canonized
    return Frame_Label_Deep(frame);
}


// Unchecked argument to an intrinsic function, adjusted for whether you
// are dispatching an intrinsic or not.
//
// Typically use this when you take a ^value with typespec [any-value?]
//
// !!! Make this a macro that can't be accidentally used w/non-intrinsic.
//
INLINE Value* Intrinsic_ARG(Level* L) {
    if (Get_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Level_Spare(L);

    return Level_Arg(L, 1);
}



//=//// INTRINSIC FUNCTION ARGUMENT PROCESSING HELPERS ////////////////////=//
//
// If an intrinsic function is dispatched as an intrinsic, then it has to take
// on its own typechecking for its argument.  This includes handling the
// <opt-out> convention.
//
// 1. We can't return an Option(Bounce) here, because the nullptr signal has
//    to be used in typechecking to return a falsey result without overwriting
//    the OUT cell.  Any bounce value that doesn't ovewrite OUT and isn't
//    returned by the checker could be used here...and since the only bounce
//    value it does return at the moment is `nullptr` we use BOUNCE_OKAY.
//
// 2. There's an unusual situation arising due to the fact that we're doing
//    the typecheck "inside the function call": we *might* or *might not* want
//    to execute a panic() if the typecheck fails.  The case where we do not
//    is when we've dispatched an intrinsic to do a typecheck, and it's
//    enough to just return nullptr as if the typecheck didn't succeed.
//

#define BOUNCE_GOOD_INTRINSIC_ARG  BOUNCE_OKAY  // doesn't write OUT [1]


// Handling for intrinsic args that are [<opt-out> element?], since they do
// not necessarily do typechecking themselves.
//
// If it returns nullptr, then the caller should return nullptr.
//
INLINE Result(Option(Element*)) Typecheck_Element_Intrinsic_Arg(
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Known_Element(Level_Arg(L, 1));  // was checked

    Value* arg = Level_Dispatching_Intrinsic_Arg(L);

    if (Is_Antiform(arg)) {
        if (Get_Level_Flag(L, RUNNING_TYPECHECK))
            return nullptr;  // [2]
        return fail (Error_Bad_Intrinsic_Arg_1(L));
    }

    return Known_Element(arg);
}

// Because decay can call the evaluator (e.g. for GETTER or ALIAS that is
// decaying) the machinery has to do that before the intrinsic, as we do not
// want frameless natives on the stack above an evaluation (which might want
// to introspect the stack and isn't prepared to see an intrinsic there).
//
// If the parameter is <opt-out> that is handled prior to this as well.
//
INLINE Stable* Stable_Decayed_Intrinsic_Arg(
    Level* L
){
    if (Not_Level_Flag(L, DISPATCHING_INTRINSIC))
        return Known_Stable(Level_Arg(L, 1));  // was checked

    Value* arg = Level_Dispatching_Intrinsic_Arg(L);
    return Known_Stable(arg);
}
