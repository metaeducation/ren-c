//
//  File: %sys-protect.h
//  Summary: "System Const and Protection Functions"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2018 Ren-C Open Source Contributors
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
// introduces a new form of read-only-ness that is not a bit on series, but
// rather bits on values.  This means that a value can be a read-only view of
// a series that is otherwise mutable.
//
// !!! Checking for read access was a somewhat half-baked feature in R3-Alpha,
// as heeding the protection bit had to be checked explicitly.  Many places in
// the code did not do the check.  While several bugs of that nature have
// been replaced in an ad-hoc fashion, a better solution would involve using
// C's `const` feature to locate points that needed to promote series access
// to be mutable, so it could be checked at compile-time.
//


// Flags used for Protect functions
//
enum {
    PROT_SET = 1 << 0,
    PROT_DEEP = 1 << 1,
    PROT_HIDE = 1 << 2,
    PROT_WORD = 1 << 3,
    PROT_FREEZE = 1 << 4
};

inline static bool Is_Array_Frozen_Shallow(Array(const*) a)
  { return Get_Series_Info(a, FROZEN_SHALLOW); }

inline static bool Is_Array_Frozen_Deep(Array(const*) a) {
    if (Not_Series_Info(a, FROZEN_DEEP))
        return false;

    assert(Get_Series_Info(a, FROZEN_SHALLOW));  // implied by FROZEN_DEEP
    return true;
}

inline static Array(*) Freeze_Array_Deep(Array(*) a) {
    Protect_Series(
        a,
        0, // start protection at index 0
        PROT_DEEP | PROT_SET | PROT_FREEZE
    );
    Uncolor_Array(a);
    return a;
}

inline static Array(*) Freeze_Array_Shallow(Array(*) a) {
    Set_Series_Info(a, FROZEN_SHALLOW);
    return a;
}

#define Is_Array_Shallow_Read_Only(a) \
    Is_Series_Read_Only(SER(a))

#define Force_Value_Frozen_Deep(v) \
    Force_Value_Frozen_Core((v), true, EMPTY_ARRAY)  // auto-locked

#define Force_Value_Frozen_Deep_Blame(v,blame) \
    Force_Value_Frozen_Core((v), true, blame)

#define Force_Value_Frozen_Shallow(v) \
    Force_Value_Frozen_Core((v), false, EMPTY_ARRAY)  // auto-locked
