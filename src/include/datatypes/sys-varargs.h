//
//  File: %sys-varargs.h
//  Summary: {Definitions for Variadic Value Type}
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
// A VARARGS! represents a point for parameter gathering inline at the
// callsite of a function.  The point is located *after* that function has
// gathered all of its arguments and started running.  It is implemented by
// holding a reference to a reified FRAME! series, which allows it to find
// the point of a running evaluation (as well as to safely check for when
// that call is no longer on the stack, and can't provide data.)
//
// A second VARARGS! form is implemented as a thin proxy over an ANY-ARRAY!.
// This mimics the interface of feeding forward through those arguments, to
// allow for "parameter packs" that can be passed to variadic functions.
//
// When the bits of a payload of a VARARGS! are copied from one item to
// another, they are still maintained in sync.  TAKE-ing a vararg off of one
// is reflected in the others.  This means that the "indexor" position of
// the vararg is located through the frame pointer.  If there is no frame,
// then a single element array (the `array`) holds an ANY-ARRAY! value that
// is shared between the instances, to reflect the state.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * If the extra->binding of the varargs is not UNBOUND, it represents the
//   frame in which this VARARGS! was tied to a parameter.  This 0-based
//   offset can be used to find the param the varargs is tied to, in order
//   to know whether it is quoted or not (and its name for error delivery).
//
// * It can also find the arg.  Similar to the param, the arg is only good
//   for the lifetime of the FRAME! in extra->binding...but even less so,
//   because VARARGS! can (currently) be overwritten with another value in
//   the function frame at any point.  Despite this, we proxy the
//   CELL_FLAG_UNEVALUATED from the last TAKE to reflect its status.
//

#define CELL_MASK_VARARGS \
    CELL_FLAG_SECOND_IS_NODE

#define VAL_VARARGS_SIGNED_PARAM_INDEX(v) \
    PAYLOAD(Any, (v)).first.i

#define VAL_VARARGS_PHASE_NODE(v) \
    PAYLOAD(Any, (v)).second.node

#define VAL_VARARGS_PHASE(v) \
    ACT(VAL_VARARGS_PHASE_NODE(v))


inline static bool Is_Block_Style_Varargs(
    REBVAL **shared_out,
    REBCEL(const*) vararg
){
    assert(CELL_KIND(vararg) == REB_VARARGS);

    if (EXTRA(Binding, vararg).node->header.bits & ARRAY_FLAG_IS_VARLIST) {
        *shared_out = nullptr; // avoid compiler warning in -Og build
        return false; // it's an ordinary vararg, representing a FRAME!
    }

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <variadic> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    REBARR *array1 = ARR(EXTRA(Binding, vararg).node);
    *shared_out = SPECIFIC(ARR_HEAD(array1));
    assert(
        IS_END(*shared_out)
        or (IS_BLOCK(*shared_out) and ARR_LEN(array1) == 1)
    );

    return true;
}


inline static bool Is_Frame_Style_Varargs_Maybe_Null(
    REBFRM **f_out,
    REBCEL(const*) vararg
){
    assert(CELL_KIND(vararg) == REB_VARARGS);

    if (EXTRA(Binding, vararg).node->header.bits & ARRAY_FLAG_IS_VARLIST) {
        // "Ordinary" case... use the original frame implied by the VARARGS!
        // (so long as it is still live on the stack)

        *f_out = CTX_FRAME_IF_ON_STACK(CTX(EXTRA(Binding, vararg).node));
        return true;
    }

    *f_out = nullptr; // avoid compiler warning in -Og build
    return false; // it's a block varargs, made via MAKE VARARGS!
}


inline static bool Is_Frame_Style_Varargs_May_Fail(
    REBFRM **f_out,
    const RELVAL *vararg
){
    if (not Is_Frame_Style_Varargs_Maybe_Null(f_out, vararg))
        return false;

    if (not *f_out)
        fail (Error_Frame_Not_On_Stack_Raw());

    return true;
}


// !!! A left-hand-side variadic parameter is a complex concept.  It started
// out as a thought experiment, where the left was a "source of 0 or 1 args",
// in order to implement something like `<skip>`.  However, the need to create
// the SHOVE operator showed a more meaningful and technically complex
// interpretation of a variadic left-hand side, which used its right hand side
// to make a decision about how the left would be processed (quoted, tight,
// or normal).
//
// This new interpretation has not been fully realized, as SHOVE is very
// tricky.  So this enfix varargs implementation for userspace is old, where
// it lets the left hand side evaluate into a temporary array.  It really is
// just a placeholder for trying to rewire the mechanics used by SHOVE so that
// they can be offered to any userspace routine.
//
#define Is_Varargs_Enfix(v) \
    (VAL_VARARGS_SIGNED_PARAM_INDEX(v) < 0)


inline static const REBVAL *Param_For_Varargs_Maybe_Null(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_VARARGS);

    REBACT *phase = VAL_VARARGS_PHASE(v);
    if (phase) {
        REBARR *paramlist = ACT_PARAMLIST(phase);
        if (VAL_VARARGS_SIGNED_PARAM_INDEX(v) < 0) // e.g. enfix
            return SPECIFIC(ARR_AT(
                paramlist,
                - VAL_VARARGS_SIGNED_PARAM_INDEX(v)
            ));
        return SPECIFIC(ARR_AT(
            paramlist,
            VAL_VARARGS_SIGNED_PARAM_INDEX(v)
        ));
    }

    // A vararg created from a block AND never passed as an argument so no
    // typeset or quoting settings available.  Treat as "normal" parameter.
    //
    assert(not (EXTRA(Binding, v).node->header.bits & ARRAY_FLAG_IS_VARLIST));
    return nullptr;
}


#define Do_Vararg_Op_Maybe_End_Throws(out,op,vararg) \
    Do_Vararg_Op_Maybe_End_Throws_Core((out), (op), (vararg), REB_P_DETECT)
