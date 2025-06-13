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
// https://en.cppreference.com/w/cpp/language/partial_specialization
// https://en.cppreference.com/w/cpp/language/sfinae
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
// 1. The main CastHelper template take two parameters: the "value's type (V)"
//    that is cast from, and the "type (T)" being cast to.  This file has
//    template partial specializations that only take one parameter: the `V`
//    type being cast from.  (e.g. we want to define an operator that does the
//    handling for casting to an Array*, that gets the arbitrary type being
//    cast from to inspect).  In order to not get in the way of smart pointer
//    classes, we narrow the specializations to V* raw pointer types (the
//    smart pointers can overload CastHelper and extract that raw pointer,
//    then delegate back to cast again)
//
// 3. We want to be able to pick different convert() implementations to use
//    based on properties of the type we are casting from.  So there can be
//    multiple convert() member functions.  Picking which operation is done
//    by enabling the appropriate function using enable_if<> and SFINAE.  But
//    to get SFINAE on member functions they have to themselves be templates:
//
//      https://stackoverflow.com/questions/11531989/
//
//    So at minimum you need a dummy template variable (V_) and a dummy
//    check of it to cause the enable_if to work correctly.  <shrug>
//
// 4. By default, if you upcast (e.g. casting from a derived class like Array
//    to a base class like Flex), we do this with a zero-cost constexpr
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
// 5. One of the quirks of partial template specialization is that it does
//    not match reference types.  So Node* and Node*& need to have different
//    partial specializations:
//
//      https://stackoverflow.com/q/27843127/
//
//    We just remove the reference and pass through to the regular casting
//    code, and that appears to work.  Better solutions welcome, but it's not
//    exactly light reading to understand the rules in play:
//
//      https://stackoverflow.com/q/63801580/
//
// 6. In order to get a compile-time static assert that prohibits a particular
//    cast, there has to be some condition that disables the body of the code
//    holding the static_assert().  Also, you can't just static_assert() on
//    false, because any assert which doesn't depend on any template
//    variables will fire regardless of whether the SFINAE applies or not.
//


//=//// cast(Node*, ...) //////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Node*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and std::is_base_of<Node, V0>::value,  // upcasting, no check [4]
    const Node*>::type convert(V_* p) {
        return reinterpret_cast<const Node*>(p);
    }

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_base_of<Node, V0>::value,  // downcasting, check [4]
    const Node*>::type convert(V_* p) {
        static_assert(
            c_type_list<void, Byte>::contains<V0>(),
            "Invalid type for downcast to Node*"
        );

        if (not p)
            return nullptr;

        if (not (*reinterpret_cast<const Byte*>(p) & NODE_BYTEMASK_0x80_NODE))
            crash (p);

        return reinterpret_cast<const Node*>(p);
    }
};

template<typename V>
struct CastHelper<V*,Node*> {
    static constexpr Node* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Node*>(cast(const Node*, rr_cast(p)));
    }
};

template<typename V>
struct CastHelper<V*,Node*&&> {  // [5]
    static constexpr Node* convert(V* p)
      { return const_cast<Node*>(cast(Node*, p)); }
};


//=//// cast(Stub*, ...) //////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Stub*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and std::is_base_of<Stub, V0>::value,  // upcasting, no check [4]
    const Stub*>::type convert(V_* p) {
        return reinterpret_cast<const Stub*>(p);
    }

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_base_of<Stub, V0>::value,  // downcasting, check [4]
    const Stub*>::type convert(V_* p) {
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
};

template<typename V>
struct CastHelper<V*,Stub*> {
    static constexpr Stub* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Stub*>(
            CastHelper<decltype(rr_cast(p)), const Stub*>::convert(rr_cast(p))
            /*cast(const Stub*, rr_cast(p))*/);
    }
};

template<typename V>
struct CastHelper<V*,Stub*&&> {  // [5]
    static constexpr Stub* convert(V* p)
      { return const_cast<Stub*>(cast(Stub*, p)); }  // cast away ref
};


//=//// cast(Flex*, ...) //////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Flex*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and std::is_base_of<Flex, V0>::value,  // upcasting, no check [4]
    const Flex*>::type convert(V_* p) {
        return reinterpret_cast<const Flex*>(p);
    }

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_base_of<Flex, V0>::value,  // downcasting, check [4]
    const Flex*>::type convert(V_* p) {
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
};

template<typename V>
struct CastHelper<V*,Flex*> {
    static constexpr Flex* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Flex*>(
            CastHelper<decltype(rr_cast(p)), const Flex*>::convert(rr_cast(p))
            /*cast(const Flex*, rr_cast(p))*/);
    }
};

template<typename V>
struct CastHelper<V*,Flex*&&> {  // [5]
    static constexpr Flex* convert(V* p)
      { return const_cast<Flex*>(cast(Flex*, p)); }  // cast away ref
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Binary*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and std::is_base_of<Binary, V0>::value,  // upcasting, no check [4]
    const Binary*>::type convert(V_* p) {
        return reinterpret_cast<const Binary*>(p);
    }

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_base_of<Binary, V0>::value,  // downcasting, check [4]
    const Binary*>::type convert(V_* p) {
        static_assert(
            c_type_list<void,Byte,Node,Flex>::contains<V0>(),
            "Invalid type for downcast to Flex*"
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
    }
};

template<typename V>
struct CastHelper<V*,Binary*> {
    static constexpr Binary* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Binary*>(cast(const Binary*, rr_cast(p)));
    }
};


//=//// cast(String*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const String*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    static const String* convert(V* p) {
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
    }
};

template<typename V>
struct CastHelper<V*,String*> {
    static constexpr String* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<String*>(cast(const String*, rr_cast(p)));
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Symbol*> {  // [2]
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
    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value,  // [6]
    Symbol*>::type convert(V_* p) {
        UNUSED(p);
        static_assert(
            not std::is_same<V_,V>::value,  // [6]
            "Mutable cast on Symbol requested--use x_cast() if intentional"
        );
        return nullptr;
    }
};*/


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,const Array*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and std::is_base_of<Array, V0>::value,  // upcasting, no check [4]
    const Array*>::type convert(V_* p) {
        return reinterpret_cast<const Array*>(p);
    }

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_base_of<Array, V0>::value,  // downcasting, checked [4]
    const Array*>::type convert(V_* p) {
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
    }
};

template<typename V>
struct CastHelper<V*,Array*> {
    static constexpr Array* convert(V* pq) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Array*>(cast(const Array*, rr_cast(pq)));
    }
};


//=//// cast(VarList*, ...) ///////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,VarList*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_const<V>::value,
    VarList*>::type convert(V_* p) {
        static_assert(
            c_type_list<void,Byte,Node,Stub,Flex,Array>::contains<V0>(),
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
    }
};

template<typename V>
struct CastHelper<V*,const VarList*> {
    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value,  // [3]
    const VarList*>::type convert(V_* p) {
        static_assert(
            not std::is_same<V_,V>::value,
            "const VarList* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};


//=//// cast(Phase*, ...) ////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,Phase*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_const<V>::value,
    Phase*>::type convert(V_* p) {
        static_assert(
            c_type_list<void,Byte,Node,Stub,Flex,Array>::contains<V0>(),
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

template<typename V>
struct CastHelper<V*,const Phase*> {
    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value,  // [3]
    const Phase*>::type convert(V_* p) {
        static_assert(
            not std::is_same<V_, V>::value,
            "const Phase* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};


//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename V>  // [1]
struct CastHelper<V*,Level*> {  // [2]
    typedef typename std::remove_const<V>::type V0;

    template<typename V_ = V>
    static typename std::enable_if<
        std::is_same<V_, V>::value  // [3]
        and not std::is_const<V>::value,
    Level*>::type convert(V_* p) {
        static_assert(
            c_type_list<void,Byte,Node>::contains<V0>(),
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

template<typename V>
struct CastHelper<V*,const Level*> {
    template<typename V_ = V>
    static constexpr typename std::enable_if<
        std::is_same<V_, V>::value,  // [3]
    const Level*>::type convert(V_* p) {
        static_assert(
            not std::is_same<V_, V>::value,
            "const Level* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};
