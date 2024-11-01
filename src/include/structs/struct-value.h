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
    struct Param : public Value {};

    INLINE const Param* cast_PAR(const Value* v)
        { return c_cast(Param*, v); }

    INLINE Param* cast_PAR(Value* v)
        { return cast(Param*, v); }
#else
    #define Param Value

    #define cast_PAR(v) (v)
#endif


#define Stable_Unchecked(atom) \
    x_cast(Value*, ensure(const Atom*, (atom)))


// Because atoms are supersets of value, you may want to pass an atom to a
// function that writes a value.  But such passing is usually illegal, due
// to wanting to protect functions that only expect stable isotopes from
// getting unstable ones.  So you need to specifically point out that the
// atom is being written into and its contents not heeded.
//
// We do this with the Sink() wrapper class (which must be enabled in order
// for the DEBUG_USE_CELL_SUBCLASSES to work).  We have to extend it with
// some helpers.
//
// In the checked build we can give this extra teeth by wiping the contents
// of the atom, to ensure they are not examined.
//
#if DEBUG_USE_CELL_SUBCLASSES  // Note: Sink(Value) wrapper has runtime cost
    template<>
    struct c_cast_helper<Byte*, Sink(Value) const&>
      { typedef Byte* type; };

    template<typename V, typename T>
    struct cast_helper<NeedWrapper<V, true>,T>
      { static T convert(NeedWrapper<V, true> v) { return (T)(v.p);} };

    template<typename V, typename T>
    struct cast_helper<NeedWrapper<V, false>,T>
      { static T convert(NeedWrapper<V, false> v) { return (T)(v.p);} };

    // !!! Originally when NeedWrapper was specific to Cell* subclasses, it
    // had these methods.  But commenting them out didn't break anything.
    // Now that it's generic it shouldn't mention Node...and it's unclear why
    // they were there.  It may be that this predated the cast operator being
    // generalized and what it was for is now done a better way, but hold onto
    // for a bit until sure.
    /*
        struct NeedWrapper { ...
            operator copy_const_t<Node,T>* () const
              { return p; }
            explicit operator copy_const_t<Byte,T>* () const
              { return reinterpret_cast<copy_const_t<Byte,T>*>(p); }
        ... };
    */

   #if (! DEBUG_STATIC_ANALYZING)
      template<typename V>
      void Corrupt_If_Debug(NeedWrapper<V, true> sink)
         { Corrupt_If_Debug(*sink.p); }
   #endif
#endif


//=//// EXTANT STACK POINTERS /////////////////////////////////////////////=//
//
// See %sys-datastack.h for a deeper explanation.
//
// Even with this definition, the intersecting needs of DEBUG_CHECK_CASTS and
// DEBUG_EXTANT_STACK_POINTERS means there will be some cases where distinct
// overloads of Value* vs. Element* vs Cell* will wind up being ambiguous.
// For instance, VAL_DECIMAL(OnStack(Value*)) can't tell which checked overload
// to use.  Then you have to cast, e.g. VAL_DECIMAL(cast(Value*, stackval)).
//
#if (! DEBUG_EXTANT_STACK_POINTERS)
    #define OnStack(TP) TP
#else
    template<typename T>
    struct OnStackPointer;
    #define OnStack(TP) OnStackPointer<typename std::remove_pointer<TP>::type>
#endif
