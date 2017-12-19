//
//  File: %sys-rebnod.h
//  Summary: {Definitions for the Rebol_Header-having "superclass" structure}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// But a more compelling case is the planned usage through the API, so that
// variadic combinations of strings and values can be intermixed, as in:
//
//     rebDo("[", "poke", series, "1", value, "]") 
//
// Internally, the ability to discern these types helps certain structures or
// arrangements from having to find a place to store a kind of "flavor" bit
// for a stored pointer's type.  They can just check the first byte instead.
//
// For lack of a better name, the generic type covering the superclass is
// called a "Rebol Node".
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE HEADER a.k.a `struct Reb_Header` (for REBVAL and REBSER uses)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Assignments to bits and fields in the header are done through a native
// platform-sized integer...while still being able to control the underlying
// ordering of those bits in memory.  See FLAGIT_LEFT() in %reb-c.h for how
// this is achieved.
//
// This control allows the leftmost byte of a Rebol header (the one you'd
// get by casting REBVAL* to an unsigned char*) to always start with the bit
// pattern `10`.  This pattern corresponds to what UTF-8 calls "continuation
// bytes", which may never legally start a UTF-8 string:
//
// https://en.wikipedia.org/wiki/UTF-8#Codepage_layout
//
// There are also applications of Reb_Header as an "implicit terminator".
// Such header patterns don't actually start valid REBNODs, but have a bit
// pattern able to signal the IS_END() test for REBVAL.  See notes on
// NODE_FLAG_END and NODE_FLAG_CELL.
//

struct Reb_Header {
    //
    // Uses REBUPT which is 32-bits on 32 bit platforms and 64-bits on 64 bit
    // machines.  Note the numbers and layout in the headers will not be
    // directly comparable across architectures.
    //
    // !!! A clever future application of the 32 unused header bits on 64-bit
    // architectures might be able to add optimization or instrumentation
    // abilities as a bonus.
    //
    REBUPT bits;
};

enum Reb_Pointer_Detect {
    DETECTED_AS_UTF8 = 0,
    
    DETECTED_AS_SERIES = 1,
    DETECTED_AS_FREED_SERIES = 2,

    DETECTED_AS_VALUE = 3,
    DETECTED_AS_END = 4, // may be a cell, or made with Init_Endlike_Header()
    DETECTED_AS_TRASH_CELL = 5
};


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_NODE (leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// For the sake of simplicity, the leftmost bit in a node is always one.  This
// is because every UTF-8 string starting with a bit pattern 10xxxxxxx in the
// first byte is invalid.
//
// Warning: Previous attempts to multiplex this with an information-bearing
// bit were tricky, and wound up ultimately paying for a fixed bit in some
// other situations.  Better to sacrifice the bit and keep it straightforward.
//
#define NODE_FLAG_NODE \
    FLAGIT_LEFT(0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_FREE (second-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The second-leftmost bit will be 0 for all Reb_Header in the system that
// are "valid".  This completes the plan of making sure all REBVAL and REBSER
// that are usable will start with the bit pattern 10xxxxxx, hence not be
// confused with a string...since that always indicates an invalid leading
// byte in UTF-8.
//
// The exception are freed nodes, but they use 11000000 and 110000001 for
// freed REBSER nodes and "freed" value nodes (trash).  These are the bytes
// 192 and 193, which are specifically illegal in any UTF8 sequence.  So
// even these cases may be safely distinguished from strings.  See the
// NODE_FLAG_CELL for why it is chosen to be that 8th bit.
//
#define NODE_FLAG_FREE \
    FLAGIT_LEFT(1)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_MANAGED (third-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
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
#define NODE_FLAG_MANAGED \
    FLAGIT_LEFT(2)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_MARKED (fourth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This flag is used by the mark-and-sweep of the garbage collector, and
// should not be referenced outside of %m-gc.c.
//
// See `SERIES_INFO_BLACK` for a generic bit available to other routines
// that wish to have an arbitrary marker on series (for things like
// recursion avoidance in algorithms).
//
#define NODE_FLAG_MARKED \
    FLAGIT_LEFT(3)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_END (fifth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// If set, it means this header should signal the termination of an array
// of REBVAL, as in `for (; NOT_END(value); ++value) {}` loops.  In this
// sense it means the header is functioning much like a null-terminator for
// C strings.
//
// *** This bit being set does not necessarily mean the header is sitting at
// the head of a full REBVAL-sized slot! ***
//
// Some data structures punctuate arrays of REBVALs with a Reb_Header that
// has the NODE_FLAG_END bit set, and the NODE_FLAG_CELL bit clear.  This
// functions fine as the terminator for a finite number of REBVAL cells, but
// can only be read with IS_END() with no other operations legal.
//
// It's only valid to overwrite end markers when NODE_FLAG_CELL is set.
//
#define NODE_FLAG_END \
    FLAGIT_LEFT(4)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_ROOT (sixth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This indicates the node should be treated as a root for GC purposes.  It
// only means anything on a REBVAL if that REBVAL happens to live in the key
// slot of a paired REBSER--it should not generally be set otherwise.
//
// !!! Review the implications of this flag "leaking" if a key is ever bit
// copied out of a pairing that uses it.  It might not be a problem so long
// as the key is ensured read-only, so that the bit is just noise on any
// non-key that has it...but the consequences may be more sinister.
//
#define NODE_FLAG_ROOT \
    FLAGIT_LEFT(5)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_SPECIAL (seventh-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's a bit of a pun to try and come up with a meaning that is shared
// between REBSER and REBVAL for this bit,  But the specific desire to put the
// NODE_FLAG_CELL in eighth from the left position means it's easier to make
// this a generic node flag to keep the first byte layout knowledge here.
//
// For a REBVAL, this means THROWN.  For a REBSER, this means marked as
// voids being legal.  They alias this as ARRAY_FLAG_VOIDS_LEGAL and
// VALUE_FLAG_THROWN.
//
#define NODE_FLAG_SPECIAL \
    FLAGIT_LEFT(6)


//=////////////////////////////////////////////////////////////////////////=//
//
//  NODE_FLAG_CELL (eighth-leftmost bit)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// If this bit is set in the header, it indicates the slot the header is for
// is `sizeof(REBVAL)`.
//
// Originally it was just for the debug build, to make it safer to use the
// implementation trick of "implicit END markers".  Checking NODE_FLAG_CELL
// before allowing an operation like Init_Word() to write a location
// avoided clobbering NODE_FLAG_END signals that were backed by only
// `sizeof(struct Reb_Header)`.
//
// However, in the release build it became used to distinguish "pairing"
// nodes (holders for two REBVALs in the same pool as ordinary REBSERs)
// from an ordinary REBSER node.  Plain REBSERs have the cell mask clear,
// while paring values have it set.
//
// The position chosen is not random.  It is picked as the 8th bit from the
// left so that freed nodes can still express a distinction between
// being a cell and not, due to 11000000 (192) and 11000001 (193) are both
// invalid UTF-8 bytes, hence these two free states are distinguishable from
// a leading byte of a string.
//
#define NODE_FLAG_CELL \
    FLAGIT_LEFT(7)


// v-- BEGIN GENERAL VALUE AND SERIES BITS WITH THIS INDEX

#define GENERAL_VALUE_BIT 8
#define GENERAL_SERIES_BIT 8


// There are two special invalid bytes in UTF8 which have a leading "110"
// bit pattern, and these are used to signal the header bytes in trashed
// values...this is why NODE_FLAG_CELL is chosen at its position.
//
#define FREED_SERIES_BYTE 192
#define TRASH_CELL_BYTE 193


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
    struct Reb_Header header; // leftmost byte FREED_SERIES_BYTE if free

    struct Reb_Node *next_if_free; // if not free, entire node is available

    // Size of a node must be a multiple of 64-bits.  This is because there
    // must be a baseline guarantee for node allocations to be able to know
    // where 64-bit alignment boundaries are.
    //
    /* REBI64 payload[N];*/
};

#ifdef NDEBUG
    #define IS_FREE_NODE(p) \
        LOGICAL(cast(struct Reb_Node*, (p))->header.bits & NODE_FLAG_FREE)
#else
    // In the debug build, add in an extra check that the left 8 bits of any
    // freed nodes match FREED_SERIES_BYTE or TRASH_CELL_BYTE.  This is
    // needed to distinguish freed nodes from valid UTF8 strings, to implement
    // features like polymorphic fail() or distinguishing strings in the API.
    //
    inline static REBOOL IS_FREE_NODE(void *p) {
        struct Reb_Node *n = cast(struct Reb_Node*, p);

        if (NOT(n->header.bits & NODE_FLAG_FREE))
            return FALSE;

        REBYTE left_8 = LEFT_8_BITS(n->header.bits);
        assert(left_8 == FREED_SERIES_BYTE || left_8 == TRASH_CELL_BYTE);
        return TRUE;
    }
#endif


//
// With these definitions:
//
//     struct Foo_Type { struct Reb_Header header; int x; }
//     struct Foo_Type *foo = ...;
//
//     struct Bar_Type { struct Reb_Header header; float x; }
//     struct Bar_Type *bar = ...;
//
// This C code:
//
//     foo->header.bits = 1020;
//
// ...is actually different *semantically* from this code:
//
//     struct Reb_Header *alias = &foo->header;
//     alias->bits = 1020;
//
// The first is considered as not possibly able to affect the header in a
// Bar_Type.  It only is seen as being able to influence the header in other
// Foo_Type instances.
//
// The second case, by forcing access through a generic aliasing pointer,
// will cause the optimizer to realize all bets are off for any type which
// might contain a `struct Reb_Header`.
//
// This is an important point to know, with certain optimizations of writing
// headers through one type and then reading them through another.  That
// trick is used for "implicit termination", see documentation of IS_END().
//
// (Note that this "feature" of writing through pointers actually slows
// things down.  Desire to control this behavior is why the `restrict`
// keyword exists in C99: https://en.wikipedia.org/wiki/Restrict )
//
inline static void Init_Endlike_Header(struct Reb_Header *alias, REBUPT bits)
{
    // Endlike headers have the leading bits `10` so they don't look like a
    // UTF-8 string.  This makes them look like an "in use node", and they
    // of course have NODE_FLAG_END set.  They do not have NODE_FLAG_CELL
    // set, however, which prevents value writes to them.
    //
    assert(
        NOT(bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_END | NODE_FLAG_CELL
        ))
    );
    alias->bits = bits | NODE_FLAG_NODE | NODE_FLAG_END;
}


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

// !!! Definitions for the memory allocator generally don't need to be
// included by all clients, though currently it is necessary to indicate
// whether a "node" is to be allocated from the REBSER pool or the REBGOB
// pool.  Hence, the REBPOL has to be exposed to be included in the
// function prototypes.  Review this necessity when REBGOB is changed.
//
typedef struct rebol_mem_pool REBPOL;

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
