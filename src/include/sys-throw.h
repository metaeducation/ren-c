//
//  File: %sys-throw.h
//  Summary: {Simulated datatype for throws, in lieu of REB_THROWN}
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
// All THROWN values have two parts: the REBVAL arg being thrown and
// a REBVAL indicating the /NAME of a labeled throw.  (If the throw was
// created with plain THROW instead of THROW/NAME then its name is BLANK!).
//
// You cannot fit both values into a single value's bits of course.  One way
// to approach the problem would be to create a new REB_THROWN type with
// two fields (like a PAIR!).  But since there can only be one thrown value
// on an evaluator thread at a time, a more efficient trick is used.  The
// throw label is shuffled up the stack via the output cell, with the arg
// put off to the side.
//
// There are important technical reasons for favoring the label as the output:
//
// * RETURN is implemented as a throw whose label is a FRAME!.  That FRAME!
//   value can store either a Frame(*) which costs nothing extra, or a Context(*)
//   which requires "reifying" the frame and making it GC-visible.  Reifying
//   would happen unconditionally if the frame is put into a global variable,
//   but so long as the FRAME! value bubbles up no higher than the Frame(*)
//   it points to, it can be used as-is.  With RETURN, it will be exactly the
//   right lifetime--since the originating frame is right where it stops.
//
// * When various stack levels are checking for their interest in a thrown
//   value, they look at the label...and if it's not what they want, they
//   pass it on.  So the label is checked many times, while the arg is only
//   caught once at its final location.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * While similar to ERROR!s that are "raised" with FAIL, throwing is a
//   lighter-weight mechanism and doesn't subvert the C call stack.
//
// * ALL calls into the evaluator to generate values must check for the
//   thrown flag.  This is helped by naming conventions, e.g. `XXX_Throws()`
//   to remind callers they have to handle it, pass it up the stack, or
//   raise an uncaught throw exception.
//
// * VAL_THROWN_LABEL() should be used vs. direct access of a thrown out
//   cell.  This abstracts the mechanism and allows the debug build to do
//   more checking that thrown values aren't being dropped or misused.
//

inline static const REBVAL *VAL_THROWN_LABEL(Frame(*) frame_) {
    UNUSED(frame_);
    assert(not Is_Cell_Erased(&TG_Thrown_Label));
    return &TG_Thrown_Label;
}

inline static Bounce Init_Thrown_With_Label(  // assumes `arg` in TG_Thrown_Arg
    Frame(*) frame_,
    const REBVAL *arg,
    const REBVAL *label  // Note: is allowed to be same as `out`
){
    assert(not THROWING);

    assert(Is_Cell_Erased(&TG_Thrown_Arg));
    Copy_Cell(&TG_Thrown_Arg, arg);

    assert(Is_Cell_Erased(&TG_Thrown_Label));
    Copy_Cell(&TG_Thrown_Label, label);

    assert(THROWING);

    FRESHEN(frame_->out);
    return BOUNCE_THROWN;
}

// When errors are put in the throw state, they are the label--not the value.
//
inline static Bounce Init_Thrown_Error(Frame(*) frame, Value(const*) error) {
    assert(IS_ERROR(error));
    return Init_Thrown_With_Label((frame), Lib(NULL), (error));
}

inline static void CATCH_THROWN(
    Cell(*) arg_out,
    Frame(*) frame_
){
    UNUSED(frame_);

    assert(THROWING);

    Copy_Cell(arg_out, &TG_Thrown_Arg);

    Erase_Cell(&TG_Thrown_Arg);
    Erase_Cell(&TG_Thrown_Label);

    assert(not THROWING);

    TG_Unwind_Frame = nullptr;
}


inline static void Drop_Frame(Frame(*) f);

// When you're sure that the value isn't going to be consumed by a multireturn
// then use this to get the first value unmeta'd
//
inline static Value(*) Decay_If_Isotope(Value(*) v) {
    if (not Is_Isotope(v))
        return v;

    if (Is_Lazy(v)) {
        if (not Pushed_Reifying_Frame(v, v, FRAME_MASK_NONE))
            return v;  // cheap reification
        if (Trampoline_With_Top_As_Root_Throws())
            fail (Error_No_Catch_For_Throw(TOP_FRAME));
        Drop_Frame(TOP_FRAME);
        return v;
    }

    if (Is_Pack(v)) {  // iterate until result is not multi-return, see [1]
        Cell(const*) pack_meta_tail;
        Cell(const*) pack_meta_at = VAL_ARRAY_AT(&pack_meta_tail, v);
        if (pack_meta_at == pack_meta_tail)
            fail ("No value in isotopic BLOCK! pack: ~[]~");  // treat as void?
        Derelativize(v, pack_meta_at, VAL_SPECIFIER(v));
        Meta_Unquotify(v);
        if (Is_Pack(v))
            fail (Error_Bad_Isotope(v));  // need more granular unpacking
        return v;
    }

    return v;
}
