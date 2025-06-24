//
//  file: %sys-generic.h
//  summary: "Definitions for Generic Function Dispatch"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2015 Ren-C Open Source Contributors
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
// Ren-C has a new concept of generic dispatch using sparse tables which are
// scanned during the build process to find IMPLEMENT_GENERIC(name, typeset)
// instances.
//
//   https://forum.rebol.info/c/development/optimization/53
//

// `name` is taken in all-caps so we can get a SYM_XXX from token pasting.
//
#define Dispatch_Generic(name,cue,L) \
    Dispatch_Generic_Core( \
        SYM_##name, &g_generic_##name, Datatype_Of_Fundamental(cue), (L) \
    )

#define Try_Dispatch_Generic(bounce,name,cue,L) \
    Try_Dispatch_Generic_Core( \
        bounce, SYM_##name, &g_generic_##name, \
        Datatype_Of_Fundamental(cue), (L) \
    )

// Generic Dispatch if you just want it to fail if there's no handler.
// (Some clients use Try_Dispatch_Generic_Core(), so they can take an
// alternative action if no handler is registered... e.g. REVERSE-OF will
// fall back on COPY and REVERSE.)
//
// 1. return PANIC() can't be used in %sys-core.h because not everything that
//    includes %sys-core.h defines the helper macros.  We want this to be
//    fast and get inlined, so expand the macro manually.
//
INLINE Bounce Dispatch_Generic_Core(
    SymId symid,
    GenericTable* table,
    const Value* datatype,  // no quoted/quasi/anti [1]
    Level* level_
){
    Bounce bounce;
    if (Try_Dispatch_Generic_Core(
        &bounce,
        symid,
        table,
        datatype,
        level_
    )){
        return bounce;
    }

    DECLARE_ELEMENT (name);
    Init_Word(name, Canon_Symbol(symid));

    return Native_Panic_Result(  // can't use FAIL() macro in %sys-core.h [1]
        level_, Derive_Error_From_Pointer(
            Error_Cannot_Use_Raw(name, datatype)
        )
    );
}


INLINE Option(Dispatcher*) Get_Builtin_Generic_Dispatcher(
    const GenericTable* table,
    Option(Heart) heart
){
    const GenericInfo* info = table->info;
    for (; info->typeset_byte != 0; ++info) {
        if (Builtin_Typeset_Check(info->typeset_byte, heart))
            return info->dispatcher;
    }
    return nullptr;
}

#define Handles_Builtin_Generic(name,heart) \
    (did Get_Builtin_Generic_Dispatcher(&g_generic_##name, heart))


INLINE Option(Dispatcher*) Get_Generic_Dispatcher(
    const GenericTable* table,
    const Value* datatype
){
    Option(Heart) heart = Cell_Datatype_Builtin_Heart(datatype);
    if (not heart)
        panic ("Generic dispatch not supported for extension types yet");

    return Get_Builtin_Generic_Dispatcher(table, unwrap heart);
}

#define Handles_Generic(name,datatype) \
    (did Get_Generic_Dispatcher(&g_generic_##name, datatype))


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
// passing a separate flag, the g_tripwire pointer is used (mutable, but it
// has the protected bit set to avoid accidents)
//
#define GROUPS_OK  cast(Option(Element*), m_cast(Element*, g_empty_text))
#define NO_STEPS  cast(Option(Element*), nullptr)


#define DUAL_LIFTED(v)    Liftify(v ? v : Init_Nulled(OUT))
#define DUAL_SIGNAL_NULL_ABSENT  cast(Bounce, Init_Nulled(OUT))
#define Is_Dual_Nulled_Absent_Signal(dual)  Is_Nulled(dual)

#define WRITEBACK(out)  DUAL_LIFTED(out)  // commentary
#define NO_WRITEBACK_NEEDED  DUAL_SIGNAL_NULL_ABSENT
#define Is_Dual_Nulled_No_Writeback_Signal(dual)  Is_Nulled(dual)

#define Is_Dual_Nulled_Pick_Signal(dual)  Is_Nulled(dual)
#define Init_Dual_Nulled_Pick_Signal(dual)  Init_Nulled(dual)

#define Is_Dual_Word_Remove_Signal(dual)  Is_Word_With_Id((dual), SYM_REMOVE)
#define Init_Dual_Word_Remove_Signal(dual)  Init_Word((dual), CANON(REMOVE))

#define Is_Dual_Tripwire_Unset_Signal(dual)  Is_Tripwire(dual)
#define Init_Dual_Tripwire_Unset_Signal(dual)  Init_Tripwire(dual)

#define Is_Dual_Word_Named_Signal(dual)  Is_Word(dual)


// Show that we know we're dealing with a lifted dual slot.
//
INLINE bool Any_Lifted_Dual(const Slot* slot) {
    assert(Get_Cell_Flag(slot, SLOT_WEIRD_DUAL));
    return LIFT_BYTE_RAW(slot) >= QUASIFORM_2;
}

INLINE Slot* Init_Dual_Unset(Init(Slot) slot) {
    Init_Tripwire(slot);
    Set_Cell_Flag(slot, SLOT_WEIRD_DUAL);  // special case
    return slot;
}

INLINE bool Is_Dual_Unset(Cell* cell) {
    if (Not_Cell_Flag(cell, SLOT_WEIRD_DUAL))
        return false;
    return Is_Tripwire(u_cast(Value*, cell));
}
