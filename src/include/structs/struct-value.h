//
//  file: %struct-value.h
//  summary: "Value structure defininitions preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
    m_cast(Value*, ensure(Atom*, (atom)))


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
    #define OnStack(TP)  OnStackPointer<TP>
#endif
