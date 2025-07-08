//
//  file: %cell-varargs.h
//  summary: "Definitions for VARARGS! Cells"
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
// A VARARGS! represents a point for parameter gathering inline at the
// callsite of a function.  The point is located *after* that function has
// gathered all of its arguments and started running.  It is implemented by
// holding a reference to a reified FRAME!, which allows it to find
// the point of a running evaluation (as well as to safely check for when
// that call is no longer on the stack, and can't provide data.)
//
// A second VARARGS! form is implemented as a thin proxy over a BLOCK!.
// This mimics the interface of feeding forward through those arguments, to
// allow for "parameter packs" that can be passed to variadic functions.
//
// When the bits of a payload of a VARARGS! are copied from one item to
// another, they are still maintained in sync.  TAKE-ing a vararg off of one
// is reflected in the others.  This means that the array index position of
// the vararg is located through the level pointer.  If there is no level,
// then a single element array (the `array`) holds a BLOCK! value that
// is shared between the instances, to reflect the state.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * VARARGS! is mostly old code.  It was instrumental in pushing the design
//   toward having `Level` structures that could model an evaluation in
//   a persistent way...which eventually grew into the stackless model that
//   is available today.  But VARARGS! has not been tended to very much, and
//   has a lot of broken/sloppy/unfinished aspects.  It's not clear if it
//   will be kept going forward, or if functions will just be able to get
//   access to their FRAME! and simulate varargs-like behavior that way.
//
// * If CELL_VARARGS_EXTRA_ORIGIN of the varargs is not nullptr, it represents the
//   frame in which this VARARGS! was tied to a parameter.  A 0-based
//   offset can be used to find the param the varargs is tied to, in order
//   to know whether it is quoted or not (and its name for error delivery).
//

#define CELL_VARARGS_EXTRA_ORIGIN(c)  CELL_EXTRA(c)
#define CELL_VARARGS_SIGNED_PARAM_INDEX(c)  (c)->payload.split.one.i
#define CELL_VARARGS_PAYLOAD_2_PHASE(c)  CELL_PAYLOAD_2(c)

INLINE Phase* Extract_Cell_Varargs_Phase(const Cell* c) {
    assert(Heart_Of(c) == TYPE_VARARGS);
    return cast(Phase*, CELL_VARARGS_PAYLOAD_2_PHASE(c));
}

INLINE void Tweak_Cell_Varargs_Phase(Cell* c, Option(Phase*) phase) {
    assert(Heart_Of(c) == TYPE_VARARGS);
    CELL_VARARGS_PAYLOAD_2_PHASE(c) = maybe phase;
    if (phase)
        Clear_Cell_Flag(c, DONT_MARK_PAYLOAD_2);
    else
        Set_Cell_Flag(c, DONT_MARK_PAYLOAD_2);
}

INLINE Array* Cell_Varargs_Origin(const Cell* c) {
    assert(Heart_Of(c) == TYPE_VARARGS);
    return cast(Array*, CELL_VARARGS_EXTRA_ORIGIN(c));
}

INLINE void Tweak_Cell_Varargs_Origin(
    Cell* c,
    Stub* source  // either a feed, or a frame varlist
){
    assert(Heart_Of(c) == TYPE_VARARGS);
    CELL_VARARGS_EXTRA_ORIGIN(c) = source;
}


INLINE Element* Init_Varargs_Untyped_Normal(Init(Element) out, Level* L) {
    Reset_Cell_Header_Noquote(out, CELL_MASK_VARARGS);
    Tweak_Cell_Varargs_Origin(out, L->varlist);  // frame-based VARARGS!
    UNUSED(CELL_VARARGS_SIGNED_PARAM_INDEX(out));
    Tweak_Cell_Varargs_Phase(out, nullptr);  // set in typecheck
    return out;
}

INLINE Element* Init_Varargs_Untyped_Infix(
    Init(Element) out,
    Option(const Value*) left
){
    Stub* feed;
    if (not left)
        feed = g_empty_array;
    else {
        Source* singular = Alloc_Singular(STUB_MASK_MANAGED_SOURCE);
        Copy_Cell(Stub_Cell(singular), unwrap left);

        feed = require (Make_Untracked_Stub(
            FLAG_FLAVOR(FLAVOR_FEED) | BASE_FLAG_MANAGED
        ));
        Init_Block(Stub_Cell(feed), singular);  // index 0
    }

    Reset_Cell_Header_Noquote(out, CELL_MASK_VARARGS);
    Tweak_Cell_Varargs_Origin(out, feed);
    UNUSED(CELL_VARARGS_SIGNED_PARAM_INDEX(out));
    Tweak_Cell_Varargs_Phase(out, nullptr);  // set in typecheck
    return out;
}


INLINE bool Is_Block_Style_Varargs(
    Element* *shared_out,
    const Cell* vararg
){
    assert(Heart_Of(vararg) == TYPE_VARARGS);

    Array* source = Cell_Varargs_Origin(vararg);
    if (Is_Stub_Varlist(source)) {
      #if APPEASE_WEAK_STATIC_ANALYSIS
        *shared_out = nullptr;
      #endif
        return false;  // it's an ordinary vararg, representing a FRAME!
    }

    // Came from MAKE VARARGS! on some random block, hence not implicitly
    // filled by the evaluator on a <variadic> parameter.  Should be a singular
    // array with one BLOCK!, that is the actual array and index to advance.
    //
    // !!! Rethink interface to give back Option(Element*) vs. exposing an
    // implementation detail of a poisoned cell
    //
    Array* array1 = source;
    *shared_out = u_cast(Element*, Stub_Cell(array1));
    assert(Is_Cell_Poisoned(*shared_out) or Is_Block(*shared_out));

    return true;
}


INLINE bool Is_Level_Style_Varargs_Maybe_Null(
    Level* *L_out,
    const Cell* vararg
){
    assert(Heart_Of(vararg) == TYPE_VARARGS);

    Array* source = Cell_Varargs_Origin(vararg);
    if (Is_Stub_Varlist(source)) {
        // "Ordinary" case... use the original level implied by the VARARGS!
        // (so long as it is still live on the stack)

        *L_out = Level_Of_Varlist_If_Running(cast(VarList*, source));
        return true;
    }

  #if APPEASE_WEAK_STATIC_ANALYSIS
    *L_out = nullptr;
  #endif
    return false;  // it's a block varargs, made via MAKE VARARGS!
}


INLINE bool Is_Level_Style_Varargs_May_Panic(
    Level* *L_out,
    const Cell* vararg
){
    if (not Is_Level_Style_Varargs_Maybe_Null(L_out, vararg))
        return false;

    if (not *L_out)
        panic (Error_Frame_Not_On_Stack_Raw());

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
// tricky.  So this infix varargs implementation for userspace is old, where
// it lets the left hand side evaluate into a temporary array.  It really is
// just a placeholder for trying to rewire the mechanics used by SHOVE so that
// they can be offered to any userspace routine.
//
#define Is_Varargs_Infix(v) \
    (CELL_VARARGS_SIGNED_PARAM_INDEX(v) < 0)


INLINE const Param* Param_For_Varargs_Maybe_Null(
    const Key* *key,
    const Cell* v
){
    assert(Heart_Of(v) == TYPE_VARARGS);

    Phase* phase = Extract_Cell_Varargs_Phase(v);
    if (phase) {
        if (CELL_VARARGS_SIGNED_PARAM_INDEX(v) < 0) {  // e.g. infix
            if (key)
                *key = Phase_Key(
                    phase,
                    (- CELL_VARARGS_SIGNED_PARAM_INDEX(v))
                );
            return Phase_Param(
                phase,
                (- CELL_VARARGS_SIGNED_PARAM_INDEX(v))
            );
        }

        *key = Phase_Key(phase, CELL_VARARGS_SIGNED_PARAM_INDEX(v));
        return Phase_Param(phase, CELL_VARARGS_SIGNED_PARAM_INDEX(v));
    }

    if (key)
        *key = nullptr;

    // A vararg created from a block AND never passed as an argument so no
    // typeset or quoting settings available.  Treat as "normal" parameter.
    //
    assert(not Is_Stub_Varlist(Cell_Varargs_Origin(v)));
    return nullptr;
}


#define Do_Vararg_Op_Maybe_End_Throws(out,op,vararg) \
    Do_Vararg_Op_Maybe_End_Throws_Core((out), (op), (vararg), PARAMCLASS_0)
