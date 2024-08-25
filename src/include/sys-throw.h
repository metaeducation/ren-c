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
// All THROWN values have two parts: the Atom arg being thrown and
// a Value indicating the /NAME of a labeled throw.  (If the throw was
// created with plain THROW instead of THROW/NAME then its name is BLANK!).
//
// You cannot fit both values into a single value's bits of course.  One way
// to approach the problem would be to create a new REB_THROWN type with
// two fields (like a PAIR!).  But since there can only be one thrown value
// on an evaluator thread at a time, trampoline-globals are used instead.
//
// ALL calls into the evaluator to generate values must check for the thrown
// flag.  This is helped by naming conventions, e.g. `XXX_Throws()` to remind
// callers they have to handle it, pass it up the stack, or raise an uncaught
// throw exception.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. When an abrupt failure occurs, it is intercepted by the trampoline and
//    converted into a throw state with an ERROR! as the label.  This state
//    is bubbled up the stack much like a throw, however it cannot be
//    intercepted by CATCH or definitional-error handlers like TRY.  Only
//    special routines like SYS.UTIL.RESCUE can catch abrupt failures, as
//    what they mean is too nebulous for arbitrary stacks to assume they
//    know how to handle them.
//

INLINE const Value* VAL_THROWN_LABEL(Level* level_) {
    UNUSED(level_);
    assert(not Is_Cell_Erased(&g_ts.thrown_label));
    return &g_ts.thrown_label;
}

#define Is_Throwing_Failure(level_) \
    Is_Error(VAL_THROWN_LABEL(level_))  // non-definitional errors [1]

INLINE Bounce Init_Thrown_With_Label(  // assumes `arg` in g_ts.thrown_arg
    Level* level_,
    const Atom* arg,
    const Value* label  // Note: is allowed to be same as `out`
){
    assert(not THROWING);

    assert(Is_Cell_Erased(&g_ts.thrown_arg));
    Copy_Cell(&g_ts.thrown_arg, arg);

    assert(Is_Cell_Erased(&g_ts.thrown_label));
    Copy_Cell(&g_ts.thrown_label, label);
    Deactivate_If_Action(&g_ts.thrown_label);

    assert(THROWING);

    Freshen_Cell(level_->out);
    return BOUNCE_THROWN;
}

// When failures are put in the throw state, they are the label--not the value.
//
INLINE Bounce Init_Thrown_Failure(Level* L, const Value* error) {
    assert(Is_Error(error));
    return Init_Thrown_With_Label(L, Lib(NULL), error);
}

INLINE void CATCH_THROWN(
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


INLINE void Drop_Level(Level* L);

// When you're sure that the value isn't going to be consumed by a multireturn
// then use this to get the first value unmeta'd
//
INLINE Value* Decay_If_Unstable(Need(Atom*) v) {
    if (not Is_Antiform(v))
        return u_cast(Value*, u_cast(Atom*, v));

    if (Is_Lazy(v)) {
        if (not Pushed_Decaying_Level(v, v, LEVEL_MASK_NONE))
            return u_cast(Value*, u_cast(Atom*, v));  // cheap reification
        if (Trampoline_With_Top_As_Root_Throws())
            fail (Error_No_Catch_For_Throw(TOP_LEVEL));
        Drop_Level(TOP_LEVEL);

        // fall through in case result is pack or raised
        // (should this iterate?)

        assert(not Is_Lazy(v));
    }

    if (Is_Pack(v)) {  // iterate until result is not multi-return [1]
        const Element* pack_meta_tail;
        const Element* pack_meta_at = Cell_List_At(&pack_meta_tail, v);
        if (pack_meta_at == pack_meta_tail)
            fail (Error_No_Value_Raw());  // treat as void?
        Derelativize(v, pack_meta_at, Cell_Specifier(v));
        Meta_Unquotify_Undecayed(v);
        if (Is_Pack(v) or Is_Lazy(v))
            fail (Error_Bad_Antiform(v));  // need more granular unpacking
        if (Is_Raised(v))
            fail (VAL_CONTEXT(v));
        assert(not Is_Antiform(v) or Is_Antiform_Stable(v));
        return u_cast(Value*, u_cast(Atom*, v));
    }

    if (Is_Barrier(v))
        fail (Error_No_Value_Raw());  // distinct error from nihil?

    if (Is_Raised(v))  // !!! should this raise an error here?
        fail (VAL_CONTEXT(v));

    return u_cast(Value*, u_cast(Atom*, v));
}

// Packs with unstable isotopes in their first cell are not able to be decayed.
// Type checking has to be aware of this, and know that such packs shouldn't
// raise errors.
//
INLINE bool Is_Pack_Undecayable(Atom* pack)
{
    assert(Is_Pack(pack));
    if (Is_Nihil(pack))
        return true;
    const Element* at = Cell_List_At(nullptr, pack);
    if (Is_Meta_Of_Raised(at))
        return true;
    if (Is_Meta_Of_Pack(at))
        return true;
    if (Is_Meta_Of_Lazy(at))
        return true;
    return false;
}
