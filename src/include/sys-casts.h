//
//  File: %sys-casts.h
//  Summary: {Instrumented operators for casting Node subclasses}
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// operation executes.  This means that if you have a datatype like Series or
// Cell, you can do runtime validation of the bits in these types when
// `cast(Series*, ptr)` or `cast(Cell*, ptr)` happen.  That's an extremely
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
// `cast_helper::convert<>` functions and ignore everything else.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. The main cast_helper template take two parameters: the "value's type (V)"
//    that is cast from, and the "type (T)" being cast to.  This file has
//    template partial specializations that only take one parameter: the `V`
//    type being cast from.  (e.g. we want to define an operator that does the
//    handling for casting to an Array*, that gets the arbitrary type being
//    cast from to inspect).  This single parameter is renamed `VP` since we
//    require it to be a pointer--which means we can make typedef aliases for
//    the type without a pointer being named `V`, etc.
//
// 2. The casts are implemented with a static method inside of a templated
//    class vs. just as a templated function.  This is because partial
//    specialization of function templates is not legal in C++ due to the
//    fact that functions can be overloaded, while classes can't:
//
//      http://www.gotw.ca/publications/mill17.htm
//
//    "If you're writing a function base template, prefer to write it as a
//     single function template that should never be specialized or overloaded,
//     and then implement the function template entirely as a simple handoff
//     to a class template containing a static function with the same
//     signature. Everyone can specialize that -- both fully and partially,
//     and without affecting the results of overload resolution."
//
// 3. We want to be able to pick different convert() implementations to use
//    based on properties of the type we are casting from.  So there can be
//    multiple convert() member functions.  Picking which operation is done
//    by enabling the appropriate function using enable_if<> and SFINAE.  But
//    to get SFINAE on member functions they have to themselves be templates:
//
//      https://stackoverflow.com/questions/11531989/
//
//    So at minimum you need a dummy template variable (VP_) and a dummy
//    check of it to cause the enable_if to work correctly.  <shrug>
//
// 4. By default, if you upcast (e.g. casting from a derived class like Array
//    to a base class like series), we do this with a zero-cost constexpr
//    that does the cast for free.  This is because every Array is-a Series,
//    and if you have an Array* in your hand we can assume you got it through
//    a means that you knew it was valid.  But if you downcast (e.g. from a
//    Node* to a Context*), then it's a riskier operation, so validation
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

template<typename VP>  // [1]
struct cast_helper<VP,const Node*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        std::is_base_of<Node, V0>::value,  // upcasting, no check [4]
    const Node*>::type convert(VP_ p) {
        return reinterpret_cast<const Node*>(p);
    }

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_base_of<Node, V0>::value),  // downcasting, checked [4]
    const Node*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value,
            "downcast(Node*) works on [void* Byte*]"
        );

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<const Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_FREE
        )) != (
            NODE_BYTEMASK_0x80_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<const Node*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,Node*> {
    static constexpr Node* convert(VP p) {
        typedef typename std::remove_pointer<VP>::type V;
        static_assert(!std::is_const<V>::value, "casting discards const");
        return const_cast<Node*>(cast(const Node*, rr_cast(p)));
    }
};

template<typename VP>
struct cast_helper<VP,Node*&&> {  // [5]
    static constexpr Node* convert(VP p)
      { return const_cast<Node*>(cast(Node*, p)); }
};


//=//// cast(Series*, ...) ////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,const Series*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        std::is_base_of<Series, V0>::value,  // upcasting, no check [4]
    const Series*>::type convert(VP_ p) {
        return reinterpret_cast<const Series*>(p);
    }

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_base_of<Series, V0>::value),  // downcasting, checked [4]
    const Series*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value,
            "downcast(Series*) works on [void* Byte* Node*]"
        );

        if (not p)
            return nullptr;

        if ((reinterpret_cast<const Stub*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<const Series*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,Series*> {
    static constexpr Series* convert(VP p) {
        typedef typename std::remove_pointer<VP>::type V;
        static_assert(!std::is_const<V>::value, "casting discards const");
        return const_cast<Series*>(
            cast_helper<decltype(rr_cast(p)), const Series*>::convert(rr_cast(p))
            /*cast(const Series*, rr_cast(p))*/);
    }
};

template<typename VP>
struct cast_helper<VP,Series*&&> {  // [5]
    static constexpr Series* convert(VP p)
      { return const_cast<Series*>(cast(Series*, p)); }  // cast away ref
};


//=//// cast(Binary*, ...) ////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,const Binary*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        std::is_base_of<Binary, V0>::value,  // upcasting, no check [4]
    const Binary*>::type convert(VP_ p) {
        return reinterpret_cast<const Binary*>(p);
    }

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_base_of<Binary, V0>::value),  // downcasting, checked [4]
    const Binary*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value
                or std::is_same<V0, Series>::value,
            "downcast(Series*) works on [void* Byte* Node* Series*]"
        );

        if (not p)
            return nullptr;

        if ((reinterpret_cast<const Stub*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        // assert series width here (trouble with Series_Wide() from within
        // cast at the moment)

        return reinterpret_cast<const Binary*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,Binary*> {
    static constexpr Binary* convert(VP p) {
        typedef typename std::remove_pointer<VP>::type V;
        static_assert(!std::is_const<V>::value, "casting discards const");
        return const_cast<Binary*>(cast(const Binary*, rr_cast(p)));
    }
};


//=//// cast(String*, ...) ////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,const String*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    static const String* convert(VP p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value
                or std::is_same<V0, Series>::value
                or std::is_same<V0, Binary>::value,
            "downcast(String*) works on [void* Byte* Node* Series* Binary*]"
        );

        if (not p)
            return nullptr;

        if (((reinterpret_cast<const Stub*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        ))
        ) !=
            NODE_FLAG_NODE
        ){
            panic (p);
        }

        const Byte flavor = reinterpret_cast<const Byte*>(p)[2];
        if (flavor != FLAVOR_STRING and flavor != FLAVOR_SYMBOL)
            panic (p);

        return reinterpret_cast<const String*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,String*> {
    static constexpr String* convert(VP p) {
        typedef typename std::remove_pointer<VP>::type V;
        static_assert(!std::is_const<V>::value, "casting discards const");
        return const_cast<String*>(cast(const String*, rr_cast(p)));
    }
};


//=//// cast(Symbol*, ...) ////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,const Symbol*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    static const Symbol* convert(VP p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value
                or std::is_same<V0, Series>::value
                or std::is_same<V0, Binary>::value
                or std::is_same<V0, String>::value,
            "downcast(Symbol*) works on [void* Byte* Node* Series* Binary* String*]"
        );

        if (not p)
            return nullptr;

        if (((reinterpret_cast<const Stub*>(p)->header.bits & (
            SERIES_MASK_SYMBOL
                | NODE_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        ))
        ) !=
            SERIES_MASK_SYMBOL
        ){
            panic (p);
        }

        return reinterpret_cast<const Symbol*>(p);
    }
};

// If we didn't supply a cast in the const case, it would be unchecked.  The
// only time Symbols should be mutable is at creation time, or when bits are
// being tweaked in binding slots.  Stored or external pointers should always
// be const if downcasting.

/*
template<typename VP>
struct cast_helper<VP,Symbol*> {
    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value,  // [6]
    Symbol*>::type convert(VP_ p) {
        UNUSED(p);
        static_assert(
            !std::is_same<VP_,VP>::value,  // [6]
            "Mutable cast on Symbol requested--use x_cast() if intentional"
        );
        return nullptr;
    }
};*/


//=//// cast(Array*, ...) /////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,const Array*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        std::is_base_of<Array, V0>::value,  // upcasting, no check [4]
    const Array*>::type convert(VP_ p) {
        return reinterpret_cast<const Array*>(p);
    }

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_base_of<Array, V0>::value),  // downcasting, checked [4]
    const Array*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Series>::value
                or std::is_same<V0, Node>::value,
            "downcast(Array*) works on [void* Byte* Node* Series*]"
        );

        if (not p)
            return nullptr;

        if ((reinterpret_cast<const Stub*>(p)->header.bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
        )) != (
            NODE_FLAG_NODE
        )){
            panic (p);
        }

        return reinterpret_cast<const Array*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,Array*> {
    static constexpr Array* convert(VP pq) {
        typedef typename std::remove_pointer<VP>::type V;
        static_assert(!std::is_const<V>::value, "casting discards const");
        return const_cast<Array*>(cast(const Array*, rr_cast(pq)));
    }
};


//=//// cast(Context*, ...) ///////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,Context*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_const<V>::value),
    Context*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value
                or std::is_same<V0, Series>::value
                or std::is_same<V0, Array>::value,
            "downcast(Context*) works on [void* Byte* Node* Series* Array*]"
        );

        if (not p)
            return nullptr;

        if (((reinterpret_cast<Stub*>(p)->header.bits & (
            SERIES_MASK_VARLIST
                | NODE_FLAG_FREE
                | NODE_FLAG_CELL
                | FLAG_FLAVOR_BYTE(255)
        ))
            | SERIES_FLAG_DYNAMIC  // permit non-dynamic (e.g. inaccessible
        ) !=
            SERIES_MASK_VARLIST
        ){
            panic (p);
        }

        return reinterpret_cast<Context*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,const Context*> {
    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value,  // need VP_ reference for SFINAE
    const Context*>::type convert(VP_ p) {
        static_assert(
            !std::is_same<VP_,VP>::value,
            "const Context* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};


//=//// cast(Action*, ...) ////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,Action*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_const<V>::value),
    Action*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value
                or std::is_same<V0, Series>::value
                or std::is_same<V0, Array>::value,
            "downcast(Action*) works on [void* Byte* Node* Series* Array*]"
        );

        if (not p)
            return nullptr;

        if (reinterpret_cast<Byte*>(p)[2] == FLAVOR_DETAILS) {
            if ((reinterpret_cast<const Stub*>(p)->header.bits & (
                SERIES_MASK_DETAILS
                    | NODE_FLAG_FREE
                    | NODE_FLAG_CELL
                    | FLAG_FLAVOR_BYTE(255)
            )) !=
                SERIES_MASK_DETAILS
            ){
                panic (p);
            }
        }
        else {
            if (((reinterpret_cast<Stub*>(p)->header.bits & (
                      SERIES_MASK_VARLIST
                      | NODE_FLAG_FREE
                      | NODE_FLAG_CELL
                      | FLAG_FLAVOR_BYTE(255)
                      ))
                 | SERIES_FLAG_DYNAMIC  // permit non-dynamic (e.g. inaccessible
                 ) !=
                SERIES_MASK_VARLIST
            ){
                panic (p);
            }
        }

        return reinterpret_cast<Action*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,const Action*> {
    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value,  // need VP_ reference for SFINAE
    const Action*>::type convert(VP_ p) {
        static_assert(
            !std::is_same<VP_,VP>::value,
            "const Action* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};


//=//// cast(Level*, ...) /////////////////////////////////////////////////=//

template<typename VP>  // [1]
struct cast_helper<VP,Level*> {  // [2]
    typedef typename std::remove_pointer<VP>::type V;
    typedef typename std::remove_const<V>::type V0;

    template<typename VP_ = VP>
    static typename std::enable_if<
        std::is_same<VP_, VP>::value &&  // [3]
        !(std::is_const<V>::value),
    Level*>::type convert(VP_ p) {
        static_assert(
            std::is_same<V0, void>::value
                or std::is_same<V0, Byte>::value
                or std::is_same<V0, Node>::value,
            "downcast(Level*) works on [void* Byte* Node*]"
        );

        if (not p)
            return nullptr;

        if ((*reinterpret_cast<const Byte*>(p) & (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x40_FREE
                | NODE_BYTEMASK_0x01_CELL
        )) != (
            NODE_BYTEMASK_0x80_NODE | NODE_BYTEMASK_0x01_CELL
        )){
            panic (p);
        }

        return reinterpret_cast<Level*>(p);
    }
};

template<typename VP>
struct cast_helper<VP,const Level*> {
    template<typename VP_ = VP>
    static constexpr typename std::enable_if<
        std::is_same<VP_, VP>::value,  // need VP_ reference for SFINAE
    const Level*>::type convert(VP_ p) {
        static_assert(
            !std::is_same<VP_,VP>::value,
            "const Level* pointers currently shouldn't exist, can't cast to"
        );
        UNUSED(p);
        return nullptr;
    }
};
