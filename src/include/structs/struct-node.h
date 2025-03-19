//
//  File: %struct-node.h
//  Summary: "Node structure definitions preceding %tmp-internals.h"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// many datatypes is a `HeaderUnion` union.  Using byte-order-sensitive
// macros like FLAG_LEFT_BIT(), the layout of this header is chosen in such a
// way that not only can Cell pointers be distinguished from Stub pointers,
// but these can be discerned from a valid UTF-8 string just by looking at the
// first byte.  That's a safe C operation since reading a `char*` is not
// subject to "strict aliasing" requirements.
//
// On a semi-superficial level, this permits a kind of dynamic polymorphism,
// such as that used by panic():
//
//     Cell* cell = ...;
//     panic (cell);  // can tell this is a Cell
//
//     Stub* stub = ...;
//     panic (stub)  // can tell this is a Stub (Flex, String, Array, Binary)
//
//     panic ("Ḧéllŏ");  // can tell this is UTF-8 data (not Stub or Cell)
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
// called a "Node".
//


//=//// NODE_FLAG_NODE (leftmost bit) /////////////////////////////////////=//
//
// For the sake of simplicity, the leftmost bit in a node is always one.  This
// is because every UTF-8 string starting with a bit pattern 10xxxxxxx in the
// first byte is invalid.
//
#define NODE_FLAG_NODE \
    FLAG_LEFT_BIT(0)
#define NODE_BYTEMASK_0x80_NODE  0x80


//=//// NODE_FLAG_UNREADABLE (second-leftmost bit) ////////////////////////=//
//
// The second-leftmost bit will be 0 for most Cells and Stubs in the system.
// This gives the most freedom to set the other node bits independently, since
// the bit pattern 10xxxxxx, is always an invalid leading byte in UTF-8.
//
// But when the bit is set and the pattern is 11xxxxxx, it's still possible
// to cleverly use subsets of the remaining bit patterns for Cells and Stubs
// and avoid conflating with legal UTF-8 states.  See NODE_FLAG_CELL for
// how this is done.
//
// Additional non-UTF-8 states that have NODE_FLAG_UNREADABLE set are
// END_SIGNAL_BYTE, which uses 11000000, and FREE_POOLUNIT_BYTE, which uses
// 110000001... which are the illegal UTF-8 bytes 192 and 193.
//
#define NODE_FLAG_UNREADABLE \
    FLAG_LEFT_BIT(1)
#define NODE_BYTEMASK_0x40_UNREADABLE  0x40


//=//// NODE_FLAG_GC_ONE / NODE_FLAG_GC_TWO (third/fourth-leftmost bit) ////=//
//
// Both Cell* and Stub* have two bits in their NODE_BYTE which can be called
// out for attention from the GC.  Though these bits are scarce, sacrificing
// them means not needing to do a switch() on the TYPE_TYPE of the cell to
// know how to mark them.
//
// The third potentially-node-holding slot in a cell ("Extra") is deemed
// whether to be marked or not by the ordering in the %types.r file.  So no
// bit is needed for that.
//
#define NODE_FLAG_GC_ONE \
    FLAG_LEFT_BIT(2)
#define NODE_BYTEMASK_0x20_GC_ONE  0x20

#define NODE_FLAG_GC_TWO \
    FLAG_LEFT_BIT(3)
#define NODE_BYTEMASK_0x10_GC_TWO  0x10


//=//// NODE_FLAG_CELL (fifth-leftmost bit) //////////////////////////////=//
//
// If this bit is set in the header, it indicates the slot the header is for
// is `sizeof(Cell)`.
//
// In checked builds, it provides some safety for all cell writing routines.
// In the release build, it distinguishes "Pairing" Nodes (holders for two
// cells in the same Pool as ordinary Stubs) from an ordinary Flex Stub.
// Stubs have the cell bit clear, while Pairings in the STUB_POOL have it set.
//
// The position chosen is not random.  It is picked as the 5th bit from the
// left so that unreadable nodes can have the pattern:
//
//    11111xxx: Flags: NODE | UNREADABLE | GC_ONE | GC_TWO | CELL | ...
//
// This pattern is for an Not_Cell_Readable() cell, and so long as we set the
// GC_ONE and GC_TWO flags we can still have free choices of `xxx` (e.g.
// arbitrary ROOT, MANAGED, and MARKED flags), while Detect_Rebol_Pointer()
// can be certain it's a cell and not UTF-8.
//
#define NODE_FLAG_CELL \
    FLAG_LEFT_BIT(4)
#define NODE_BYTEMASK_0x08_CELL  0x08


//=//// NODE_FLAG_MANAGED (sixth-leftmost bit) ////////////////////////////=//
//
// The GC-managed bit is used on a Stub to indicate that its lifetime is
// controlled by the garbage collector.  If this bit is not set, then it is
// still manually managed...and during the GC's sweeping phase the simple fact
// that it isn't NODE_FLAG_MARKED won't be enough to consider it for freeing.
//
// See Manage_Flex() for details on the lifecycle of a Flex (how it starts
// out manually managed, and then must either become managed or be freed
// before the evaluation that created it ends).
//
// Note that all scanned code is expected to be managed by the GC (because
// walking the tree after constructing it to add the "manage GC" bit would be
// expensive, and we don't load source and free it manually anyway...how
// would you know after running it that pointers in it weren't stored?)
//
#define NODE_FLAG_MANAGED \
    FLAG_LEFT_BIT(5)
#define NODE_BYTEMASK_0x04_MANAGED  0x04


//=//// NODE_FLAG_ROOT (seventh-leftmost bit) /////////////////////////////=//
//
// Means the node should be treated as a root for GC purposes.  If the node
// also has NODE_FLAG_CELL, that means the cell must live in a "pairing"
// Stub-sized structure for two cells.
//
// This flag is masked out by CELL_MASK_COPY, so that when values are moved
// into or out of API handle cells the flag is left untouched.
//
#define NODE_FLAG_ROOT \
    FLAG_LEFT_BIT(6)
#define NODE_BYTEMASK_0x02_ROOT  0x02


//=//// NODE_FLAG_MARKED (eighth-leftmost bit) ////////////////////////////=//
//
// On Stub Nodes, this flag is used by the mark-and-sweep of the garbage
// collector, and should not be referenced outside of %m-gc.c.
//
// 1. THE CHOICE OF BEING THE LAST BIT IS NOT RANDOM.  This means that decayed
//    Stub states can be represented as 11000000 and 11000001, where you have
//    just NODE_FLAG_NODE and NODE_FLAG_STUB plus whether the stub has been
//    marked or not, and these are illegal UTF-8.
//
// 2. See `FLEX_INFO_BLACK` for a generic bit available to other routines
//    that wish to have an arbitrary marker on a Flex (for things like
//    recursion avoidance in algorithms).
//
// 3. Because "Pairings" can wind up marking what looks like a Cell but is
//    in the STUB_POOL, it's a bit dangerous to try exploiting this bit on a
//    generic Cell.  If one is *certain* that a value is not "paired" (e.g. in
//    a function arglist, or array slot), it may be used for other things).
//
#define NODE_FLAG_MARKED \
    FLAG_LEFT_BIT(7)
#define NODE_BYTEMASK_0x01_MARKED  0x01

#define DECAYED_NON_CANON_BYTE      0xC0  // 11000000: illegal UTF-8 [1]
#define DECAYED_CANON_BYTE          0xC1  // 11000001: illegal UTF-8 [1]


// All the illegal UTF-8 bit patterns are in use for some purpose in the
// Cell and Stub space except for these 3 bytes:
//
//        0xF5 (11110101), 0xF6 (11110110), 0xF7 (11110111)
//
// If these were interpreted as flags, it's a stub (no NODE_FLAG_CELL) with:
//
//    11110xxx: Flags: NODE | UNREADABLE | GC_ONE | GC_TWO
//
// 0xF7 is used for END_SIGNAL_BYTE
// 0xF6 is used for FREE_POOLUNIT_BYTE
// 0xF5 is NODE_BYTE_WILD that is used for Bounce, and other purposes
//
// 1. At time of writing, the END_SIGNAL_BYTE must always be followed by a
//    zero byte.  It's easy to do with C strings(*see rebEND definition*).
//    Not strictly necessary--one byte suffices--but it's a good sanity check.

#define END_SIGNAL_BYTE  0xF7  // followed by a zero byte [1]
STATIC_ASSERT(not (END_SIGNAL_BYTE & NODE_BYTEMASK_0x08_CELL));

#define FREE_POOLUNIT_BYTE  0xF6

#define NODE_BYTE_WILD  0xF5  // not NODE_FLAG_CELL, use for whatever purposes


//=//// Node Base Type: Empty Base Class (or minimal C struct) ////////////=//
//
// If we were willing to commit to building with a C++ compiler, we'd want to
// make the NodeStruct contain the common `header` bits that Stub and Cell
// would share.  But since we're not, we instead make a less invasive empty
// base class, that doesn't disrupt the memory layout of derived classes due
// to the "Empty Base Class Optimization":
//
//   https://en.cppreference.com/w/cpp/language/ebo
//
// In plain C builds, there's no such thing as "base classes".  So the only
// way to make a function that can accept either a Flex* or a Value* without
// knowing which is to use a `void*`.  So the Node is defined as `void`, and
// the C++ build is trusted to do the more strict type checking.
//
// Note: At one time there was an attempt to make Context/Action/Map derive
// from Node, but not Flex.  Facilitating that through multiple inheritance
// foils the Empty Base Class optimization, and creates other headaches.  So
// it was decided that so long as they are Flex, not Array, that's still
// abstract enough to block most casual misuses.
//
#if CPLUSPLUS_11
    struct RebolNodeStruct {};  // empty base for Stub, Flex, Cell, Level...
    typedef struct RebolNodeStruct Node;
#else
    struct RebolNodeStruct {  // Node is void*, but must define struct for API
        Byte first;
    };
    typedef void Node;  // couldn't pass Flex* to a RebolNodeStruct* in C
#endif


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
// easily, so sync with %struct-stub.h or %struct-cell.h if they do...and
// double check against FLAG_BIT_LEFT(xx) numbers if anything seems fishy.
//
// Note: Bitfields are notoriously underspecified, and there's no way to do
// `#if sizeof(struct StubHeaderPun) <= sizeof(uint32_t)`.  Hence
// the DEBUG_USE_BITFIELD_HEADER_PUNS flag should be set with caution.
//
#if DEBUG_USE_BITFIELD_HEADER_PUNS
    struct StubHeaderPun {
        int _07_marked:1;
        int _06_root:1;
        int _05_managed:1;
        int _04_cell_always_false:1;
        int _03_misc_needs_mark:1;
        int _02_link_needs_mark:1;
        int _01_unreadable:1;
        int _00_node_always_true:1;

        unsigned int _08to15_flavor:8;

        int _23_fixed_size:1;
        int _22_power_of_2:1;
        int _21_flag_21:1;
        int _20_flag_20:1;
        int _19_flag_19:1;
        int _18_black:1;
        int _17_dynamic:1;
        int _16_info_node_needs_mark:1;

        int _31_subclass:1;
        int _30_subclass:1;
        int _29_subclass:1;
        int _28_subclass:1;
        int _27_subclass:1;
        int _26_subclass:1;
        int _25_subclass:1;
        int _24_subclass:1;
    }__attribute__((packed));

    struct InfoHeaderPun {
        int _07_flag_07:1;
        int _06_frozen_shallow:1;
        int _05_hold:1;
        int _04_frozen_deep:1;
        int _03_protected:1;
        int _02_auto_locked:1;
        int _01_flag_01:1;
        int _00_node_always_false:1;

        unsigned int _08to15_used:8;

        unsigned int _16to31_symid_if_sym:8;
    }__attribute__((packed));

    struct CellHeaderPun {
        int _07_marked:1;
        int _06_root:1;
        int _05_managed:1;
        int _04_cell_always_true:1;
        int _03_dont_mark_node2:1;
        int _02_dont_mark_node1:1;
        int _01_unreadable:1;
        int _00_node_always_true:1;

        unsigned int _08to15_heart_byte:8;

        unsigned int _16to23_quote_byte:8;

        int _31_type_specific_b:1;
        int _30_type_specific_a:1;
        int _29_newline_before:1;
        int _28_note:1;
        int _27_protected:1;
        int _26_flag_26:1;
        int _25_flag_25:1;
        int _24_const:1;
    }__attribute__((packed));
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE HEADER a.k.a `union HeaderUnion` (for Cell and Stub uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// pointer-sized integer...while still being able to control the underlying
// order of those bits in memory.  See FLAG_LEFT_BIT() in %c-enhanced.h for
// how this is achieved.
//
// This control allows the leftmost byte of a Rebol header (the one you'd
// get by casting Value* to an unsigned char*) to always start with the bit
// pattern `10`.  This pattern corresponds to what UTF-8 calls "continuation
// bytes", which may never legally start a UTF-8 string:
//
// https://en.wikipedia.org/wiki/UTF-8#Codepage_layout
//

union HeaderUnion {
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
        struct StubHeaderPun stub_pun;
        struct CellHeaderPun cell_pun;
        struct InfoHeaderPun info_pun;
    #endif
  #endif
};
