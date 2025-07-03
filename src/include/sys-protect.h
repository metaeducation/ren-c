//
//  file: %sys-protect.h
//  summary: "System Const and Protection Functions"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018-2024 Ren-C Open Source Contributors
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
// R3-Alpha introduced the idea of "protected" series and variables.  Ren-C
// introduces a new form of read-only-ness that is not a bit on Flexes, but
// rather bits on Cell instances.  This means that a value can be a read-only
// view of a series that is otherwise mutable.
//
//=//// NOTES ////////////////////////////////////////////////////////////=//
//
// * Checking for read access was a half-baked feature in R3-Alpha, as heeding
//   the protection bit had to be done by hand by any code doing mutations.
//   Many places in the code forgot the check.  Ren-C uses `const Flex*`
//   to default to immutable access, and using functions that mutate cells
//   requires you to consciously use a routine that checks the Flex at
//   runtime before it will give you back a plain `Flex*` from which you can
//   get non-const `Cell*`.  See CONST_IF_C() for why this is only done in
///  the C++ build.
//


INLINE void Protect_Cell(Cell* c) {
    assert(Not_Cell_Flag(c, PROTECTED));
    Set_Cell_Flag(c, PROTECTED);
}

INLINE void Unprotect_Cell(Cell* c) {
    assert(Get_Cell_Flag(c, PROTECTED));
    Clear_Cell_Flag(c, PROTECTED);
}


// There are some functions that set the output cell to protected to make
// sure it's not changed.  But if throwing gets in the mix, that means the
// code path that would clean it up may not be run.  Clear it.
//
#if NO_RUNTIME_CHECKS
    #define Clear_Lingering_Out_Cell_Protect_If_Debug(L)  NOOP
#else
    #define Clear_Lingering_Out_Cell_Protect_If_Debug(L) \
        ((L)->out->header.bits &= ~(CELL_FLAG_PROTECTED))
#endif


//
// Freezing and Locking
//

INLINE void Freeze_Flex(const Flex* f) {  // there is no unfreeze
    assert(not Stub_Holds_Cells(f)); // use Deep_Freeze_Source

    // We set the FROZEN_DEEP flag even though there is no structural depth
    // here, so that the generic test for deep-frozenness can be faster.
    //
    Set_Flex_Info(f, FROZEN_SHALLOW);
    Set_Flex_Info(f, FROZEN_DEEP);
}

INLINE bool Is_Flex_Frozen(const Flex* f) {
    assert(not Stub_Holds_Cells(f));  // use Is_Array_Deeply_Frozen
    if (Not_Flex_Info(f, FROZEN_SHALLOW))
        return false;
    assert(Get_Flex_Info(f, FROZEN_DEEP));  // true on frozen non-arrays
    return true;
}

INLINE bool Is_Flex_Read_Only(const Flex* f) {  // may be temporary
    return 0 != (FLEX_INFO(f) &
        (FLEX_INFO_HOLD | FLEX_INFO_PROTECTED
        | FLEX_INFO_FROZEN_SHALLOW | FLEX_INFO_FROZEN_DEEP)
    );
}


// Gives the appropriate kind of error message for the reason the series is
// read only (frozen, running, protected, locked to be a map key...)
//
// !!! Should probably report if more than one form of locking is in effect,
// but if only one error is to be reported then this is probably the right
// priority ordering.
//

INLINE void Panic_If_Read_Only_Flex(const Flex* f) {
    if (not Is_Flex_Read_Only(f))
        return;

    if (Get_Flex_Info(f, AUTO_LOCKED))
        abrupt_panic (Error_Series_Auto_Frozen_Raw());

    if (Get_Flex_Info(f, HOLD))
        abrupt_panic (Error_Series_Held_Raw());

    if (Get_Flex_Info(f, FROZEN_SHALLOW))
        abrupt_panic (Error_Series_Frozen_Raw());

    assert(Not_Flex_Info(f, FROZEN_DEEP));  // implies FROZEN_SHALLOW

    assert(Get_Flex_Info(f, PROTECTED));
    abrupt_panic (Error_Series_Protected_Raw());
}




INLINE bool Is_Source_Frozen_Shallow(const Source* a)
  { return Get_Flex_Info(a, FROZEN_SHALLOW); }

INLINE bool Is_Source_Frozen_Deep(const Source* a) {
    if (Not_Flex_Info(a, FROZEN_DEEP))
        return false;

    assert(Get_Flex_Info(a, FROZEN_SHALLOW));  // implied by FROZEN_DEEP
    return true;
}

INLINE const Source* Freeze_Source_Deep(const Source* a) {
    Protect_Flex(
        a,
        0, // start protection at index 0
        PROT_DEEP | PROT_SET | PROT_FREEZE
    );
    Uncolor_Array(a);
    return a;
}

INLINE const Source* Freeze_Source_Shallow(const Source* a) {
    Set_Flex_Info(a, FROZEN_SHALLOW);
    return a;
}

#define Is_Array_Shallow_Read_Only(a) \
    Is_Flex_Read_Only(a)

#define BLAMELESS nullptr

#define Force_Value_Frozen_Deep(v) \
    Force_Value_Frozen_Core((v), true, BLAMELESS)

#define Force_Value_Frozen_Deep_Blame(v,blame) \
    Force_Value_Frozen_Core((v), true, blame)

#define Force_Value_Frozen_Shallow(v) \
    Force_Value_Frozen_Core((v), false, BLAMELESS)


#if NO_RUNTIME_CHECKS
    #define Known_Mutable(v) v
#else
    INLINE const Value* Known_Mutable(const Value* c) {
        assert(Cell_Payload_1_Needs_Mark(c));
        const Flex* f = c_cast(Flex*, CELL_PAYLOAD_1(c));  // varlist, etc.
        assert(not Is_Flex_Read_Only(f));
        assert(Not_Cell_Flag(c, CONST));
        return c;
    }
#endif

INLINE const Value* Ensure_Mutable(const Value* v) {
    assert(Cell_Payload_1_Needs_Mark(v));
    const Flex* f = c_cast(Flex*, CELL_PAYLOAD_1(v));  // varlist, etc.

    Panic_If_Read_Only_Flex(f);

    if (Not_Cell_Flag(v, CONST))
        return v;

    abrupt_panic (Error_Const_Value_Raw(v));
}
