//
//  File: %sys-pick.h
//  Summary: "Definitions for Processing Sequence Picking/Poking"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Pathing was not designed well in R3-Alpha, and Ren-C has been trying to
// evolve the model into something more coherent:
//
// https://forum.rebol.info/t/the-pathing-and-picking-predicament-pans-out/1704
//


#define PVS_PICKER(pvs) \
    pvs->u.path.picker


// 1. Generally speaking, generics (and most functions in the system) do
//    not work on antiforms, quasiforms, or quoted datatypes.
//
//    For one thing, this would introduce uncomfortable questions, like:
//    should the NEXT of ''[a b c] be [b c] or ''[b c] ?  This would take the
//    already staggering combinatorics of the system up a notch by forcing
//    "quote propagation" policies to be injected everywhere.
//
//    Yet there's another danger: if quoted/quasi items wind up giving an
//    answer instead of an error for lots of functions, this will lead to
//    carelessness in propagation of the marks...not stripping them off when
//    they aren't needed.  This would lead to an undisciplined hodgepodge of
//    marks that are effectively meaningless.  In addition to being ugly, that
//    limits the potential for using the marks intentionally in a dialect
//    later, if you're beholden to treating leaky quotes and quasis as if
//    they were not there.
//
INLINE Bounce Run_Generic_Dispatch(
    const Element* cue,
    Level* L,
    const Symbol* verb
){
    Heart heart = Cell_Heart_Ensure_Noquote(cue);  // no quoted/quasi/anti [1]

    GenericHook* hook = Generic_Hook_For_Heart(heart);
    return hook(L, verb);
}


// For efficiency, native PICK-POKE* implementations reuse the level (this is
// somewhat comparable to R3-Alpha's "PVS" struct, reused for all the path
// dispatches...but with the added protections levels have with the GC).
//
// For pokes, the poke location of the value that is doing the chaining to
// another pickpoke needs to be preserved...because the bits in the container
// may need to be updated for some immediate types, as their storage is
// actually in the container.
//
INLINE Bounce Run_Pickpoke_Dispatch(
    Level* level_,
    const Symbol* verb,
    const Value* new_location
){
    Copy_Cell(PUSH(), ARG_N(1));
    Copy_Cell(ARG_N(1), new_location);
    Bounce r = Run_Generic_Dispatch(cast(Element*, ARG_N(1)), level_, verb);
    Move_Drop_Top_Stack_Value(ARG_N(1));
    return r;
}

// If you pass in a nullptr for the steps in the Get_Var() and Set_Var()
// mechanics, they will disallow groups.  This is a safety measure which helps
// avoid unwanted side effects in SET and GET, and motivates passing in a
// variable that will be assigned a "hardened" path of steps to get to the
// location more repeatedly (e.g. if something like default wanted to make
// sure it updates the same variable it checked to see if it had a value...
// and only run code in groups once.)
//
// Requesting steps will supress that, but sometimes you don't actually need
// the steps (as the evaluator doesn't when doing SET-TUPLE!).  Rather than
// passing a separate flag, the NOTHING_VALUE pointer is used (mutable, but it
// has the protected bit set to avoid accidents)
//
#define GROUPS_OK &PG_Nothing_Value
