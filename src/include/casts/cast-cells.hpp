//
//  file: %cast-cells.hpp
//  summary: "Instrumented operators for casting Cell subclasses"
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
// This file is specifically for checking casts to Cells.
//
// One benefit is we can check cells for valid readability at the moment of
// the cast.  While this doesn't seem too profound since attempts to read
// the Cell would trigger failures anyway even without the casts...it does
// help with locality.  Also makes sure that locations are accurately labeled
// as to whether they have a valid Element/Value/Atom or are Sink()/Init().
//
// Another big benefit is that casts to Element can enure that no antiforms
// are in the cell, and casts to Value don't hold unstable antiforms.  This
// could be accomplished without casts with helper functions such as
// Known_Element() or Known_Stable(), but using a cast makes the checks
// work generically in macros parameterized by type.  Also, using a cast
// helps point out "ugliness" that encourages caution at these points, and
// looking to find another way to do it.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A. See README.md's explanation of why we specialize one parameter and
//    leave the other free, so CastHelper fixes the type cast to while the
//    type being cast from is arbitrary and can be subsetted or reacted to.
//
// B. See README.md's explanation of why you need both a const T* and T*
//    case in your handling, due to general design nature of C++ and const.
//
// C. A loophole we throw in here is that "checked" casts of Byte* or char*
//    to a Cell subclass are not validated.  This random-seeming choice is
//    just a way to avoid having to come up with arbitrary variations of
//    unchecked casts, such as "unchecked const-preserving cast"...because
//    there's diminishing benefit, given that the casts from Byte* pretty
//    much always are on raw array data that has to be tolerant of being
//    at a tail condition.  If you want validation of Byte* or char* data
//    then just cast to a void* first.
//

//=//// cast(Atom*, ...) //////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*, const Atom*> {  // const Atom* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Atom* convert(V* p) {
        if (c_type_list<Byte,char>::contains<V0>())  // exempt Byte/char [C]
            return reinterpret_cast<const Atom*>(p);

        const Cell* c = reinterpret_cast<const Cell*>(p);
        Assert_Cell_Readable(c);
        unnecessary(LIFT_BYTE(c) >= ANTIFORM_0);  // always true
        return reinterpret_cast<const Atom*>(c);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Atom*> {  // Atom* case [B]
    static Atom* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Atom*>(cast(const Atom*, p));
    }
};


//=//// cast(Value*, ...) //////////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*, const Value*> {  // const Value* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Value* convert(V* p) {
        if (c_type_list<Byte,char>::contains<V0>())  // exempt Byte/char* [C]
            return reinterpret_cast<const Value*>(p);

        const Cell* c = reinterpret_cast<const Cell*>(p);
        Assert_Cell_Readable(c);
        if (LIFT_BYTE(c) == ANTIFORM_0)
            assert(Is_Stable_Antiform_Heart_Byte(HEART_BYTE_RAW(c)));
        return reinterpret_cast<const Value*>(c);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Value*> {  // Value* case [B]
    static Value* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Value*>(cast(const Value*, p));
    }
};


//=//// cast(Element*, ...) ///////////////////////////////////////////////=//

template<typename V>  // [A]
struct CastHelper<V*, const Element*> {  // const Element* case [B]
    typedef typename std::remove_const<V>::type V0;

    static const Element* convert(V* p) {
        if (c_type_list<Byte,char>::contains<V0>())  // exempt Byte/char* [C]
            return reinterpret_cast<const Element*>(p);

        const Cell* c = reinterpret_cast<const Cell*>(p);
        Assert_Cell_Readable(c);
        assert(LIFT_BYTE(c) != ANTIFORM_0);
        return reinterpret_cast<const Element*>(c);
    }
};

template<typename V>  // [A]
struct CastHelper<V*,Element*> {  // Element* case [B]
    static Element* convert(V* p) {
        static_assert(not std::is_const<V>::value, "casting discards const");
        return const_cast<Element*>(cast(const Element*, p));
    }
};
