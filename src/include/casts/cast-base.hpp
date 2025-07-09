//
//  file: %cast-base.hpp
//  summary: "Instrumented operators for casting to Base"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// See src/include/casts/README.md for general information about CastHook.
//
// This file is specifically for checking casts to Base.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. CastHook<> has two parameters (From and To types), but we pin down the
//    "To" type, then match a pattern for any "From" type (F).
//
// B. See the definition of CastHook for why the generalized casting
//    mechanic runs through const pointers only.
//
// C. See the definitions of UpcastTag and DowncastTag for an explanation of
//    why we trust upcasts by default (you can override it if needed).
//


//=//// cast(Base*, ...) //////////////////////////////////////////////////=//

// 1. RebolContext and RebolInstruction are synonyms for Base, so you see
//    cast(RebolContext*, ...) of Context* pointers or subclasses in the API
//    when it's trying to export those pointers.  Review if this can be
//    done more elegantly--but remember the goal is to be as correct as
//    possible while not having the API return void pointers (because we
//    don't want the variadic APIs to be un-type-checked and take any old
//    pointer...since the C++ variadic API can enforce typechecking due
//    to recursively packing the arguments).

template<typename F>  // [A]
struct CastHook<const F*, const Base*> {  // both must be const [B]
  static void Validate_Bits(const F* p) {
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte,
        Stub, ParamList, Context, SeaOfVars,
        Element, Value
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return;

    if (not (*u_cast(const Byte*, p) & BASE_BYTEMASK_0x80_NODE))
        crash (p);
  }
};
