//
//  File: %sys-bitset.h
//  Summary: "BITSET! Datatype Header"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2020 Ren-C Open Source Contributors
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
// R3-Alpha had a flawed concept of BITSET! as being a thin veneer over a
// BINARY!.  This made it impractical to use bitsets to represent large
// numbers in the set (putting the single number 1,000,000 would require
// a million bits), along with problems representing bitset negations,
// and operating on them with unions/etc.
// https://github.com/rebol/rebol-issues/issues/2371
//
// This is solved in Ren-C by using "Roaring Bitmaps":
// https://roaringbitmap.org/
//
// Roaring Bitmaps are pure C code for dealing with bitset manipulations, and
// available as an amalgamated single file for easy inclusion:
// https://github.com/lemire/CRoaringUnityBuild
//
// The tracking entity for roaring bitmaps is very close to what a series
// node is.  So since a series node would have to be created anyway for the
// GC to keep track of the bitset, this code renders the series node as a
// `roaring_bitmap_t` API structure each time it's going to be used.  This
// saves memory--but requires having a few strategic patches to the roaring
// source code.  For details, see:
//
//     bool roaring_realloc_array(roaring_array_t *ra, int32_t new_capacity);
//     void roaring_size_updated(roaring_array_t *ra);
//     void roaring_flags_updated(roaring_array_t *ra);
//
// These are hook functions which do not exist in the official roaring bitmap
// sources, but are crafted to connect back and synchronize the series node
// with changes being done to the temporary `roaring_bitmap_t` rendering of
// the series node.  In order to enable those hooks to reach back to the
// series, a the node itself is patched in to the `roaring_array_t` struct
// definition.  The changes are *relatively* uninvasive, allowing the lion's
// share of the roaring code to be left as-is.  (pun possibly intended)
//

#define ROARING_HOOK_ARRAY
#include "roaring/roaring.h"

#define MAX_BITSET 0x7fffffff  // !!! Roaring uses UINT64_C(0x100000000)

// Generate a struct for temporary use with the roaring_bitmap_xxx APIs from
// the REBSER where the container list is actually stored.  In the scheme of
// things this extraction is quite fast, so worth it to avoid the redundant
// allocation of an actual `roaring_bitmap_t` along with the REBSER node.
//
inline static roaring_bitmap_t *Roaring_From_Bitset(
    roaring_bitmap_t *out,
    const REBBIT *bits
){
    REBBIT *s = m_cast(REBBIT*, bits);  // (better const methods in stackless)
    roaring_array_t *ra = &out->high_low_container;
    ra->size = SER_USED(s);
    ra->allocation_size = SER_REST(s);
    ra->containers = cast(void**, SER_DATA(s));
    ra->keys = cast(uint16_t*, ra->containers + ra->allocation_size);
    ra->typecodes = cast(uint8_t*, ra->keys + ra->allocation_size);
    ra->flags = FOURTH_BYTE(s->leader);
    ra->hookdata = s;  // save so updates/reallocate hooks can get back to it
    return out;
}

#if defined(NDEBUG)
    #define ASSERT_BITSET(b)
#else
    inline static void Assert_Bitset_Core(const REBBIT *bits) {
        roaring_bitmap_t r;
        Roaring_From_Bitset(&r, bits);
        const roaring_array_t *ra = &r.high_low_container;
        assert(ra->size <= ra->allocation_size);
        int i;
        for (i = 0; i < ra->size; ++i)
            assert(ra->typecodes[i] <= 4);
    }
    #define ASSERT_BITSET(b) Assert_Bitset_Core(b)
#endif

inline static const REBBIT *VAL_BITSET(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_BITSET);
    return cast(REBBIT*, VAL_NODE1(v));
}


// This operation "borrows" the bitset; you must call sync_roaring_bitmap()
// to reflect any changes done by a `roaring_bitmap_xxx` routine.
//
#define VAL_BITSET_ENSURE_MUTABLE(v) \
    m_cast(REBBIT*, VAL_BITSET(ENSURE_MUTABLE(v)))

#define VAL_KNOWN_MUTABLE_BITSET(v) \
    m_cast(REBBIT*, VAL_BITSET(v))

inline static REBVAL *Init_Bitset(RELVAL *out, REBBIT *bits) {
    assert(SER_FLAVOR(bits) == FLAVOR_BITSET);
    RESET_CELL(out, REB_BITSET, CELL_FLAG_FIRST_IS_NODE);
    INIT_VAL_NODE1(out, Force_Series_Managed(bits));
    return SPECIFIC(out);
}

inline static void Negate_Bitset(REBBIT *bits) {
    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    // We don't actually flip the bitset here.  It happens in optimization,
    // when the new highest bit changes the logic.
    //
    bits->misc.inverted = not bits->misc.inverted;
}

inline static void Optimize_Bitset(REBBIT *bits) {
    roaring_bitmap_t r;
    Roaring_From_Bitset(&r, bits);

    roaring_bitmap_run_optimize(&r);
    roaring_bitmap_shrink_to_fit(&r);
}


// Mathematical set operations for UNION, INTERSECT, DIFFERENCE
enum {
    SOP_NONE = 0, // used by UNIQUE (other flags do not apply)
    SOP_FLAG_BOTH = 1 << 0, // combine and interate over both series
    SOP_FLAG_CHECK = 1 << 1, // check other series for value existence
    SOP_FLAG_INVERT = 1 << 2 // invert the result of the search
};
