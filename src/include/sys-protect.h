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


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES COLORING API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha re-used the same marking flag from the GC in order to do various
// other bit-twiddling tasks when the GC wasn't running.  This is an
// unusually dangerous thing to be doing...because leaving a stray mark on
// during some other traversal could lead the GC to think it had marked
// things reachable from that series when it had not--thus freeing something
// that was still in use.
//
// While leaving a stray mark on is a bug either way, GC bugs are particularly
// hard to track down.  So one doesn't want to risk them if not absolutely
// necessary.  Not to mention that sharing state with the GC that you can
// only use when it's not running gets in the way of things like background
// garbage collection, etc.
//
// Ren-C keeps the term "mark" for the GC, since that's standard nomenclature.
// A lot of basic words are taken other places for other things (tags, flags)
// so this just goes with a series "color" of black or white, with white as
// the default.  The debug build keeps a count of how many black series there
// are and asserts it's 0 by the time each evaluation ends, to ensure balance.
//

INLINE bool Is_Series_Black(const Series* s) {
    return Get_Series_Flag(s, BLACK);
}

INLINE bool Is_Series_White(const Series* s) {
    return Not_Series_Flag(s, BLACK);
}

INLINE void Flip_Series_To_Black(const Series* s) {
    assert(Not_Series_Flag(s, BLACK));
    Set_Series_Flag(s, BLACK);
  #if !defined(NDEBUG)
    g_mem.num_black_series += 1;
  #endif
}

INLINE void Flip_Series_To_White(const Series* s) {
    assert(Get_Series_Flag(s, BLACK));
    Clear_Series_Flag(s, BLACK);
  #if !defined(NDEBUG)
    g_mem.num_black_series -= 1;
  #endif
}

//
// Freezing and Locking
//

INLINE void Freeze_Series(const Series* s) {  // there is no unfreeze
    assert(not Is_Series_Array(s)); // use Deep_Freeze_Array

    // We set the FROZEN_DEEP flag even though there is no structural depth
    // here, so that the generic test for deep-frozenness can be faster.
    //
    Set_Series_Info(s, FROZEN_SHALLOW);
    Set_Series_Info(s, FROZEN_DEEP);
}

INLINE bool Is_Series_Frozen(const Series* s) {
    assert(not Is_Series_Array(s));  // use Is_Array_Deeply_Frozen
    if (Not_Series_Info(s, FROZEN_SHALLOW))
        return false;
    assert(Get_Series_Info(s, FROZEN_DEEP));  // true on frozen non-arrays
    return true;
}

INLINE bool Is_Series_Read_Only(const Series* s) {  // may be temporary
    return 0 != (SERIES_INFO(s) &
        (SERIES_INFO_HOLD | SERIES_INFO_PROTECTED
        | SERIES_INFO_FROZEN_SHALLOW | SERIES_INFO_FROZEN_DEEP)
    );
}


// Gives the appropriate kind of error message for the reason the series is
// read only (frozen, running, protected, locked to be a map key...)
//
// !!! Should probably report if more than one form of locking is in effect,
// but if only one error is to be reported then this is probably the right
// priority ordering.
//

INLINE void Fail_If_Read_Only_Series(const Series* s) {
    if (not Is_Series_Read_Only(s))
        return;

    if (Get_Series_Info(s, AUTO_LOCKED))
        fail (Error_Series_Auto_Locked_Raw());

    if (Get_Series_Info(s, HOLD))
        fail (Error_Series_Held_Raw());

    if (Get_Series_Info(s, FROZEN_SHALLOW))
        fail (Error_Series_Frozen_Raw());

    assert(Not_Series_Info(s, FROZEN_DEEP));  // implies FROZEN_SHALLOW

    assert(Get_Series_Info(s, PROTECTED));
    fail (Error_Series_Protected_Raw());
}




INLINE bool Is_Array_Frozen_Shallow(const Array* a)
  { return Get_Series_Info(a, FROZEN_SHALLOW); }

INLINE bool Is_Array_Frozen_Deep(const Array* a) {
    if (Not_Series_Info(a, FROZEN_DEEP))
        return false;

    assert(Get_Series_Info(a, FROZEN_SHALLOW));  // implied by FROZEN_DEEP
    return true;
}

INLINE const Array* Freeze_Array_Deep(const Array* a) {
    Protect_Series(
        a,
        0, // start protection at index 0
        PROT_DEEP | PROT_SET | PROT_FREEZE
    );
    Uncolor_Array(a);
    return a;
}

INLINE const Array* Freeze_Array_Shallow(const Array* a) {
    Set_Series_Info(a, FROZEN_SHALLOW);
    return a;
}

#define Is_Array_Shallow_Read_Only(a) \
    Is_Series_Read_Only(a)

#define Force_Value_Frozen_Deep(v) \
    Force_Value_Frozen_Core((v), true, EMPTY_ARRAY)  // auto-locked

#define Force_Value_Frozen_Deep_Blame(v,blame) \
    Force_Value_Frozen_Core((v), true, blame)

#define Force_Value_Frozen_Shallow(v) \
    Force_Value_Frozen_Core((v), false, EMPTY_ARRAY)  // auto-locked


#if defined(NDEBUG)
    #define Known_Mutable(v) v
#else
    INLINE const Cell* Known_Mutable(const Cell* v) {
        assert(Get_Cell_Flag(v, FIRST_IS_NODE));
        const Series* s = c_cast(Series*, Cell_Node1(v));  // varlist, etc.
        assert(not Is_Series_Read_Only(s));
        assert(Not_Cell_Flag(v, CONST));
        return v;
    }
#endif

INLINE const Cell* Ensure_Mutable(const Cell* v) {
    assert(Get_Cell_Flag(v, FIRST_IS_NODE));
    const Series* s = c_cast(Series*, Cell_Node1(v));  // varlist, etc.

    Fail_If_Read_Only_Series(s);

    if (Not_Cell_Flag(v, CONST))
        return v;

    fail (Error_Const_Value_Raw(v));
}


// (Used by DO and EVALUATE)
//
// If `source` is not const, tweak it to be explicitly mutable--because
// otherwise, it would wind up inheriting the FEED_MASK_CONST of our
// currently executing level.  That's no good for `repeat 2 [do block]`,
// because we want whatever constness is on block...
//
// (Note we *can't* tweak values that are Cell in source.  So we either
// bias to having to do this or Do_XXX() versions explode into passing
// mutability parameters all over the place.  This is better.)
//
INLINE void Tweak_Non_Const_To_Explicitly_Mutable(Value* source) {
    if (Not_Cell_Flag(source, CONST))
        Set_Cell_Flag(source, EXPLICITLY_MUTABLE);
}
