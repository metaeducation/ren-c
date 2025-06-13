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
// D. If you find yourself having trouble with `static_assert(false, ...)`
//    triggering in SFINAE, see `static_assert(always_false<T>::value, ...)`
//

//=//// cast(Stub*, ...) //////////////////////////////////////////////////=//

template<typename V0>
const Stub* stub_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Stub* stub_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void, Byte, Node>::contains<V0>(),
        "Invalid type for downcast to Stub*"
    );

    if (not p)
        return nullptr;

    if ((reinterpret_cast<const Stub*>(p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_CELL  // NODE_FLAG_UNREADABLE ok
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }

    return reinterpret_cast<const Stub*>(p);
}

template<typename V>  // [A]
struct CastHelper<V*, const Stub*> {  // const Stub* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Stub* convert(V* p) {
        return flex_cast_impl(const_cast<V0*>(p), WhichCast<V0, Stub>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Stub*> {  // Stub* case [B]
    static Stub* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Stub*>(cast(const Stub*, p));
    }
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename V0>
const Flex* flex_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Flex* flex_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void,Byte,Node,Stub>::contains<V0>(),
        "Invalid type for downcast to Flex*"
    );
    if (not p)
        return nullptr;
    if ((reinterpret_cast<const Stub*>(p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }
    return reinterpret_cast<const Flex*>(p);
}

template<typename V>  // [A]
struct CastHelper<V*, const Flex*> {  // const Flex* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Flex* convert(V* p) {
        return flex_cast_impl(const_cast<V0*>(p), WhichCast<V0, Flex>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Flex*> {  // Flex* case [B]
    static Flex* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Flex*>(cast(const Flex*, p));
    }
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename V0>
const Binary* binary_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const Binary*>(p);
}

template<typename V0>
const Binary* binary_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void,Byte,Node,Flex>::contains<V0>(),
        "Invalid type for downcast to Binary*"
    );

    if (not p)
        return nullptr;

    if ((reinterpret_cast<const Stub*>(p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE  // NODE_FLAG_UNREADABLE is diminished Stub
    )){
        crash (p);
    }

    // assert Flex width here (trouble with Flex_Wide() from within
    // cast at the moment)

    return reinterpret_cast<const Binary*>(p);
};

template<typename V>  // [A]
struct CastHelper<V*, const Binary*> {  // const Binary* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Binary* convert(V* p) {
        return binary_cast_impl(const_cast<V0*>(p), WhichCast<V0, Binary>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Binary*> {  // Binary* case [B]
    static Binary* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Binary*>(cast(const Binary*, p));
    }
};


//=//// cast(String*, ...) ////////////////////////////////////////////////=//

template<typename V0>
const String* string_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const String*>(p);
}

template<typename V0>
const String* string_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void,Byte,Node,Stub,Flex,Binary>::contains<V0>(),
        "Invalid type for downcast to String*"
    );

    if (not p)
        return nullptr;

    const Stub* stub = reinterpret_cast<const Stub*>(p);

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

    return reinterpret_cast<const String*>(p);
};

template<typename V>  // [A]
struct CastHelper<V*, const String*> {  // const String* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const String* convert(V* p) {
        return string_cast_impl(const_cast<V0*>(p), WhichCast<V0, String>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,String*> {  // String* case [B]
    static String* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<String*>(cast(const String*, p));
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,const Symbol*> {  // const Symbol* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Symbol* convert(V* p) {
        static_assert(
            c_type_list<void,Byte,Node,Stub,Flex,Binary,String>::contains<V0>(),
            "Invalid type for downcast to Symbol*"
        );

        if (not p)
            return nullptr;

        const Stub* stub = reinterpret_cast<const Stub*>(p);
        if ((stub->leader.bits & (
            (FLEX_MASK_SYMBOL | FLAG_TASTE_BYTE(255))
                | NODE_FLAG_UNREADABLE
                | NODE_FLAG_CELL
        )) !=
            FLEX_MASK_SYMBOL
        ){
            crash (p);
        }

        return reinterpret_cast<const Symbol*>(p);
    }
};

// If we didn't supply a cast in the const case, it would be unchecked.  The
// only time Symbols should be mutable is at creation time, or when bits are
// being tweaked in binding slots.  Stored or external pointers should always
// be const if downcasting.

/*
template<typename V>
struct CastHelper<V*,Symbol*> {  // Symbol* case [B]
    static Symbol* convert(V* p) = delete;
};
*/


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename V0>
const Array* array_cast_impl(V0* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<const Array*>(p);
}

template<typename V0>
const Array* array_cast_impl(V0* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void,Byte,Stub,Node,Flex>::contains<V0>(),
        "Invalid type for downcast to Array*"
    );

    if (not p)
        return nullptr;

    if ((reinterpret_cast<const Stub*>(p)->leader.bits & (
        NODE_FLAG_NODE | NODE_FLAG_UNREADABLE | NODE_FLAG_CELL
    )) != (
        NODE_FLAG_NODE
    )){
        crash (p);
    }

    return reinterpret_cast<const Array*>(p);
};

template<typename V>  // [A]
struct CastHelper<V*, const Array*> {  // const Array* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Array* convert(V* p) {
        return array_cast_impl(const_cast<V0*>(p), WhichCast<V0, Array>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Array*> {  // Array* case [B]
    static Array* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Array*>(cast(const Array*, p));
    }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename V>
VarList* varlist_cast_impl(V* p, UpcastTag) {  // trust types [C]
    return reinterpret_cast<VarList*>(p);
}

template<typename V>
VarList* varlist_cast_impl(V* p, DowncastTag) {  // validate bits [C]
    static_assert(
        c_type_list<void,Byte,Node,Stub,Flex,Array>::contains<V>(),
        "Invalid type for downcast to VarList*"
    );

    if (not p)
        return nullptr;

    if ((reinterpret_cast<Stub*>(p)->leader.bits & (
        FLEX_MASK_LEVEL_VARLIST  // MISC_NODE_NEEDS_MARK
            | NODE_FLAG_UNREADABLE
            | NODE_FLAG_CELL
            | FLAG_TASTE_BYTE(255)
    )) !=
        FLEX_MASK_LEVEL_VARLIST
    ){
        crash (p);
    }

    return reinterpret_cast<VarList*>(p);
};

template<typename V>  // [A]
struct CastHelper<V*, VarList*> {  // VarList* case [B]
    static VarList* convert(V* p) {
        return varlist_cast_impl(p, WhichCast<V, VarList>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,const VarList*> {  // const VarList* case [B]
    static const VarList* convert(V* p) = delete;  // no const Varlist*
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,Phase*> {  // Phase* case [B]
    static Phase* convert(V* p) {
        static_assert(
            c_type_list<void,Byte,Node,Stub,Flex,Array>::contains<V>()
                and not std::is_const<V>::value,
            "Invalid type for downcast to Phase*"
        );

        if (not p)
            return nullptr;

        const Stub* stub = reinterpret_cast<Stub*>(p);

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

        return reinterpret_cast<Phase*>(p);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,const Phase*> {  // const Phase* case [B]
    static const Phase* convert(V* p) = delete;  // no const Phase*
};
