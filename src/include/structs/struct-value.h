//
//  file: %struct-value.h
//  summary: "Subclasses of Cell to quarantine LIFT_BYTE() states"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2022-2026 Ren-C Open Source Contributors
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=/////////////////////////////////////////////////////////////////////////=//
//
// Systemically, we want to stop antiforms from being put into the array
// elements of blocks, groups, paths, and tuples.  We also want to prevent
// unstable antiforms from being the values of variables.  To make it easier
// to do this, the C++ build offers the ability to make `Element` that
// can't hold any antiforms, `Stable*` that can hold stable antiforms, and
// `Value` that can hold anything--including unstable isotopes.
//
// * Class Hierarchy: Value as base, Stable* derived, Element derived
//   (upside-down for compile-time error preferences--we want passing an
//   Value to a routine that expects only Element to not compile)
//
// * Primary Goal: Prevent passing Atoms/Stables to Element-only routines,
//   or Atoms to Stable*-only routines.
//
// * Secondary Goal: Prevent things like passing Element cells to writing
//   routines that may potentially produce antiforms in that cell.
//
// * Tertiary Goal: Detect things like superfluous Is_Antiform() calls
//   being made on Elements.
//
// The primary goal is achieved by choosing Element as a most-derived type
// instead of a base type.  The next two goals are trickier, and require a
// smart pointer class to wrap the pointers and invert the class hierarchy
// in terms of what are accepted for initialization (see Sink() and Exact()).
//
// Additionally, the Cell* class is differentiated by not allowing you to
// ask for its "type".  This makes it useful in passing to routines that
// are supposed to act agnostically regarding the quoting level of the cell,
// such as molding...where the quoting level is accounted for by the core
// molding process, and mold callbacks are only supposed to account for the
// cell payloads.
//


//=//// SLOTS /////////////////////////////////////////////////////////////=//
//
// Contexts like OBJECT!, MODULE!, FRAME!, LET!, etc. store "variables".  A
// Cell that holds a variable's value is called a "Slot".  Slots have special
// considerations for handling, because they may store bit patterns that
// indicate a function should be run to fulfill the variable (a "GETTER") or
// a function should be run to accept a value to store (a "SETTER").
// These considerations apply when the Slot's LIFT_BYTE() >= MIN_TYPE_BEDROCK
//
// This means you can't casually use something like Init_Integer() or
// Copy_Cell() to blindly write bit patterns into a Slot, because it might
// overlook handling of the special cases.  And you can't use functions like
// Type_Of() to read a Slot, either.  This means Slots have to go through
// special functions that in the general case, may run arbitrary code in
// the evaluator.
//
// There is one exception: an Init(Slot) e.g. what you get from adding a
// fresh variable to a context, is able to be initialized by any routine
// that could do an Init(Param/Value/Stable/Element).
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Slot;
#else
    struct Slot : public Cell {};  // can hold unstable antiforms
#endif


//=//// "Param" SUBCLASS OF "Slot" ////////////////////////////////////////=//
//
// The datatype ParamList holds a list of PARAMETER! values with LIFT_BYTE()
// of TYPE_BEDROCK_HOLE for unspecialized arguments.  Then any Value* (possibly
// unstable) for specialized args and locals.
//
// The Cells in the list are subtyped as `Param`.  They could have used
// an existing subclass like `Slot`, however calling it Param helps indicate
// a cell uses CELL_FLAG_NOTE for CELL_FLAG_PARAM_NOTE_TYPECHECKED (for
// example), and are constrained to just TYPE_BEDROCK_HOLE.
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Param;
#else
    struct Param : public Slot {};  // inherits Slot (Base)
#endif

typedef Param Arg;  // !!! Args should be just Slot; review.


//=//// "VALUE" CELLS /////////////////////////////////////////////////////=//
//
// If we are in the C build, then the *only* Cell struct is RebolValueStruct,
// and it can hold anything.  So we define Cell as a synonym for that.
//
// In the C++ build, Cell is the base class, and we define RebolValueStruct
// (the one exposed through the API) as being the cell in the subclass
// hierarchy that's *right* above Param.  So it can't hold any BEDROCK
// states, but can hold any unstable antiform.
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Cell;
#else
    struct RebolValueStruct : public Param {};  // can hold unstable antiforms
#endif


//=//// STABLE CELLS //////////////////////////////////////////////////////=//
//
// A "Stable" cell is one that can hold stable antiforms, but not unstable
// ones.  So no PACK! or FAILURE! or TRASH! or VOID!.  But SPLICE! and LOGIC!
// and DATATYPE! are all fine.
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Stable;
#else
    struct Stable : public RebolValueStruct {};  // can't hold unstable forms
#endif


//=//// ELEMENTS //////////////////////////////////////////////////////////=//
//
// An "Element" is an element in the sense of "Array Element", e.g. anything
// you can put in a BLOCK!, GROUP!, FENCE!.  So not an antiform.
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Element;
#else
    struct Element : public Stable {};  // can't hold any antiforms
#endif


//=//// DUALS /////////////////////////////////////////////////////////////=//
//
// Some parts of the system want to be able to represent a value that could
// be in a BEDROCK state, but push it "in-band" of normal values.  This is
// done by taking most values and putting them in lifted representation, and
// then using the unlifted states to represent BEDROCK.
//
// PACK!, for example, contains "dual values"...they are Element* (because
// they have to be, to be in a List).  But the representational conception is
// that the values are lifted unless they are signals in the unlifted state.)
//
// !!! Should there be a `Lifted` subclass, holding Quoted/Quasi?  Currently
// functions like Lift_Cell() just return Dual...
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Dual;
#else
    struct Dual : public Element {};
#endif


//=//// ENSURE CELL TYPES ARE STANDARD LAYOUT /////////////////////////////=//
//
// Using too much C++ magic can potentially lead to the exported structure
// not having "standard layout" and being incompatible with C.  We want to be
// able to memcpy() cells safely, so check to ensure that is still the case.
//
#if CPLUSPLUS_11
    static_assert(
        std::is_standard_layout<Cell>::value
            and std::is_standard_layout<Slot>::value
            and std::is_standard_layout<Param>::value
            and std::is_standard_layout<Value>::value
            and std::is_standard_layout<Stable>::value
            and std::is_standard_layout<Element>::value
            and std::is_standard_layout<Dual>::value,
        "C++ Cells must match C Cells: http://stackoverflow.com/a/7189821/"
    );
#endif


//=//// STOP INIT/SINK CONVERSIONS FOR PLAIN SLOT* ////////////////////////=//
//
// Because a Slot can contain BEDROCK states with bit patterns that are
// things like SETTERs or GETTERs, or ALIASes...you can't necessarily assume
// writing into a Slot can be done by just overwriting its bit pattern.  Thus
// variable writing has to go through an abstraction layer.
//
// The exception is when a Slot is being initialized for the first time, so
// it is legal to do pass an Init(Slot) to any routine that does Init(Cell)
// for any subclass.
//

#if CHECK_CELL_SUBCLASSES
  #if NEEDFUL_CONTRAS_USE_WRAPPER
  namespace needful {
    template<>
    struct MayUseIndirectEncoding<Slot> : std::true_type {};
  }
  #endif
#endif


//=//// EXTANT STACK POINTERS /////////////////////////////////////////////=//
//
// See %sys-datastack.h for a deeper explanation.
//
// Even with this definition, the intersecting needs of DEBUG_CHECK_CASTS and
// DEBUG_EXTANT_STACK_POINTERS means there will be some cases where distinct
// overloads of Stable* vs. Element* vs Cell* will wind up being ambiguous.
// For instance, VAL_DECIMAL(OnStack(Stable*)) can't tell which checked overload
// to use.  Then you have to cast, e.g. VAL_DECIMAL(cast(Stable*, stackval)).
//
#if (! DEBUG_EXTANT_STACK_POINTERS)
    #define OnStack(T)  T
#else
    template<typename T>
    struct OnStackPointer;
    #define OnStack(T)  OnStackPointer<T>
#endif
