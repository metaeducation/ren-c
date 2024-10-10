//
//  File: %sys-rebnod.h
//  Summary: {Definitions for the Rebol_Header-having "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In order to implement several "tricks", the first pointer-size slots of
// many datatypes is a `HeaderUnion` structure.  The bit layout of this header
// is chosen in such a way that not only can Rebol value pointers (Value*)
// be distinguished from Rebol series pointers (Flex*), but these can be
// discerned from a valid UTF-8 string just by looking at the first byte.
//
// On a semi-superficial level, this permits a kind of dynamic polymorphism,
// such as that used by panic():
//
//     Value* value = ...;
//     panic (value); // can tell this is a value
//
//     Flex* series = ...;
//     panic (series) // can tell this is a series
//
//     const char *utf8 = ...;
//     panic (utf8); // can tell this is UTF-8 data (not a series or value)
//
// But a more compelling case is the usage through the API, so variadic
// combinations of strings and values can be intermixed, as in:
//
//     rebValue("poke", series, "1", value)
//
// Internally, the ability to discern these types helps certain structures or
// arrangements from having to find a place to store a kind of "flavor" bit
// for a stored pointer's type.  They can just check the first byte instead.
//
// For lack of a better name, the generic type covering the superclass is
// called a "Rebol Node".
//


//=//// BYTE-ORDER SENSITIVE BIT FLAGS & MASKING //////////////////////////=//
//
// These macros are for purposefully arranging bit flags with respect to the
// "leftmost" and "rightmost" bytes of the underlying platform, when encoding
// them into an unsigned integer the size of a platform pointer:
//
//     uintptr_t flags = FLAG_LEFT_BIT(0);
//     unsigned char *ch = (unsigned char*)&flags;
//
// In the code above, the leftmost bit of the flags has been set to 1,
// resulting in `ch == 128` on all supported platforms.
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
//    unsigned int left = LEFT_N_BITS(&flags, 3); // == 6 (binary `110`)
//    unsigned int right = SECOND_BYTE(&flags); // == 13
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: It is simpler to not worry about the underlying bytes and just use
// ordinary bit masking.  But this is used for an important feature (the
// discernment of a `void*` to a cell from that of a valid UTF-8 string).
// Other tools that might be tried with this all have downsides:
//
// * bitfields arranged in a `union` with integers have no layout guarantee
// * `#pragma pack` is not standard C98 or C99...nor is any #pragma
// * `char[4]` or `char[8]` targets don't usually assign in one instruction
//

#define PLATFORM_BITS \
    (sizeof(uintptr_t) * 8)

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

    #define FLAG_FIRST_BYTE(b) \
        ((uintptr_t)(b))

    #define FLAG_SECOND_BYTE(b) \
        ((uintptr_t)(b) << 8)

    #define FLAG_THIRD_BYTE(b) \
        ((uintptr_t)(b) << 16)

    #define FLAG_FOURTH_BYTE(b) \
        ((uintptr_t)(b) << 24)

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
// aliasing exemption for character types.

#if !defined(DEBUG_CHECK_CASTS)  // use x_cast and throw away const knowledge
    #define FIRST_BYTE(p)       x_cast(Byte*, (p))[0]
    #define SECOND_BYTE(p)      x_cast(Byte*, (p))[1]
    #define THIRD_BYTE(p)       x_cast(Byte*, (p))[2]
    #define FOURTH_BYTE(p)      x_cast(Byte*, (p))[3]
#else
    INLINE Byte FIRST_BYTE(const void* p)
      { return cast(const Byte*, p)[0]; }

    INLINE Byte& FIRST_BYTE(void* p)
      { return cast(Byte*, p)[0]; }

    INLINE Byte SECOND_BYTE(const void* p)
      { return cast(const Byte*, p)[1]; }

    INLINE Byte& SECOND_BYTE(void* p)
      { return cast(Byte*, p)[1]; }

    INLINE Byte THIRD_BYTE(const void* p)
      { return cast(const Byte*, p)[2]; }

    INLINE Byte& THIRD_BYTE(void *p)
      { return cast(Byte*, p)[2]; }

    INLINE Byte FOURTH_BYTE(const void* p)
      { return cast(const Byte*, p)[3]; }

    INLINE Byte& FOURTH_BYTE(void* p)
      { return cast(Byte*, p)[3]; }
#endif


// There might not seem to be a good reason to keep the uint16_t variant in
// any particular order.  But if you cast a uintptr_t (or otherwise) to byte
// and then try to read it back as a uint16_t, compilers see through the
// cast and complain about strict aliasing.  Building it out of bytes makes
// these generic (so they work with uint_fast32_t, or uintptr_t, etc.) and
// as long as there has to be an order, might as well be platform-independent.

INLINE uint16_t FIRST_UINT16(const void* p) {
    const Byte* bp = cast(const Byte*, p);
    return cast(uint16_t, bp[0] << 8) | bp[1];
}

INLINE uint16_t SECOND_UINT16(const void* p) {
    const Byte* bp = cast(const Byte*, p);
    return cast(uint16_t, bp[2] << 8) | bp[3];
}

INLINE void SET_FIRST_UINT16(void *p, uint16_t u) {
    Byte* bp = cast(Byte*, p);
    bp[0] = u / 256;
    bp[1] = u % 256;
}

INLINE void SET_SECOND_UINT16(void* p, uint16_t u) {
    Byte* bp = cast(Byte*, p);
    bp[2] = u / 256;
    bp[3] = u % 256;
}

INLINE uintptr_t FLAG_FIRST_UINT16(uint16_t u)
  { return FLAG_FIRST_BYTE(u / 256) | FLAG_SECOND_BYTE(u % 256); }

INLINE uintptr_t FLAG_SECOND_UINT16(uint16_t u)
  { return FLAG_THIRD_BYTE(u / 256) | FLAG_FOURTH_BYTE(u % 256); }


// !!! SECOND_UINT32 should be defined on 64-bit platforms, for any enhanced
// features that might be taken advantage of when that storage is available.


//=//// TYPE-PUNNING BITFIELD DEBUG HELPER (GCC LITTLE-ENDIAN ONLY) ///////=//
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
#if !defined(NDEBUG) && GCC_VERSION_AT_LEAST(7, 0) && ENDIAN_LITTLE
    struct Reb_Series_Header_Pun {
        int _07_cell_always_false:1;
        int _06_stack:1;
        int _05_root:1;
        int _04_transient:1;
        int _03_marked:1;
        int _02_managed:1;
        int _01_free:1;
        int _00_node_always_true:1;

        int _15_unused:1;
        int _14_unused:1;
        int _13_has_dynamic:1;
        int _12_is_array:1;
        int _11_power_of_two:1;
        int _10_utf8_string:1;
        int _09_fixed_size:1;
        int _08_not_end_always_true:1;

        int _23_array_unused:1;
        int _22_array_tail_newline;
        int _21_array_unused:1;
        int _20_array_pairlist:1;
        int _19_array_varlist:1;
        int _18_array_paramlist:1;
        int _17_array_nulleds_legal:1;
        int _16_array_file_line:1;
    }__attribute__((packed));

    struct Reb_Info_Header_Pun {
        int _07_cell_always_false:1;
        int _06_frozen:1;
        int _05_hold:1;
        int _04_protected:1;
        int _03_black:1;
        int _02_unused:1;
        int _01_free_always_false:1;
        int _00_node_always_true:1;

        unsigned int _08to15_wide:8;

        unsigned int _16to23_len_if_non_dynamic:8;

        int _31_unused:1;
        int _30_unused:1;
        int _29_api_release:1;
        int _28_shared_keylist:1;
        int _27_string_canon:1;
        int _26_frame_failed:1;
        int _25_inaccessible:1;
        int _24_auto_locked:1;
    }__attribute__((packed));

    struct Reb_Value_Header_Pun {
        int _07_cell_always_true:1;
        int _06_stack:1;
        int _05_root:1;
        int _04_transient:1;
        int _03_marked:1;
        int _02_managed:1;
        int _01_free:1;
        int _00_node_always_true:1;

        unsigned int _08to15_kind:8;

        int _23_unused:1;
        int _22_eval_flip:1;
        int _21_infixed:1;
        int _20_unevaluated:1;
        int _19_newline_before:1;
        int _18_falsey:1;
        int _17_thrown:1;
        int _16_protected:1;

        unsigned int _24to31_type_specific_bits:8;
    }__attribute__((packed));
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE HEADER a.k.a `union HeaderUnion` (for Cell and Stub uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// platform-sized integer...while still being able to control the underlying
// ordering of those bits in memory.  See FLAG_LEFT_BIT() for how this is
// achieved.
//
// This control allows the leftmost byte of a Rebol header (the one you'd
// get by casting Value* to an unsigned char*) to always start with the bit
// pattern `10`.  This pattern corresponds to what UTF-8 calls "continuation
// bytes", which may never legally start a UTF-8 string:
//
// https://en.wikipedia.org/wiki/UTF-8#Codepage_layout
//
// There are applications of HeaderUnion as an "implicit terminator".  Such
// header patterns don't actually start valid REBNODs, but have a bit pattern
// able to signal the IS_END() test for Cell.  See Endlike_Header()
//

union HeaderUnion {
    //
    // unsigned integer that's the size of a platform pointer (e.g. 32-bits on
    // 32 bit platforms and 64-bits on 64 bit machines).  See macros like
    // FLAG_LEFT_BIT() for how these bits are laid out in a special way.
    //
    // !!! Future application of the 32 unused header bits on 64-bit machines
    // might add some kind of optimization or instrumentation, though the
    // unused bits are currently in weird byte positions.
    //
    uintptr_t bits;

  #if !defined(NDEBUG)
    char bytes_pun[4];

    #if GCC_VERSION_AT_LEAST(7, 0) && ENDIAN_LITTLE
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
// The second-leftmost bit will be 0 for all HeaderUnion in the system that
// are "valid".  This completes the plan of making sure all Cell and Stub
// that are usable will start with the bit pattern 10xxxxxx, which always
// indicates an invalid leading byte in UTF-8.
//
// The exception are freed nodes, but they use 11000000 and 110000001 for
// freed Stub nodes and "freed" value nodes (trash).  These are the bytes
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
// See Manage_Flex() for details on the lifecycle of a series (how it starts
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
// See `FLEX_INFO_BLACK` for a generic bit available to other routines
// that wish to have an arbitrary marker on series (for things like
// recursion avoidance in algorithms).
//
// Because "pairings" can wind up marking what looks like both a value cell
// and a series, it's a bit dangerous to try exploiting this bit on a generic
// clel.  If one is *certain* that a value is not "paired" (e.g. it's in
// a function arglist, or array slot), it may be used for other things, e.g.
//
// * ARG_MARKED_CHECKED -- This uses the NODE_FLAG_MARKED bit on args in
//   action frames, and in particular specialization uses it to denote which
//   arguments in a frame are actually specialized.  This helps notice the
//   difference during an APPLY of encoded partial refinement specialization
//   encoding from just a user putting random values in a refinement slot.
//
// * OUT_MARKED_STALE -- This application of NODE_FLAG_MARKED helps show
//   when an evaluation step didn't add any new output, but it does not
//   overwrite the contents of the out cell.  This allows the evaluator to
//   leave a value in the output slot even if there is trailing invisible
//   evaluation to be done, such as in `[1 + 2 elide (print "Hi")]`, where
//   something like ALL would want to hold onto the 3 without needing to
//   cache it in some other location.  Stale out cells cannot be used as
//   left side input for infix.
//
// **IMPORTANT**: This means that a routine being passed an arbitrary value
//   should not make assumptions about the marked bit.  It should only be
//   used in circumstances where some understanding of being "in control"
//   of the bit are in place--like processing an array a routine itself made.
//
#define NODE_FLAG_MARKED \
    FLAG_LEFT_BIT(3)
#define NODE_BYTEMASK_0x10_MARKED 0x10

#define CELL_FLAG_ARG_MARKED_CHECKED NODE_FLAG_MARKED
#define CELL_FLAG_OUT_MARKED_STALE NODE_FLAG_MARKED
#define CELL_FLAG_VAR_MARKED_REUSE NODE_FLAG_MARKED


//=//// NODE_FLAG_4 (fifth-leftmost bit) //////////////////////////////////=//
//
#define NODE_FLAG_4 \
    FLAG_LEFT_BIT(4)


//=//// NODE_FLAG_5 (seventh-leftmost bit) ////////////////////////////////=//
//
#define NODE_FLAG_STACK \
    FLAG_LEFT_BIT(5)


//=//// NODE_FLAG_ROOT (sixth-leftmost bit) ///////////////////////////////=//
//
// Means the node should be treated as a root for GC purposes.  If the node
// also has NODE_FLAG_CELL, that means the cell must live in a "pairing"
// Stub-sized structure for two cells.  This indicates it is an API handle.
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
// is `sizeof(Cell)`.
//
// In the debug build, it provides safety for all value writing routines,
// including avoiding writing over "implicit END markers".  For details, see
// Endlike_Header().
//
// In the release build, it distinguishes "pairing" nodes (holders for two
// REBVALs in the same pool as ordinary Stubs) from an ordinary Stub node.
// Plain Stubs have the cell flag clear, while pairing values have it set.
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
#define FREED_FLEX_BYTE 192
#define FREED_CELL_BYTE 193


#define NODE_BYTE(n) \
    FIRST_BYTE(n)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE STRUCTURE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Though the name Node is used for a superclass that can be "in use" or
// "free", this is the definition of the structure for its layout when it
// has NODE_FLAG_FREE set.  In that case, the memory manager will set the
// header bits to have the leftmost byte as FREED_FLEX_BYTE, and use the
// pointer slot right after the header for its linked list of free nodes.
//

struct PoolUnitStruct {
    union HeaderUnion header; // leftmost byte FREED_FLEX_BYTE if free

    struct PoolUnitStruct* next_if_free; // if not free, unit is available

    // Size of a unit must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
};


//=////////////////////////////////////////////////////////////////////////=//
//
// MEMORY ALLOCATION AND FREEING MACROS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's internal memory management is done based on a pooled model, which
// use Alloc_Mem and Free_Mem instead of calling malloc directly.  (See the
// comments on those routines for explanations of why this was done--even in
// an age of modern thread-safe allocators--due to Rebol's ability to exploit
// extra data in its pool block when a series grows.)
//
// Since Free_Mem requires the caller to pass in the size of the memory being
// freed, it can be tricky.  These macros are modeled after C++'s new/delete
// and new[]/delete[], and allocations take either a type or a type and a
// length.  The size calculation is done automatically, and the result is cast
// to the appropriate type.  The deallocations also take a type and do the
// calculations.
//
// In a C++11 build, an extra check is done to ensure the type you pass in a
// FREE or FREE_N lines up with the type of pointer being freed.
//

#define ALLOC(t) \
    cast(t *, Alloc_Mem(sizeof(t)))

#define ALLOC_ZEROFILL(t) \
    cast(t *, memset(ALLOC(t), '\0', sizeof(t)))

#define ALLOC_N(t,n) \
    cast(t *, Alloc_Mem(sizeof(t) * (n)))

#define ALLOC_N_ZEROFILL(t,n) \
    cast(t *, memset(ALLOC_N(t, (n)), '\0', sizeof(t) * (n)))

#if CPLUSPLUS_11
    #define FREE(t,p) \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE type" \
            ); \
            Free_Mem(p, sizeof(t)); \
        } while (0)

    #define FREE_N(t,n,p)   \
        do { \
            static_assert( \
                std::is_same<decltype(p), std::add_pointer<t>::type>::value, \
                "mismatched FREE_N type" \
            ); \
            Free_Mem(p, sizeof(t) * (n)); \
        } while (0)
#else
    #define FREE(t,p) \
        Free_Mem((p), sizeof(t))

    #define FREE_N(t,n,p)   \
        Free_Mem((p), sizeof(t) * (n))
#endif

#define CLEAR(m, s) \
    memset((void*)(m), 0, s)

#define CLEARS(m) \
    memset((void*)(m), 0, sizeof(*m))
