//
//  File: %struct-value.h
//  Summary: "Value structure defininitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
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


//=//// VARS and PARAMs ///////////////////////////////////////////////////=//
//
// These are lightweight classes on top of cells that help catch cases of
// testing for flags that only apply if you're sure something is a parameter
// cell or variable cell.
//

#if DEBUG_USE_CELL_SUBCLASSES
    struct Param : public REBVAL {};

    INLINE const Param* cast_PAR(const REBVAL *v)
        { return c_cast(Param*, v); }

    INLINE Param* cast_PAR(REBVAL *v)
        { return cast(Param*, v); }
#else
    #define Param REBVAL

    #define cast_PAR(v) (v)
#endif


// Because atoms are supersets of value, you may want to pass an atom to a
// function that writes a value.  But such passing is usually illegal, due
// to wanting to protect functions that only expect stable isotopes from
// getting unstable ones.  So you need to specifically point out that the
// atom is being written into and its contents not heeded.
//
// In the debug build we can give this extra teeth by wiping the contents
// of the atom, to ensure they are not examined.
//
#define Stable_Unchecked(atom) \
    x_cast(Value*, ensure(const Atom*, (atom)))

INLINE REBVAL* Freshen_Cell_Untracked(Cell* v);

#if DEBUG_USE_CELL_SUBCLASSES  // wrapper has runtime cost
    template<typename T>
    struct SinkWrapper {
        T* p;

        SinkWrapper() = default;  // or MSVC warns making Option(Sink(Value*))
        SinkWrapper(nullptr_t) : p (nullptr) {}

        template<
            typename U,
            typename std::enable_if<
                std::is_base_of<U,T>::value  // e.g. pass Atom to Sink(Element)
            >::type* = nullptr
        >
        SinkWrapper(U* u) : p (u_cast(T*, u)) {
            /* Init_Unreadable(p); */
        }

        template<
            typename U,
            typename std::enable_if<
                std::is_base_of<U,T>::value  // e.g. pass Atom to Sink(Element)
            >::type* = nullptr
        >
        SinkWrapper(SinkWrapper<U> u) : p (u_cast(T*, u.p)) {
        }

        operator bool () const { return p != nullptr; }

        operator T* () const { return p; }

        operator copy_const_t<Node,T>* () const { return p; }

        explicit operator copy_const_t<Byte,T>* () const
          { return reinterpret_cast<copy_const_t<Byte,T>*>(p); }

        T* operator->() const { return p; }
    };

    #define Sink(T) SinkWrapper<std::remove_pointer<T>::type>
    #define Need(T) SinkWrapper<std::remove_pointer<T>::type>

    template<>
    struct c_cast_helper<Byte*, Sink(Value*) const&> {
        typedef Byte* type;
    };

    template<typename V, typename T>
    struct cast_helper<SinkWrapper<V>,T>
      { static T convert(SinkWrapper<V> v) { return (T)(v.p);} };
#else
    #define Sink(T) T
    #define Need(T) T
#endif


//=//// EXTANT STACK POINTERS /////////////////////////////////////////////=//
//
// See %sys-stack.h for a deeper explanation.
//
// Even with this definition, the intersecting needs of DEBUG_CHECK_CASTS and
// DEBUG_EXTANT_STACK_POINTERS means there will be some cases where distinct
// overloads of Value* vs. Element* vs Cell* will wind up being ambiguous.
// For instance, VAL_DECIMAL(StackValue(*)) can't tell which checked overload
// to use.  Then you have to cast, e.g. VAL_DECIMAL(cast(Value*, stackval)).
//
#if (! DEBUG_EXTANT_STACK_POINTERS)
    #define StackValue(p) REBVAL*
#else
    struct StackValuePointer;
    #define StackValue(p) StackValuePointer
#endif
