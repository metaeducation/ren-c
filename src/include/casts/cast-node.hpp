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
// A. See README.md's explanation of why we specialize one parameter and
//    leave the other free, so CastHelper fixes the type cast to while the
//    type being cast from is arbitrary and can be subsetted or reacted to.
//
// B. See README.md's explanation of why you need both a const T* and T*
//    case in your handling, due to general design nature of C++ and const.
//
// C. See README.md's explanation of why we trust the type system in upcast
//    situations by default (though you can edit this to override it).
//

//=//// UPCAST AND DOWNCAST TAG DISPATCH //////////////////////////////////=//
//
// Pursuant to [C], we generally want to trust the type system when it comes
// to upcasting, and be more skeptical of downcasts...verifying the bits.
//
// To make this easier to do, this factors out the logic for determining if
// something is an upcast or downcast into a tag type.  You can then write
// two functions taking a pointer and either an UpcastTag or DowncastTag,
// and use the `WhichCast<...>` to select which one to call.
//

template<typename V0, typename Base>
struct IsUpcastTo : std::integral_constant<
    bool,
    std::is_base_of<Base, V0>::value
> {};

struct UpcastTag {};
struct DowncastTag {};

template<typename V0, typename Base>
using WhichCast = typename std::conditional<  // tag selector
    IsUpcastTo<V0, Base>::value,
    UpcastTag,
    DowncastTag
>::type;  // if you see WhichCast<...>{} that instantiates the selected tag


//=//// cast(Node*, ...) //////////////////////////////////////////////////=//

template<typename V0>
const Node* node_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Node* node_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void, Byte>::contains<V0>(),
        "Invalid type for downcast to Node*"
    );

    if (not p)
        return nullptr;

    if (not (*reinterpret_cast<const Byte*>(p) & NODE_BYTEMASK_0x80_NODE))
        crash (p);

    return reinterpret_cast<const Node*>(p);
};

template<typename V>  // [A]
struct CastHelper<V*, const Node*> {  // const Node* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Node* convert(V* p) {
        return node_cast_impl(const_cast<V0*>(p), WhichCast<V0, Node>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Node*> {  // Node* case [B]
    static Node* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Node*>(cast(const Node*, p));
    }
};
