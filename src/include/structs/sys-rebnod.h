//
//  File: %sys-rebnod.h
//  Summary: {Definitions for the Rebol_Header-having "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// In order to implement several "tricks", the first pointer-size slots of
// many datatypes is a `Reb_Header` structure.  Using byte-order-sensitive
// macros like FLAG_LEFT_BIT(), the layout of this header is chosen in such a
// way that not only can Rebol value pointers (REBVAL*) be distinguished from
// Rebol series pointers (REBSER*), but these can be discerned from a valid
// UTF-8 string just by looking at the first byte.  That's a safe C operation
// since reading a `char*` is not subject to "strict aliasing" requirements.
//
// On a semi-superficial level, this permits a kind of dynamic polymorphism,
// such as that used by panic():
//
//     REBVAL *value = ...;
//     panic (value);  // can tell this is a value
//
//     REBSER *series = ...;
//     panic (series)  // can tell this is a series
//
//     panic ("Ḧéllŏ");  // can tell this is UTF-8 data (not series or value)
//
// An even more compelling case is the usage through the API, so variadic
// combinations of strings and values can be intermixed, as in:
//
//     rebElide("poke", block, "1", value)
//
// Internally, the ability to discern these types helps certain structures or
// arrangements from having to find a place to store a kind of "flavor" bit
// for a stored pointer's type.  They can just check the first byte instead.
//
// For lack of a better name, the generic type covering the superclass is
// called a "Rebol Node".
//


#if !defined(CPLUSPLUS_11)
    //
    // In plain C builds, there's no such thing as "base classes".  So the
    // only way to make a function that can accept either a REBSER* or a
    // REBVAL* without knowing which is to use a `void*`.  So the REBNOD is
    // defined as `void`, and the C++ build is trusted to do the more strict
    // type checking.
    //
    struct Reb_Node { REBYTE first; };  // REBNOD is void*, but define struct
    typedef void REBNOD;
#else
    // If we were willing to commit to building with a C++ compiler, we'd
    // want to make the Reb_Node contain the common `header` bits that REBSER
    // and REBVAL would share.  But since we're not, we instead make a less
    // invasive empty base class, that doesn't disrupt the memory layout of
    // derived classes due to the "Empty Base Class Optimization":
    //
    // https://en.cppreference.com/w/cpp/language/ebo
    //
    // At one time there was an attempt to make REBCTX/REBACT/REBMAP derive
    // from REBNOD, but not REBSER.  Facilitating that through multiple
    // inheritance foils the Empty Base Class optimization, and creates other
    // headaches.  So it was decided that so long as they are REBSER and not
    // REBARR, that's still abstract enough to block most casual misuses.
    //
    struct Reb_Node {};  // used as empty base class for REBSER, REBVAL, REBFRM
    typedef struct Reb_Node REBNOD;
#endif


//=////////////////////////////////////////////////////////////////////=///=//
//
// BYTE-ORDER SENSITIVE BIT FLAGS & MASKING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// To facilitate the tricks of the Rebol Node, these macros are purposefully
// arranging bit flags with respect to the "leftmost" and "rightmost" bytes of
// the underlying platform, when encoding them into an unsigned integer the
// size of a platform pointer:
//
//     uintptr_t flags = FLAG_LEFT_BIT(0);
//     unsigned char *ch = (unsigned char*)&flags;
//
// In the code above, the leftmost bit of the flags has been set to 1, giving
// `ch == 128` on all supported platforms.
//
// These can form *compile-time constants*, which can be singly assigned to
// a uintptr_t in one instruction.  Quantities smaller than a byte can be
// mixed in on with bytes:
//
//    uintptr_t flags
//        = FLAG_LEFT_BIT(0) | FLAG_LEFT_BIT(1) | FLAG_SECOND_BYTE(13);
//
// They can be masked or shifted out efficiently:
//
//    unsigned int left = LEFT_N_BITS(flags, 3); // == 6 (binary `110`)
//    unsigned int right = SECOND_BYTE(flags); // == 13
//
// Other tools that might be tried with this all have downsides:
//
// * bitfields arranged in a `union` with integers have no layout guarantee
// * `#pragma pack` is not standard C98 or C99...nor is any #pragma
// * `char[4]` or `char[8]` targets don't usually assign in one instruction
//

#define PLATFORM_BITS \
    (sizeof(uintptr_t) * 8)

// !!! Originally the REBFLGS type was a `uint_fast32_t`.  However, there were
// several cases of the type being used with these macros...which only work
// with platform sized ints.  If the callsites that use REBFLGS are all
// changed to not hold things like NODE_FLAG_XXX then this could be changed
// back, but until then it has to be uintptr_t (which is likely defined to be
// the same as uint_fast32_t on most platforms anyway).
//
typedef uintptr_t REBFLGS;

#if defined(ENDIAN_BIG) // Byte w/most significant bit first

    #define FLAG_LEFT_BIT(n) \
        ((uintptr_t)1 << (PLATFORM_BITS - (n) - 1)) // 63,62,61..or..32,31,30

    #define FLAG_FIRST_BYTE(b) \
        ((uintptr_t)(b) << (24 + (PLATFORM_BITS - 8)))

    #define FLAG_SECOND_BYTE(b) \
        ((uintptr_t)(b) << (16 + (PLATFORM_BITS - 8)))

    #define FLAG_THIRD_BYTE(b) \
        ((uintptr_t)(b) << (8 + (PLATFORM_BITS - 32)))

    #define FLAG_FOURTH_BYTE(b) \
        ((uintptr_t)(b) << (0 + (PLATFORM_BITS - 32)))

#elif defined(ENDIAN_LITTLE) // Byte w/least significant bit first (e.g. x86)

    #define FLAG_LEFT_BIT(n) \
        ((uintptr_t)1 << (7 + ((n) / 8) * 8 - (n) % 8)) // 7,6,..0|15,14..8|..

    #define FLAG_FIRST_BYTE(b)      ((uintptr_t)(b))
    #define FLAG_SECOND_BYTE(b)     ((uintptr_t)(b) << 8)
    #define FLAG_THIRD_BYTE(b)      ((uintptr_t)(b) << 16)
    #define FLAG_FOURTH_BYTE(b)     ((uintptr_t)(b) << 24)
#else
    // !!! There are macro hacks which can actually make reasonable guesses
    // at endianness, and should possibly be used in the config if nothing is
    // specified explicitly.
    //
    // http://stackoverflow.com/a/2100549/211160
    //
    #error "ENDIAN_BIG or ENDIAN_LITTLE must be defined"
#endif

// `unsigned char` is used below, as opposed to `uint8_t`, to coherently
// access the bytes despite being written via a `uintptr_t`, due to the strict
// aliasing exemption for character types (some say uint8_t should count...)
//
// mutable and immutable variations are needed, because sometimes the flags
// are const (e.g. of a header in a `const REBVAL*`)

#define FIRST_BYTE(flags)       ((const unsigned char*)&(flags))[0]
#define SECOND_BYTE(flags)      ((const unsigned char*)&(flags))[1]
#define THIRD_BYTE(flags)       ((const unsigned char*)&(flags))[2]
#define FOURTH_BYTE(flags)      ((const unsigned char*)&(flags))[3]

#define mutable_FIRST_BYTE(flags)       ((unsigned char*)&(flags))[0]
#define mutable_SECOND_BYTE(flags)      ((unsigned char*)&(flags))[1]
#define mutable_THIRD_BYTE(flags)       ((unsigned char*)&(flags))[2]
#define mutable_FOURTH_BYTE(flags)      ((unsigned char*)&(flags))[3]

// There might not seem to be a good reason to keep the uint16_t variant in
// any particular order.  But if you cast a uintptr_t (or otherwise) to byte
// and then try to read it back as a uint16_t, compilers see through the
// cast and complain about strict aliasing.  Building it out of bytes makes
// these generic (so they work with uint_fast32_t, or uintptr_t, etc.) and
// as long as there has to be an order, might as well be platform-independent.

inline static uint16_t FIRST_UINT16_helper(const unsigned char *flags)
  { return ((uint16_t)flags[0] << 8) | flags[1]; }

inline static uint16_t SECOND_UINT16_helper(const unsigned char *flags)
  { return ((uint16_t)flags[2] << 8) | flags[3]; }

#define FIRST_UINT16(flags) \
    FIRST_UINT16_helper((const unsigned char*)&flags)

#define SECOND_UINT16(flags) \
    SECOND_UINT16_helper((const unsigned char*)&flags)

inline static void SET_FIRST_UINT16_helper(unsigned char *flags, uint16_t u) {
    flags[0] = u / 256;
    flags[1] = u % 256;
}

inline static void SET_SECOND_UINT16_helper(unsigned char *flags, uint16_t u) {
    flags[2] = u / 256;
    flags[3] = u % 256;
}

#define SET_FIRST_UINT16(flags,u) \
    SET_FIRST_UINT16_helper((unsigned char*)&(flags), (u))

#define SET_SECOND_UINT16(flags,u) \
    SET_SECOND_UINT16_helper((unsigned char*)&(flags), (u))

inline static uintptr_t FLAG_FIRST_UINT16(uint16_t u)
  { return FLAG_FIRST_BYTE(u / 256) | FLAG_SECOND_BYTE(u % 256); }

inline static uintptr_t FLAG_SECOND_UINT16(uint16_t u)
  { return FLAG_THIRD_BYTE(u / 256) | FLAG_FOURTH_BYTE(u % 256); }


// !!! SECOND_UINT32 should be defined on 64-bit platforms, for any enhanced
// features that might be taken advantage of when that storage is available.


//=////////////////////////////////////////////////////////////////////=///=//
//
// TYPE-PUNNING BITFIELD DEBUG HELPERS (GCC LITTLE-ENDIAN ONLY)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Disengaged union states are used to give alternative debug views into
// the header bits.  This is called type punning, and it can't be relied
// on (endianness, undefined behavior)--purely for GDB watchlists!
//
// https://en.wikipedia.org/wiki/Type_punning
//
// Because the watchlist often orders the flags alphabetically, name them so
// it will sort them in order.  Note that these flags can get out of date
// easily, so sync with %rebser.h or %rebval.h if they do...and double check
// against the FLAG_BIT_LEFT(xx) numbers if anything seems fishy.
//
// Note: Bitfields are notoriously underspecified, and there's no way to do
// `#if sizeof(struct Reb_Series_Header_Pun) <= sizeof(uint32_t)`.  Hence
// the DEBUG_USE_BITFIELD_HEADER_PUNS flag should be set with caution.
//
#if DEBUG_USE_BITFIELD_HEADER_PUNS
    struct Reb_Series_Header_Pun {
        int _07_cell_always_false:1;
        int _06_root:1;
        int _05_misc_needs_mark:1;
        int _04_link_needs_mark:1;
        int _03_marked:1;
        int _02_managed:1;
        int _01_free:1;
        int _00_node_always_true:1;

        int _15_flag_15:1;
        int _14_black:1;
        int _13_flag_13:1;
        int _12_flag_12:1;
        int _11_dynamic:1;
        int _10_power_of_two:1;
        int _09_fixed_size:1;
        int _08_inaccessible:1;

        unsigned int _16to23_flavor:8;

        int _31_subclass:1;
        int _30_subclass:1;
        int _29_subclass:1;
        int _28_subclass:1;
        int _27_subclass:1;
        int _26_subclass:1;
        int _25_subclass:1;
        int _24_subclass:1;
    }__attribute__((packed));

    struct Reb_Info_Header_Pun {
        int _07_cell_always_false:1;
        int _06_frozen_shallow:1;
        int _05_hold:1;
        int _04_frozen_deep:1;
        int _03_protected:1;
        int _02_auto_locked:1;
        int _01_free_always_false:1;
        int _00_node_always_true:1;

        unsigned int _08to15_used:8;

        unsigned int _16to31_symid_if_sym:8;
    }__attribute__((packed));

    struct Reb_Value_Header_Pun {
        int _07_cell_always_true:1;
        int _06_root:1;
        int _05_second_needs_mark:1;
        int _04_first_needs_mark:1;
        int _03_marked:1;
        int _02_managed:1;
        int _01_free:1;
        int _00_node_always_true:1;

        unsigned int _08to15_kind3q:8;

        unsigned int _16to23_heart:8;

        int _31_explicitly_mutable:1;
        int _30_const:1;
        int _29_newline_before:1;
        int _28_note:1;
        int _27_unevaluated:1;
        int _26_cell:1;
        int _25_cell:1;
        int _24_protected:1;
    }__attribute__((packed));
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE HEADER a.k.a `union Reb_Header` (for REBVAL and REBSER uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// pointer-sized integer...while still being able to control the underlying
// ordering of those bits in memory.  See FLAG_LEFT_BIT() in %reb-c.h for how
// this is achieved.
//
// This control allows the leftmost byte of a Rebol header (the one you'd
// get by casting REBVAL* to an unsigned char*) to always start with the bit
// pattern `10`.  This pattern corresponds to what UTF-8 calls "continuation
// bytes", which may never legally start a UTF-8 string:
//
// https://en.wikipedia.org/wiki/UTF-8#Codepage_layout
//

union Reb_Header {
    //
    // unsigned integer that's the size of a platform pointer (e.g. 32-bits on
    // 32 bit platforms and 64-bits on 64 bit machines).  See macros like
    // FLAG_LEFT_BIT() for how these bits are laid out in a special way.
    //
    // !!! Future application of the 32 unused header bits on 64-bit machines
    // might add some kind of optimization or instrumentation.
    //
    // !!! uintptr_t may not be the fastest type for operating on 32-bits.
    // But using a `uint_fast32_t` would prohibit 64-bit platforms from
    // exploiting the additional bit space (due to strict aliasing).
    //
    uintptr_t bits;

    // !!! For some reason, at least on 64-bit Ubuntu, TCC will bloat the
    // header structure to be 16 bytes instead of 8 if you put a 4 byte char
    // array in the header.  There's probably a workaround, but for now skip
    // this debugging pun if __TINYC__ is defined.
    //
  #if DEBUG_USE_UNION_PUNS && !defined(__TINYC__)
    unsigned char bytes_pun[4];
    char chars_pun[4];

    #if DEBUG_USE_BITFIELD_HEADER_PUNS
        struct Reb_Series_Header_Pun series_pun;
        struct Reb_Value_Header_Pun value_pun;
        struct Reb_Info_Header_Pun info_pun;
    #endif
  #endif
};


//=//// NODE_FLAG_NODE (leftmost bit) /////////////////////////////////////=//
//
// For the sake of simplicity, the leftmost bit in a node is always one.  This
// is because every UTF-8 string starting with a bit pattern 10xxxxxxx in the
// first byte is invalid.
//
#define NODE_FLAG_NODE \
    FLAG_LEFT_BIT(0)
#define NODE_BYTEMASK_0x80_NODE 0x80


//=//// NODE_FLAG_FREE (second-leftmost bit) //////////////////////////////=//
//
// The second-leftmost bit will be 0 for all Reb_Header in the system that
// are "valid".  This completes the plan of making sure all REBVAL and REBSER
// that are usable will start with the bit pattern 10xxxxxx, which always
// indicates an invalid leading byte in UTF-8.
//
// The exception are freed nodes, but they use 11000000 and 110000001 for
// freed REBSER nodes and "freed" value nodes (trash).  These are the bytes
// 192 and 193, which are specifically illegal in any UTF8 sequence.  So
// even these cases may be safely distinguished from strings.  See the
// NODE_FLAG_CELL for why it is chosen to be that 8th bit.
//
#define NODE_FLAG_FREE \
    FLAG_LEFT_BIT(1)
#define NODE_BYTEMASK_0x40_FREE 0x40


//=//// NODE_FLAG_MANAGED (third-leftmost bit) ////////////////////////////=//
//
// The GC-managed bit is used on series to indicate that its lifetime is
// controlled by the garbage collector.  If this bit is not set, then it is
// still manually managed...and during the GC's sweeping phase the simple fact
// that it isn't NODE_FLAG_MARKED won't be enough to consider it for freeing.
//
// See Manage_Series() for details on the lifecycle of a series (how it starts
// out manually managed, and then must either become managed or be freed
// before the evaluation that created it ends).
//
// Note that all scanned code is expected to be managed by the GC (because
// walking the tree after constructing it to add the "manage GC" bit would be
// expensive, and we don't load source and free it manually anyway...how
// would you know after running it that pointers inside weren't stored?)
//
#define NODE_FLAG_MANAGED \
    FLAG_LEFT_BIT(2)
#define NODE_BYTEMASK_0x20_MANAGED 0x20


//=//// NODE_FLAG_MARKED (fourth-leftmost bit) ////////////////////////////=//
//
// On series nodes, this flag is used by the mark-and-sweep of the garbage
// collector, and should not be referenced outside of %m-gc.c.
//
// See `SERIES_INFO_BLACK` for a generic bit available to other routines
// that wish to have an arbitrary marker on series (for things like
// recursion avoidance in algorithms).
//
// Because "pairings" can wind up marking what looks like both a value cell
// and a series, it's a bit dangerous to try exploiting this bit on a generic
// REBVAL.  If one is *certain* that a value is not "paired" (e.g. it's in
// a function arglist, or array slot), it may be used for other things.
//
#define NODE_FLAG_MARKED \
    FLAG_LEFT_BIT(3)
#define NODE_BYTEMASK_0x10_MARKED 0x10


//=//// NODE_FLAG_GC_ONE / NODE_FLAG_GC_TWO (fifth/sixth-leftmost bit) ////=//
//
// Both REBVAL* and REBSER* nodes have two slots in them which can be called
// out for attention from the GC.  Though these bits are scarce, sacrificing
// them means not needing to do a switch() on the REB_TYPE of the cell to
// know how to mark them.
//
// The third potentially-node-holding slot in a cell ("Extra") is deemed
// whether to be marked or not by the ordering in the %types.r file.  So no
// bit is needed for that.
//
#define NODE_FLAG_GC_ONE \
    FLAG_LEFT_BIT(4)
#define NODE_BYTEMASK_0x08_GC_ONE 0x08

#define NODE_FLAG_GC_TWO \
    FLAG_LEFT_BIT(5)
#define NODE_BYTEMASK_0x04_GC_TWO 0x04


//=//// NODE_FLAG_ROOT (seventh-leftmost bit) /////////////////////////////=//
//
// Means the node should be treated as a root for GC purposes.  If the node
// also has NODE_FLAG_CELL, that means the cell must live in a "pairing"
// REBSER-sized structure for two cells.
//
// This flag is masked out by CELL_MASK_COPIED, so that when values are moved
// into or out of API handle cells the flag is left untouched.
//
#define NODE_FLAG_ROOT \
    FLAG_LEFT_BIT(6)
#define NODE_BYTEMASK_0x02_ROOT 0x02


//=//// NODE_FLAG_CELL (eighth-leftmost bit) //////////////////////////////=//
//
// If this bit is set in the header, it indicates the slot the header is for
// is `sizeof(REBVAL)`.
//
// In the debug build, it provides some safety for all value writing routines.
// In the release build, it distinguishes "pairing" nodes (holders for two
// REBVALs in the same pool as ordinary REBSERs) from an ordinary REBSER node.
// Plain REBSERs have the cell mask clear, while pairing values have it set.
//
// The position chosen is not random.  It is picked as the 8th bit from the
// left so that freed nodes can still express a distinction between
// being a cell and not, due to 11000000 (192) and 11000001 (193) are both
// invalid UTF-8 bytes, hence these two free states are distinguishable from
// a leading byte of a string.
//
#define NODE_FLAG_CELL \
    FLAG_LEFT_BIT(7)
#define NODE_BYTEMASK_0x01_CELL 0x01


// There are two special invalid bytes in UTF8 which have a leading "110"
// bit pattern, which are freed nodes.  These two patterns are for freed bytes
// and "freed cells"...though NODE_FLAG_FREE is not generally used on purpose
// (mostly happens if reading uninitialized memory)
//
#define FREED_SERIES_BYTE 192
#define FREED_CELL_BYTE 193
