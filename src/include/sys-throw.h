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

#if defined(NDEBUG)
    #define VAL_THROWN_LABEL(thrown) \
        (thrown)
#else
    inline static const REBVAL *VAL_THROWN_LABEL(const REBVAL *thrown) {
        if (Is_Stale_Void(&TG_Thrown_Label_Debug))
            return thrown;
        assert(Is_Isotope_With_Id(thrown, SYM_THROW));
        return &TG_Thrown_Label_Debug;
    }
#endif

inline static REBVAL *Init_Thrown_With_Label(  // assumes `arg` in TG_Thrown_Arg
    REBVAL *out,
    const REBVAL *arg,
    const REBVAL *label  // Note: is allowed to be same as `out`
){
    assert(Is_Stale_Void(&TG_Thrown_Arg));

    if (Is_Void(arg))
        RESET(&TG_Thrown_Arg);
    else
        Copy_Cell(&TG_Thrown_Arg, arg);

  #if defined(NDEBUG)
    if (out != label)
        Copy_Cell(out, label);
  #else
    assert(Is_Stale_Void(&TG_Thrown_Label_Debug));

    // Help avoid accidental uses of thrown output as misunderstood plain
    // outputs, by forcing thrown label access through VAL_THROWN_LABEL()...
    // but still test the release code path half the time.  (Causes different
    // reifications, but outside performance should still work the same.)
    //
    if (SPORADICALLY(2)) {
        Copy_Cell(&TG_Thrown_Label_Debug, label);
        Init_Isotope(out, Canon(THROW));
    }
    else {
        if (out != label)
            Copy_Cell(out, label);
    }
  #endif

    return out; // for chaining to dispatcher output
}

inline static void CATCH_THROWN(
    Cell *arg_out,
    REBVAL *thrown // Note: may be same pointer as arg_out
){
    if (Is_Void(&TG_Thrown_Arg))
        RESET(arg_out);
    else
        Copy_Cell(arg_out, &TG_Thrown_Arg);

    Init_Stale_Void(&TG_Thrown_Arg);

  #if defined(NDEBUG)
    UNUSED(thrown);
  #else
    // The debug build may have taken the throw label out of the output slot
    // and put it into a variable on the side (this helps avoid non-throw
    // aware reads of f->out).  If this was done, put it back after catch
    // (it is done SPORADICALLY()).  Only do so if they weren't trying to
    // overwrite the frame's output slot and didn't care about the label.
    //
    if (not Is_Stale_Void(&TG_Thrown_Label_Debug)) {
        if (arg_out != thrown)
            Move_Cell(thrown, &TG_Thrown_Label_Debug);
        Init_Stale_Void(&TG_Thrown_Label_Debug);
    }
  #endif
}

inline static bool Is_Throwing(REBFRM *f) {
    //
    // !!! An original constraint on asking if something was throwing was
    // that only the top frame could be asked about.  But Action_Executor()
    // is called to re-dispatch when there may be a frame above (kept there
    // by request from something like REDUCE).  We relax the constraint to
    // only be able to return *true* to a throw request if there are no
    // frames above on the stack.
    //
    if (not Is_Stale_Void(&TG_Thrown_Arg)) {
        /*assert(f == FS_TOP);*/  // forget even that check
        UNUSED(f);  // currently only used for debug build check
        return true;
    }
    return false;
}

#define THROWING Is_Throwing(frame_)
