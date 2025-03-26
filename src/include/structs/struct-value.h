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


//=//// "Param" SUBCLASS OF "Value" ///////////////////////////////////////=//
//
// There are some tests (e.g. for Is_Specialized()) which interprets the
// CELL_FLAG_NOTE in a particular way.  Having a subclass to help indicate
// when this test is meaningful was believed to add some safety.
//
#if CHECK_CELL_SUBCLASSES
    struct Param : public Element {};
#else
    typedef Element Param;
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
// for the CHECK_CELL_SUBCLASSES to work).  We have to extend it with
// some helpers.
//
// In the checked build we can give this extra teeth by wiping the contents
// of the atom, to ensure they are not examined.
//
#if CHECK_CELL_SUBCLASSES  // Note: Sink(Value) wrapper has runtime cost
    template<>
    struct c_cast_helper<Byte*, Sink(Value) const&>
      { typedef Byte* type; };

    template<typename V, typename T, bool Vsink>
    struct cast_helper<NeedWrapper<V, Vsink>,T> {
        static T convert(const NeedWrapper<V, Vsink>& wrapper) {
            using MV = typename std::remove_const<V>::type;
            if (wrapper.corruption_pending) {
                Corrupt_If_Debug(*const_cast<MV*>(wrapper.p));
                wrapper.corruption_pending = false;
            }
            return (T)(wrapper.p);
        }
    };
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
