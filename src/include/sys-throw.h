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
//   value can store either a REBFRM* which costs nothing extra, or a REBCTX*
//   which requires "reifying" the frame and making it GC-visible.  Reifying
//   would happen unconditionally if the frame is put into a global variable,
//   but so long as the FRAME! value bubbles up no higher than the REBFRM*
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

inline static const REBVAL *VAL_THROWN_LABEL(REBFRM *frame_) {
    UNUSED(frame_);
    assert(not Is_Stale_Void(&TG_Thrown_Label));
    return &TG_Thrown_Label;
}

inline static REB_R Init_Thrown_With_Label(  // assumes `arg` in TG_Thrown_Arg
    REBFRM *frame_,
    const REBVAL *arg,
    const REBVAL *label  // Note: is allowed to be same as `out`
){
    assert(Is_Stale_Void(&TG_Thrown_Arg));

    if (Is_Void(arg))
        RESET(&TG_Thrown_Arg);
    else
        Copy_Cell(&TG_Thrown_Arg, arg);

    assert(Is_Stale_Void(&TG_Thrown_Label));

    Copy_Cell(&TG_Thrown_Label, label);

    Mark_Eval_Out_Stale(frame_->out);
    return R_THROWN;
}

inline static void CATCH_THROWN(
    Cell *arg_out,
    REBFRM *frame_
){
    UNUSED(frame_);

    if (Is_Void(&TG_Thrown_Arg))
        RESET(arg_out);
    else
        Copy_Cell(arg_out, &TG_Thrown_Arg);

    Init_Stale_Void(&TG_Thrown_Arg);

    Init_Stale_Void(&TG_Thrown_Label);
}
