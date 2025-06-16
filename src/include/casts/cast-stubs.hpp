//
//  file: %cast-stubs.hpp
//  summary: "Instrumented operators for casting Stub subclasses"
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
// This file is specifically for checking casts to Stub-derived types.
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
// D. If you find yourself having trouble with `static_assert(false, ...)`
//    triggering in SFINAE, see `static_assert(always_false<T>::value, ...)`
//

//=//// cast(Stub*, ...) //////////////////////////////////////////////////=//

template<typename F>
const Stub* stub_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Flex*, p);
}

template<typename F>
const Stub* stub_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // NODE_FLAG_UNREADABLE ok
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }

    return u_cast(const Stub*, p);
}

template<typename F>  // [A]
struct CastHelper<const F*, const Stub*> {  // both must be const [B]
    static const Stub* convert(const F* p) {
        return stub_cast_impl(p, WhichCastDirection<F, Stub>{});
    }
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename F>
const Flex* flex_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Flex*, p);
}

template<typename F>
const Flex* flex_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node, Stub
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }

    return u_cast(const Flex*, p);
}

template<typename F>  // [A]
struct CastHelper<const F*, const Flex*> {  // both must be const [B]
    static const Flex* convert(const F* p) {
        return flex_cast_impl(p, WhichCastDirection<F, Flex>{});
    }
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename F>
const Binary* binary_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Binary*, p);
}

template<typename F>
const Binary* binary_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    const Stub* stub = u_cast(const Stub*, p);

    if ((stub->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE  // NODE_FLAG_UNREADABLE is diminished Stub
    )){
        crash (p);
    }

    impossible(Flex_Wide(stub) != 1);  // we *could* check this here

    return u_cast(const Binary*, p);
};

template<typename F>  // [A]
struct CastHelper<const F*, const Binary*> {  // both must be const [B]
    static const Binary* convert(const F* p) {
        return binary_cast_impl(p, WhichCastDirection<F, Binary>{});
    }
};


//=//// cast(String*, ...) ////////////////////////////////////////////////=//

template<typename F>
const String* string_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const String*, p);
}

template<typename F>
const String* string_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node, Stub, Flex, Binary
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    const Stub* stub = u_cast(const Stub*, p);

    Byte taste = TASTE_BYTE(stub);
    if (taste != FLAVOR_NONSYMBOL and taste != FLAVOR_SYMBOL)
        crash (p);

    if ((stub->leader.bits & (
        FLEX_MASK_SYMBOL_STRING_COMMON
            | NODE_FLAG_UNREADABLE
            | NODE_FLAG_CELL
    )) !=
        FLEX_MASK_SYMBOL_STRING_COMMON
    ){
        assert(stub->leader.bits & STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY);
        crash (p);
    }

    impossible(Flex_Wide(stub) != 1);  // we *could* check this here

    return u_cast(const String*, p);
};

template<typename F>  // [A]
struct CastHelper<const F*, const String*> {  // both must be const [B]
    static const String* convert(const F* p) {
        return string_cast_impl(p, WhichCastDirection<F, String>{});
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHelper<const F*, const Symbol*> {
    static const Symbol* convert(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Node, Stub, Flex, Binary, String
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return nullptr;

        const Stub* stub = u_cast(const Stub*, p);
        if ((stub->leader.bits & (
            (FLEX_MASK_SYMBOL | FLAG_TASTE_BYTE(255))
                | NODE_FLAG_UNREADABLE
                | NODE_FLAG_CELL
        )) !=
            FLEX_MASK_SYMBOL
        ){
            crash (p);
        }

        impossible(Flex_Wide(stub) != 1);  // we *could* check this here

        return u_cast(const Symbol*, p);
    }
};


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename F>
const Array* array_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const Array*, p);
}

template<typename F>
const Array* array_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node, Stub, Flex
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }

    return u_cast(const Array*, p);
};

template<typename F>  // [A]
struct CastHelper<const F*, const Array*> {  // both must be const [B]
    static const Array* convert(const F* p) {
        return array_cast_impl(p, WhichCastDirection<F, Array>{});
    }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename F>
const VarList* varlist_cast_impl(const F* p, UpcastTag) {  // trust upcast [C]
    return u_cast(const VarList*, p);
}

template<typename F>
const VarList* varlist_cast_impl(const F* p, DowncastTag) {  // validate [C]
    DECLARE_C_TYPE_LIST(type_list,
        void, Byte, Node, Stub, Flex, Array
    );
    STATIC_ASSERT(In_C_Type_List(type_list, F));

    if (not p)
        return nullptr;

    if ((u_cast(const Stub*, p)->leader.bits & (
        FLEX_MASK_LEVEL_VARLIST  // MISC_NODE_NEEDS_MARK
            | NODE_FLAG_UNREADABLE
            | NODE_FLAG_CELL
            | FLAG_TASTE_BYTE(255)
    )) !=
        FLEX_MASK_LEVEL_VARLIST
    ){
        crash (p);
    }

    return u_cast(const VarList*, p);
};

template<typename F>  // [A]
struct CastHelper<const F*, const VarList*> {  // both must be const [B]
    static const VarList* convert(const F* p) {
        return varlist_cast_impl(p, WhichCastDirection<F, VarList>{});
    }
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename F>  // [A]
struct CastHelper<const F*, const Phase*> {  // both must be const [B]
    static const Phase* convert(const F* p) {
        DECLARE_C_TYPE_LIST(type_list,
            void, Byte, Node, Stub, Flex, Array
        );
        STATIC_ASSERT(In_C_Type_List(type_list, F));

        if (not p)
            return nullptr;

        const Stub* stub = u_cast(const Stub*, p);

        if (TASTE_BYTE(stub) == FLAVOR_DETAILS) {
            if ((stub->leader.bits & (
                (FLEX_MASK_DETAILS | FLAG_TASTE_BYTE(255))
                    | NODE_FLAG_UNREADABLE
                    | NODE_FLAG_CELL
            )) !=
                FLEX_MASK_DETAILS
            ){
                crash (p);
            }
        }
        else {
            if ((stub->leader.bits & ((
                (FLEX_MASK_LEVEL_VARLIST | FLAG_TASTE_BYTE(255))
                    | NODE_FLAG_UNREADABLE
                    | NODE_FLAG_CELL
                )
            )) !=
                FLEX_MASK_LEVEL_VARLIST  // maybe no MISC_NODE_NEEDS_MARK
            ){
                crash (p);
            }
        }

        return u_cast(const Phase*, p);
    }
};
