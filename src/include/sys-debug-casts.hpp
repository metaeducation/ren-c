//
//  file: %sys-debug-casts.hpp
//  summary: "Instrumented operators for casting Node subclasses"
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
//         !!! DON'T BE (TOO) AFRAID OF THIS SCARY-LOOKING CODE !!!
//
// This file contains C++ template metaprogramming that delves into techniques
// like partial template specialization and SFINAE ("Substitution failure is
// not an error"):
//
//   https://en.cppreference.com/w/cpp/language/partial_specialization
//   https://en.cppreference.com/w/cpp/language/sfinae
//
// It's actually pretty tame and "easy"-to-grok when compared to a lot of C++
// boost or standard library code.  Though it's quite understandable that a C
// programmer would look at it and think it's completely bonkers.  (While
// dealing with pathological error messages designing this, I certainly had
// thoughts about tossing the whole thing rather than try to keep it working.)
//
// ...BUT bear in mind: *The interpreter is written as C, looks like C, and
// will always build fine without this.*  These casts are selectively compiled
// in when DEBUG_CHECK_CASTS is enabled, and provide powerful mechanisms for
// enforcing consistency at compile-time and runtime.
//
// The best way to think of this is as a kind of "third-party tool", sort of
// like Valgrind or Address Sanitizer.  While it's not "written in C", C++ is
// a superset of C.  So without really understanding the C++ bits, you can
// still inject arbitrary C code here to run whenever a `cast(type, value)`
// operation executes.  This means that if you have a datatype like Flex or
// Cell, you can do runtime validation of the bits in these types when
// `cast(Flex*, ptr)` or `cast(Cell*, ptr)` happen.  That's an extremely
// useful hook!
//
// Beyond that, you can even stop certain casts from happening at all at
// compile-time.  A good example would be casting to a mutable Symbol*, which
// should never be possible: Symbol is a String subclass, but all pointers
// to Symbol should be const.  (This was tried by making Symbol(const*) make
// a smart pointer class in DEBUG_CHECK_CASTS, which disabled Symbol(*)...but
// you'll have to take my word for it that this solution is much less
// convoluted and much more performant.)
//
// Explaining the C++ voodoo to a C programmer is beyond the scope of what can
// be accomplished in this file's comments.  *But you don't need to understand
// it to use it.*  If a debugging scenario would benefit from rigging in some
// code at the moment datatypes are cast, then just edit the bodies of the
// `CastHelper::convert<>` functions and ignore everything else.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. The main CastHelper template take two parameters: the "value's type (V)"
//    that is cast from, and the "type (T)" being cast to.  This file has
//    template partial specializations that only take one parameter: the `V`
//    type being cast from.  (e.g. we want to define an operator that does the
//    handling for casting to an Array*, that gets the arbitrary type being
//    cast from to inspect).  In order to not get in the way of smart pointer
//    classes, we narrow the specializations to V* raw pointer types (the
//    smart pointers can overload CastHelper and extract that raw pointer,
//    then delegate back to cast again)
//
// B. By default, if you upcast (e.g. casting from a derived class like Array
//    to a base class like Flex), we do this with a very-low-cost constexpr
//    that does the cast for free.  This is because every Array is-a Flex,
//    and if you have an Array* in your hand we can assume you got it through
//    a means that you knew it was valid.  But if you downcast (e.g. from a
//    Node* to a VarList*), then it's a riskier operation, so validation
//    code is run:
//
//      https://en.wikipedia.org/wiki/Downcasting
//
//    However, this doesn't have to be a rule.  If debugging a scenario and
//    you suspect that corruption is happening in upcast situations, then
//    just comment out the optimization and run the checks for all casts.
//
// C. If you find yourself having trouble with `static_assert(false, ...)`
//    triggering in SFINAE, see `static_assert(always_false<T>::value, ...)`
//


//=//// UPCAST AND DOWNCAST TAG DISPATCH //////////////////////////////////=//
//
// Pursuant to [A], we generally want to trust the type system when it comes
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
const Node* node_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Node* node_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const Node*> {
    typedef typename std::remove_const<V>::type V0;

    static const Node* convert(V* p) {
        return node_cast_impl(const_cast<V0*>(p), WhichCast<V0, Node>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Node*> {
    static Node* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Node*>(cast(const Node*, p));
    }
};


//=//// cast(Stub*, ...) //////////////////////////////////////////////////=//

template<typename V0>
const Stub* stub_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Stub* stub_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const Stub*> {
    typedef typename std::remove_const<V>::type V0;

    static const Stub* convert(V* p) {
        return flex_cast_impl(const_cast<V0*>(p), WhichCast<V0, Stub>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Stub*> {
    static Stub* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Stub*>(cast(const Stub*, p));
    }
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename V0>
const Flex* flex_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const Flex*>(p);
}

template<typename V0>
const Flex* flex_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const Flex*> {
    typedef typename std::remove_const<V>::type V0;

    static const Flex* convert(V* p) {
        return flex_cast_impl(const_cast<V0*>(p), WhichCast<V0, Flex>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Flex*> {
    static Flex* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Flex*>(cast(const Flex*, p));
    }
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename V0>
const Binary* binary_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const Binary*>(p);
}

template<typename V0>
const Binary* binary_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const Binary*> {
    typedef typename std::remove_const<V>::type V0;

    static const Binary* convert(V* p) {
        return binary_cast_impl(const_cast<V0*>(p), WhichCast<V0, Binary>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Binary*> {
    static Binary* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Binary*>(cast(const Binary*, p));
    }
};


//=//// cast(String*, ...) ////////////////////////////////////////////////=//

template<typename V0>
const String* string_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const String*>(p);
}

template<typename V0>
const String* string_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const String*> {
    typedef typename std::remove_const<V>::type V0;

    static const String* convert(V* p) {
        return string_cast_impl(const_cast<V0*>(p), WhichCast<V0, String>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,String*> {
    static String* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<String*>(cast(const String*, p));
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,const Symbol*> {
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
struct CastHelper<V*,Symbol*> {
    static Symbol* convert(V* p) = delete;
};
*/


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename V0>
const Array* array_cast_impl(V0* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<const Array*>(p);
}

template<typename V0>
const Array* array_cast_impl(V0* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, const Array*> {
    typedef typename std::remove_const<V>::type V0;

    static const Array* convert(V* p) {
        return array_cast_impl(const_cast<V0*>(p), WhichCast<V0, Array>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Array*> {
    static Array* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Array*>(cast(const Array*, p));
    }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename V>
VarList* varlist_cast_impl(V* p, UpcastTag) {  // trust types [B]
    return reinterpret_cast<VarList*>(p);
}

template<typename V>
VarList* varlist_cast_impl(V* p, DowncastTag) {  // validate bits [B]
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
struct CastHelper<V*, VarList*> {
    static VarList* convert(V* p) {
        return varlist_cast_impl(p, WhichCast<V, VarList>{});
    }
};

template<typename V>  // [A]
struct CastHelper<V*,const VarList*> {
    static const VarList* convert(V* p) = delete;  // no const Varlist*
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,Phase*> {
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
struct CastHelper<V*,const Phase*> {
    static const Phase* convert(V* p) = delete;  // no const Phase*
};


//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*,Level*> {
    static Level* convert(V* p) {
        static_assert(
            c_type_list<void,Byte,Node>::contains<V>()
                and not std::is_const<V>::value,
            "Invalid type for downcast to Level*"
        );

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<const Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_UNREADABLE
                | NODE_BYTEMASK_0x08_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x08_CELL
        )){
            crash (p);
        }

        return reinterpret_cast<Level*>(p);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,const Level*> {
    static const Level* convert(V* p) = delete;  // no const Level*
};
