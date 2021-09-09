//
//  File: %sys-rebcel.h
//  Summary: "Low level structure definitions for Reb_Value"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information.
//
// Licensed under the Lesser GPL, Version 3.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// https://www.gnu.org/licenses/lgpl-3.0.html
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In the C build, there is simply one structure definition for all value
// cells: the `Reb_Value`.  This is defined in %sys-rebval.h, and most of
// the contents of this file are a no-op.
//
// However, the C++ build breaks down various base classes for values that
// serve roles in type-checking.  The underlying bit pattern is the same,
// but which functions will accept the subclass varies according to what
// is legal for that pattern to do.  (In C, such conventions can only be
// enforced by rule-of-thumb...so building as C++ gives the rules teeth.)
//


//=//// "RAW" CELLS ///////////////////////////////////////////////////////=//
//
// A raw cell is just the structure, with no additional protections.  This
// makes it useful for embedding in REBSER, because if it had disablement of
// things like assignment then it would also carry disablement of memcpy()
// of containing structures...which would limit that definition.  These
// cells should not be used for any other purposes.
//
#if !defined(CPLUSPLUS_11)
    #define REBRAW struct Reb_Value
#else
    #define REBRAW struct Reb_Cell  // usually only const, see REBCEL(const*)
#endif


//=//// RELATIVE VALUES ///////////////////////////////////////////////////=//
//
// A "relative" value is a view of a value cell that can't be looked up to
// find a value unless it is coupled with a "specifier".  The bit pattern
// inside the cell may actually be "absolute"--e.g. no specifier needed--but
// many routines accept a relative view as a principle-of-least-privilege.
// (e.g. you can get the symbol of a word regardless of whether it is
// absolute or relative).
//
// Note that in the C build, %rebol.h forward-declares `struct Reb_Value` and
// then #defines REBVAL to that.
//
#if !defined(CPLUSPLUS_11)
    #define RELVAL \
        struct Reb_Value // same as REBVAL, no checking in C build
#else
    struct Reb_Relative_Value; // won't implicitly downcast to REBVAL
    #define RELVAL \
        struct Reb_Relative_Value // *might* be IS_RELATIVE()
#endif


//=//// EXTANT STACK POINTERS /////////////////////////////////////////////=//
//
// See %sys-stack.h for a deeper explanation.  This has to be declared in
// order to put in one of REBCEL(const*)'s implicit constructors.  Because
// having the STKVAL(*) have a user-defined conversion to REBVAL* won't
// get that...and you can't convert to both REBVAL* and REBCEL(const*) as
// that would be ambiguous.
//
// Even with this definition, the intersecting needs of DEBUG_CHECK_CASTS and
// DEBUG_EXTANT_STACK_POINTERS means there will be some cases where distinct
// overloads of REBVAL* vs. REBCEL(const*) will wind up being ambiguous.
// For instance, VAL_DECIMAL(STKVAL(*)) can't tell which checked overload
// to use.  In such cases, you have to cast, e.g. VAL_DECIMAL(VAL(stackval)).
//
#if (! DEBUG_EXTANT_STACK_POINTERS)
    #define STKVAL(p) REBVAL*
#else
    struct Reb_Stack_Value_Ptr;
    #define STKVAL(p) Reb_Stack_Value_Ptr
#endif


//=//// ESCAPE-ALIASABLE CELLS ////////////////////////////////////////////=//
//
// The system uses a trick in which the type byte is bumped by multiples of
// 64 to indicate up to 3 levels of escaping.  VAL_TYPE() will report these
// as being REB_QUOTED, but the entire payload for them is in the cell.
//
// Most of the time, routines want to see these as being QUOTED!.  But some
// lower-level routines (like molding or comparison) want to be able to act
// on them in-place witout making a copy.  To ensure they see the value for
// the "type that it is" and use CELL_KIND() and not VAL_TYPE(), this alias
// for RELVAL prevents VAL_TYPE() operations.
//
// Because a REBCEL can be linked to by a QUOTED!, it's important not to
// modify the potentially-shared escaped data.  So all REBCEL* should be
// const.  That's enforced in the C++ debug build, and a wrapping class is
// used for the pointer to make sure one doesn't assume it lives in an array
// and try to do pointer math on it...since it may be a singular allocation.
//
// Note: This needs special handling in %make-headers.r to recognize the
// format.  See the `typemacro_parentheses` rule.
//
#if !defined(CPLUSPLUS_11)

    #define REBCEL(const_star) \
        const struct Reb_Value *  // same as RELVAL, no checking in C build

#elif (! DEBUG_CHECK_CASTS)
    //
    // The %sys-internals.h API is used by core extensions, and we may want
    // to build the executable with C++ but an extension with C.  If there
    // are "trick" pointer types that are classes with methods passed in
    // the API, that would inhibit such an implementation.
    //
    // Making it easy to configure such a mixture isn't a priority at this
    // time.  But just make sure some C++ builds are possible without
    // using the active pointer class.  Choose debug builds for now.
    //
    struct Reb_Cell;  // won't implicitly downcast to RELVAL
    #define REBCEL(const_star) \
        const struct Reb_Cell *  // not a class instance in %sys-internals.h
#else
    // This heavier wrapper form of Reb_Cell() can be costly...empirically
    // up to 10% of the runtime, since it's called so often.  But it protects
    // against pointer arithmetic on REBCEL().
    //
    struct Reb_Cell;  // won't implicitly downcast to RELVAL
    template<typename T>
    struct RebcellPtr {
        T p;
        static_assert(
            std::is_same<const Reb_Cell*, T>::value,
            "Instantiations of REBCEL only work as REBCEL(const*)"
        );

        RebcellPtr () { }
        RebcellPtr (const Reb_Cell *p) : p (p) {}
        RebcellPtr (STKVAL(*) p) : p (p) {}

        const Reb_Cell **operator&() { return &p; }
        const Reb_Cell *operator->() { return p; }
        const Reb_Cell &operator*() { return *p; }

        operator const Reb_Cell* () { return p; }

        explicit operator const Reb_Value* ()
          { return reinterpret_cast<const Reb_Value*>(p); }

        explicit operator const Reb_Relative_Value* ()
          { return reinterpret_cast<const Reb_Relative_Value*>(p); }
    };
    #define REBCEL(const_star) \
        struct RebcellPtr<Reb_Cell const_star>
#endif
