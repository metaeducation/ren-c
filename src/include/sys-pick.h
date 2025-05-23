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
// passing a separate flag, the g_trash pointer is used (mutable, but it
// has the protected bit set to avoid accidents)
//
#define GROUPS_OK  cast(Option(Element*), x_cast(Element*, g_empty_text))
#define NO_STEPS  cast(Option(Element*), nullptr)


// This is a helper for working with the "Dual" convention, which multiplexes
// regular values as a lifted state, on top of stable non-quoted non-quasi
// value states... to be able to use one value slot to communicate both values
// and signals.
//
// (While this could be done with a refinement when passing values *in* to a
// function, it wouldn't work for giving them back *out*.  Also, it's more
// efficient than a refinement because it uses one Cell instead of two.)
//
// The helper adjusts the Cell so that it holds the non-dual state, moving
// the dual state onto a boolean bit.  The adjustment remembers if it was
// done, so that Dual_ARG() can be called multiple times e.g. through
// successive continuations and not mutate the cell multiple times.
//
INLINE Option(const Value*) Dual_Level_Arg(
    bool* signal,  // may be nullptr (be cheap, don't use Option(Sink(bool))))
    Level* L,
    REBLEN n
){
    Value* arg = Level_Arg(L, n);

    if (Get_Cell_Flag(arg, PROTECTED)) {
        *signal = Is_Node_Marked(arg);
        if (signal)
            assert(not Any_Lifted(arg));  // signals couldn't be quoted/quasi
        if (Is_Nulled(arg))
            return nullptr;
        return arg;
    }

    assert(not Is_Node_Marked(arg));  // use mark for saying was dual

    Option(const Value*) result;
    if (Any_Lifted(arg)) {
        if (signal != nullptr)
            *signal = false;  // regular values are lifted
        Unliftify_Known_Stable(arg);  // duals can't be unstable ATM
        result = arg;
    }
    else {
        if (signal == nullptr)
            panic (Error_Bad_Poke_Dual_Raw(arg));
        *signal = true;
        Set_Node_Marked_Bit(arg);
        result = nullptr;
    }
    Set_Cell_Flag(arg, PROTECTED);  // helps stop double-unlift
    return result;
}

#define Dual_ARG(signal,name) \
    Dual_Level_Arg((signal), level_, param_##name##_)

#define Non_Dual_ARG(name) \
    Dual_Level_Arg(nullptr, level_, param_##name##_)


#define NO_WRITEBACK_NEEDED  DUAL_SIGNAL_NULL

#define WRITEBACK(out)  DUAL_LIFTED(out)  // commentary
