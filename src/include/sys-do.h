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
//     >> do [1 + 2]
//     == 3
//
//     >> do []
//     ; void
//
//     >> do [1 + 2 comment "hi"]
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


// (Used by DO and EVALUATE)
//
// If `source` is not const, tweak it to be explicitly mutable--because
// otherwise, it would wind up inheriting the FEED_MASK_CONST of our
// currently executing frame.  That's no good for `repeat 2 [do block]`,
// because we want whatever constness is on block...
//
// (Note we *can't* tweak values that are Cell in source.  So we either
// bias to having to do this or Do_XXX() versions explode into passing
// mutability parameters all over the place.  This is better.)
//
inline static void Tweak_Non_Const_To_Explicitly_Mutable(Value(*) source) {
    if (Not_Cell_Flag(source, CONST))
        Set_Cell_Flag(source, EXPLICITLY_MUTABLE);
}

inline static bool Do_Any_Array_At_Core_Throws(
    REBVAL *out,
    Flags flags,
    noquote(Cell(const*)) any_array,
    REBSPC *specifier
){
    Frame(*) f = Make_Frame_At_Core(any_array, specifier, flags);
    f->executor = &Array_Executor;

    return Trampoline_Throws(out, f);
}

#define Do_Any_Array_At_Throws(out,any_array,specifier) \
    Do_Any_Array_At_Core_Throws(out, FRAME_MASK_NONE, (any_array), (specifier))


inline static bool Do_Branch_Throws(  // !!! Legacy code, should be phased out
    REBVAL *out,
    const REBVAL *branch
){
    if (not Pushed_Continuation(
        out,
        FRAME_FLAG_BRANCH,
        SPECIFIED, branch,
        nullptr
    )){
        return false;
    }

    bool threw = Trampoline_With_Top_As_Root_Throws();
    Drop_Frame(TOP_FRAME);
    return threw;
}


inline static Bounce Run_Generic_Dispatch_Core(
    const REBVAL *first_arg,  // !!! Is this always same as FRM_ARG(f, 1)?
    Frame(*) f,
    Symbol(const*) verb
){
    GENERIC_HOOK *hook;
    switch (QUOTE_BYTE(first_arg)) {
      case ISOTOPE_0:
        hook = &T_Isotope;
        break;
      case UNQUOTED_1:
        hook = Generic_Hook_For_Type_Of(first_arg);
        break;
      case QUASI_2:
        hook = &T_Quasi;
        break;
      default:
        hook = &T_Quoted;  // a few things like COPY are supported by QUOTED!
        break;
    }

    return hook(f, verb);  // Note QUOTED! has its own hook & handling;
}


// Some routines invoke Run_Generic_Dispatch(), go ahead and reduce the
// cases they have to look at by moving any ordinary outputs into f->out, and
// make throwing the only exceptional case they have to handle.
//
inline static bool Run_Generic_Dispatch_Throws(
    const REBVAL *first_arg,  // !!! Is this always same as FRM_ARG(f, 1)?
    Frame(*) f,
    Symbol(const*) verb
){
    Bounce b = Run_Generic_Dispatch_Core(first_arg, f, verb);

    if (b == f->out) {
         // common case
    }
    else if (b == nullptr) {
        Init_Nulled(f->out);
    }
    else if (Is_Bounce_A_Value(b)) {
        REBVAL *r = Value_From_Bounce(b);
        assert(Is_Api_Value(r));
        Copy_Cell(f->out, r);
        Release_Api_Value_If_Unmanaged(r);
    }
    else {
        if (b == BOUNCE_THROWN)
            return true;
        assert(!"Unhandled return signal from Run_Generic_Dispatch_Core");
    }
    return false;
}
