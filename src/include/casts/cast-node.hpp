//
//  file: %cast-node.hpp
//  summary: "Instrumented operators for casting Node"
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
// See src/include/casts/README.md for general information about CastHelper.
//
// This file is specifically for checking casts to Nodes.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. CastHelper<> has two parameters (From and To types), but we pin down the
//    "To" type, then match a pattern for any "From" type (F).
//
// B. See the definition of CastHelper for why the generalized casting
//    mechanic runs through const pointers only.
//
// C. See the definitions of UpcastTag and DowncastTag for an explanation of
//    why we trust upcasts by default (you can override it if needed).
//


//=//// cast(Node*, ...) //////////////////////////////////////////////////=//

template<typename F>
const Node* node_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Node*, p);
}

template<typename F>
const Node* node_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if (not (*u_cast(const Byte*, p) & NODE_BYTEMASK_0x80_NODE))
        crash (p);

    return u_cast(const Node*, p);
};

template<typename F>  // [A]
struct CastHelper<const F*, const Node*> {  // both must be const [B]
    static const Node* convert(const F* p) {
        return node_cast_impl(p, WhichCastDirection<F, Node>{});
    }
};
