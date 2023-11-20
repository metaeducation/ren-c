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
//   value can store either a Level* which costs nothing extra, or Context*
//   which requires "reifying" the frame and making it GC-visible.  Reifying
//   would happen unconditionally if the level is put into a global variable,
//   but so long as the FRAME! value bubbles up no higher than the Level*
//   it points to, it can be used as-is.  With RETURN, it will be exactly the
//   right lifetime--since the originating level is right where it stops.
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

inline static Value(const*) VAL_THROWN_LABEL(Level* level_) {
    UNUSED(level_);
    assert(not Is_Cell_Erased(&g_ts.thrown_label));
    return &g_ts.thrown_label;
}

inline static Bounce Init_Thrown_With_Label(  // assumes `arg` in g_ts.thrown_arg
    Level* level_,
    Atom(const*) arg,
    const REBVAL *label  // Note: is allowed to be same as `out`
){
    assert(not THROWING);

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    Copy_Cell(&g_ts.thrown_arg, arg);

    assert(Is_Cell_Erased(&g_ts.thrown_label));
    Copy_Cell(&g_ts.thrown_label, label);
    Deactivate_If_Activation(&g_ts.thrown_label);

    assert(THROWING);

    FRESHEN(level_->out);
    return BOUNCE_THROWN;
}

// When failures are put in the throw state, they are the label--not the value.
//
inline static Bounce Init_Thrown_Failure(Level* L, Value(const*) error) {
    assert(IS_ERROR(error));
    return Init_Thrown_With_Label(L, Lib(NULL), error);
}

inline static void CATCH_THROWN(
    Cell* arg_out,
    Level* level_
){
    UNUSED(level_);

    assert(THROWING);

    Copy_Cell(arg_out, &g_ts.thrown_arg);

    Erase_Cell(&g_ts.thrown_arg);
    Erase_Cell(&g_ts.thrown_label);

    assert(not THROWING);

    g_ts.unwind_level = nullptr;
}


inline static void Drop_Level(Level* L);

// When you're sure that the value isn't going to be consumed by a multireturn
// then use this to get the first value unmeta'd
//
inline static Value(*) Decay_If_Unstable(Atom(*) v) {
    if (not Is_Isotope(v))
        return cast(Value(*), v);

    if (Is_Lazy(v)) {
        if (not Pushed_Decaying_Level(v, v, LEVEL_MASK_NONE))
            return cast(Value(*), v);  // cheap reification
        if (Trampoline_With_Top_As_Root_Throws())
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Drop_Level(TOP_LEVEL);

        // fall through in case result is pack or raised
        // (should this iterate?)

        assert(not Is_Lazy(v));
    }

    if (Is_Pack(v)) {  // iterate until result is not multi-return, see [1]
        const Cell* pack_meta_tail;
        const Cell* pack_meta_at = VAL_ARRAY_AT(&pack_meta_tail, v);
        if (pack_meta_at == pack_meta_tail)
            fail (Error_No_Value_Raw());  // treat as void?
        Derelativize(v, pack_meta_at, VAL_SPECIFIER(v));
        Meta_Unquotify_Undecayed(v);
        if (Is_Pack(v) or Is_Lazy(v))
            fail (Error_Bad_Isotope(v));  // need more granular unpacking
        if (Is_Raised(v))
            fail (VAL_CONTEXT(v));
        assert(not Is_Isotope(v) or Is_Isotope_Stable(v));
        return cast(Value(*), v);
    }

    if (Is_Barrier(v))
        fail (Error_No_Value_Raw());  // distinct error from nihil?

    if (Is_Raised(v))  // !!! should this raise an error here?
        fail (VAL_CONTEXT(v));

    return cast(Value(*), v);
}

// Packs with unstable isotopes in their first cell (or nihil) are not able
// to be decayed.  Type checking has to be aware of this, and know that such
// packs shouldn't raise errors.
//
inline static bool Is_Pack_Undecayable(Atom(*) pack)
{
    assert(Is_Pack(pack));
    if (Is_Nihil(pack))
        return true;
    const Cell* at = VAL_ARRAY_AT(nullptr, pack);
    if (Is_Meta_Of_Raised(at))
        return true;
    if (Is_Meta_Of_Pack(at))
        return true;
    if (Is_Meta_Of_Lazy(at))
        return true;
    return false;
}
