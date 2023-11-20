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
// is reflected in the others.  This means that the array index position of
// the vararg is located through the level pointer.  If there is no level,
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

#define VAL_VARARGS_SIGNED_PARAM_INDEX(v) \
    PAYLOAD(Any, (v)).first.i

#define INIT_VAL_VARARGS_PHASE          Init_Cell_Node2
#define VAL_VARARGS_PHASE(v)            cast(Action*, Cell_Node2(v))

inline static Array* VAL_VARARGS_BINDING(NoQuote(const Cell*) v) {
    assert(Cell_Heart(v) == REB_VARARGS);
    return cast(Array*, BINDING(v));  // may be varlist or plain array
}

inline static void INIT_VAL_VARARGS_BINDING(
    Cell* v,
    Array* binding  // either an array or a frame varlist
){
    assert(IS_VARARGS(v));
    mutable_BINDING(v) = binding;
}


inline static REBVAL *Init_Varargs_Untyped_Normal(Cell* out, Level* L) {
    Reset_Unquoted_Header_Untracked(out, CELL_MASK_VARARGS);
    mutable_BINDING(out) = L->varlist;  // frame-based VARARGS!
    UNUSED(VAL_VARARGS_SIGNED_PARAM_INDEX(out));
    INIT_VAL_VARARGS_PHASE(out, nullptr);  // set in typecheck
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Varargs_Untyped_Enfix(
    Sink(Value(*)) out,
    Option(Value(const*)) left
){
    Array* feed;
    if (not left or Is_Void(unwrap(left)))
        feed = EMPTY_ARRAY;
    else {
        Array* singular = Alloc_Singular(NODE_FLAG_MANAGED);
        Copy_Cell(Array_Single(singular), unwrap(left));

        feed = Alloc_Singular(FLAG_FLAVOR(FEED) | NODE_FLAG_MANAGED);
        Init_Block(Array_Single(feed), singular);  // index 0
    }

    Reset_Unquoted_Header_Untracked(out, CELL_MASK_VARARGS);
    INIT_VAL_VARARGS_BINDING(out, feed);
    UNUSED(VAL_VARARGS_SIGNED_PARAM_INDEX(out));
    INIT_VAL_VARARGS_PHASE(out, nullptr);  // set in typecheck
    return out;
}


inline static bool Is_Block_Style_Varargs(
    REBVAL **shared_out,
    NoQuote(const Cell*) vararg
){
    assert(Cell_Heart(vararg) == REB_VARARGS);

    Array* binding = cast(Array*, BINDING(vararg));
    if (IS_VARLIST(binding)) {
        *shared_out = nullptr;  // avoid compiler warning in -Og build
        return false;  // it's an ordinary vararg, representing a FRAME!
    }

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <variadic> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    Array* array1 = binding;
    *shared_out = cast(REBVAL*, Array_Single(array1));
    assert(
        Is_Cell_Poisoned(*shared_out)
        or (IS_SPECIFIC(cast(Cell*, *shared_out)) and IS_BLOCK(*shared_out))
    );

    return true;
}


inline static bool Is_Level_Style_Varargs_Maybe_Null(
    Level* *L_out,
    NoQuote(const Cell*) vararg
){
    assert(Cell_Heart(vararg) == REB_VARARGS);

    Array* binding = cast(Array*, BINDING(vararg));
    if (IS_VARLIST(binding)) {
        // "Ordinary" case... use the original level implied by the VARARGS!
        // (so long as it is still live on the stack)

        *L_out = CTX_LEVEL_IF_ON_STACK(cast(Context*, binding));
        return true;
    }

    *L_out = nullptr;  // avoid compiler warning in -Og build
    return false;  // it's a block varargs, made via MAKE VARARGS!
}


inline static bool Is_Level_Style_Varargs_May_Fail(
    Level* *L_out,
    const Cell* vararg
){
    if (not Is_Level_Style_Varargs_Maybe_Null(L_out, vararg))
        return false;

    if (not *L_out)
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


inline static const Param* Param_For_Varargs_Maybe_Null(
    const Key* *key,
    NoQuote(const Cell*) v
){
    assert(Cell_Heart(v) == REB_VARARGS);

    Action* phase = VAL_VARARGS_PHASE(v);
    if (phase) {
        Array* paramlist = CTX_VARLIST(ACT_EXEMPLAR(phase));
        if (VAL_VARARGS_SIGNED_PARAM_INDEX(v) < 0) {  // e.g. enfix
            if (key)
                *key = ACT_KEY(
                    phase,
                    (- VAL_VARARGS_SIGNED_PARAM_INDEX(v))
                );
            return cast(Param*, Array_At(
                paramlist,
                - VAL_VARARGS_SIGNED_PARAM_INDEX(v)
            ));
        }

        *key = ACT_KEY(
            phase,
            VAL_VARARGS_SIGNED_PARAM_INDEX(v)
        );
        return cast(Param*, Array_At(
            paramlist,
            VAL_VARARGS_SIGNED_PARAM_INDEX(v)
        ));
    }

    if (key)
        *key = nullptr;

    // A vararg created from a block AND never passed as an argument so no
    // typeset or quoting settings available.  Treat as "normal" parameter.
    //
    assert(not IS_VARLIST(BINDING(v)));
    return nullptr;
}


#define Do_Vararg_Op_Maybe_End_Throws(out,op,vararg) \
    Do_Vararg_Op_Maybe_End_Throws_Core((out), (op), (vararg), PARAM_CLASS_0)
