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
// many datatypes is a `Reb_Header` structure.  The bit layout of this header
// is chosen in such a way that not only can Rebol value pointers (REBVAL*)
// be distinguished from Rebol series pointers (REBSER*), but these can be
// discerned from a valid UTF-8 string just by looking at the first byte.
//
// On a semi-superficial level, this permits a kind of dynamic polymorphism,
// such as that used by panic():
//
//     REBVAL *value = ...;
//     panic (value); // can tell this is a value
//
//     REBSER *series = ...;
//     panic (series) // can tell this is a series
//
//     const char *utf8 = ...;
//     panic (utf8); // can tell this is UTF-8 data (not a series or value)
//
// But a more compelling case is the usage through the API, so variadic
// combinations of strings and values can be intermixed, as in:
//
//     rebRun("poke", series, "1", value)
//
// Internally, the ability to discern these types helps certain structures or
// arrangements from having to find a place to store a kind of "flavor" bit
// for a stored pointer's type.  They can just check the first byte instead.
//
// For lack of a better name, the generic type covering the superclass is
// called a "Rebol Node".
//


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
        int _21_enfixed:1;
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
//  NODE HEADER a.k.a `union Reb_Header` (for REBVAL and REBSER uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// platform-sized integer...while still being able to control the underlying
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
// There are applications of Reb_Header as an "implicit terminator".  Such
// header patterns don't actually start valid REBNODs, but have a bit pattern
// able to signal the IS_END() test for REBVAL.  See Endlike_Header()
//

union Reb_Header {
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


//=//// NODE_FLAG_MANAGED (third-leftmost bit) ////////////////////////////=//
//
// The GC-managed bit is used on series to indicate that its lifetime is
// controlled by the garbage collector.  If this bit is not set, then it is
// still manually managed...and during the GC's sweeping phase the simple fact
// that it isn't NODE_FLAG_MARKED won't be enough to consider it for freeing.
//
// See MANAGE_SERIES for details on the lifecycle of a series (how it starts
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
//   left side input for enfix.
//
// **IMPORTANT**: This means that a routine being passed an arbitrary value
//   should not make assumptions about the marked bit.  It should only be
//   used in circumstances where some understanding of being "in control"
//   of the bit are in place--like processing an array a routine itself made.
//
#define NODE_FLAG_MARKED \
    FLAG_LEFT_BIT(3)

#define ARG_MARKED_CHECKED NODE_FLAG_MARKED
#define OUT_MARKED_STALE NODE_FLAG_MARKED
#define VAR_MARKED_REUSE NODE_FLAG_MARKED


//=//// NODE_FLAG_TRANSIENT (fifth-leftmost bit) //////////////////////////=//
//
// The "TRANSIENT" flag is currently used only by node cells, and only in
// the data stack.  The concept is that data stack cells are so volatile that
// they cannot be passed as REBVAL* addresses to anything that might write
// between frames.  This means that moving any value with an unmanaged binding
// into it need not worry about managing...because the data stack cell has
// no longer lifetime than any cell with which it can interact.
//
#define NODE_FLAG_TRANSIENT \
    FLAG_LEFT_BIT(4)

#define CELL_FLAG_TRANSIENT NODE_FLAG_TRANSIENT


//=//// NODE_FLAG_ROOT (sixth-leftmost bit) ///////////////////////////////=//
//
// Means the node should be treated as a root for GC purposes.  If the node
// also has NODE_FLAG_CELL, that means the cell must live in a "pairing"
// REBSER-sized structure for two cells.  This indicates it is an API handle.
//
// This flag is masked out by CELL_MASK_COPIED, so that when values are moved
// into or out of API handle cells the flag is left untouched.
//
#define NODE_FLAG_ROOT \
    FLAG_LEFT_BIT(5)


//=//// NODE_FLAG_STACK (seventh-leftmost bit) ////////////////////////////=//
//
// When writing to a value cell, it is sometimes necessary to know how long
// that cell will "be alive".  This is important if there is some stack-based
// transient structure in the source cell, which would need to be converted
// into something longer-lived if the destination cell will outlive it.
//
// Hence cells must be formatted to say whether they are CELL_FLAG_STACK or
// not, before any writing can be done to them.  If they are not then they
// are presumed to be indefinite lifetime (e.g. cells resident inside of an
// array managed by the garbage collector).
//
// But if a cell is marked with CELL_FLAG_STACK, that means it is expected
// that scanning *backwards* in memory will find a specially marked REB_FRAME
// cell, which will lead to the frame to whose lifetime the cell is bound.
//
// !!! This feature is a work in progress.
//
// For series, varlists of FRAME! are also marked with this to indicates that
// a context's varlist data lives on the stack.  That means that when the
// action terminates, the data will no longer be accessible (so
// SERIES_INFO_INACCESSIBLE will be true).
//
#define NODE_FLAG_STACK \
    FLAG_LEFT_BIT(6)

#define CELL_FLAG_STACK NODE_FLAG_STACK
#define SERIES_FLAG_STACK NODE_FLAG_STACK


//=//// NODE_FLAG_CELL (eighth-leftmost bit) //////////////////////////////=//
//
// If this bit is set in the header, it indicates the slot the header is for
// is `sizeof(REBVAL)`.
//
// In the debug build, it provides safety for all value writing routines,
// including avoiding writing over "implicit END markers".  For details, see
// Endlike_Header().
//
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


// There are two special invalid bytes in UTF8 which have a leading "110"
// bit pattern, which are freed nodes.  These two patterns are for freed bytes
// and "freed cells"...though NODE_FLAG_FREE is not generally used on purpose
// (mostly happens if reading uninitialized memory)
//
#define FREED_SERIES_BYTE 192
#define FREED_CELL_BYTE 193


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE STRUCTURE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Though the name Node is used for a superclass that can be "in use" or
// "free", this is the definition of the structure for its layout when it
// has NODE_FLAG_FREE set.  In that case, the memory manager will set the
// header bits to have the leftmost byte as FREED_SERIES_BYTE, and use the
// pointer slot right after the header for its linked list of free nodes.
//

struct Reb_Node {
    union Reb_Header header; // leftmost byte FREED_SERIES_BYTE if free

    struct Reb_Node *next_if_free; // if not free, entire node is available

    // Size of a node must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
};

#ifdef NDEBUG
    #define IS_FREE_NODE(p) \
        (did (cast(struct Reb_Node*, (p))->header.bits & NODE_FLAG_FREE))
#else
    inline static bool IS_FREE_NODE(void *p) {
        struct Reb_Node *n = cast(struct Reb_Node*, p);

        if (not (n->header.bits & NODE_FLAG_FREE))
            return false;

        assert(
            FIRST_BYTE(n->header) == FREED_SERIES_BYTE
            or FIRST_BYTE(n->header) == FREED_CELL_BYTE
        );
        return true;
    }
#endif


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

#ifdef CPLUSPLUS_11
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
