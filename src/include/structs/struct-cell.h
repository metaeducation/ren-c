//
//  File: %struct-cell.h
//  Summary: "Cell structure definitions preceding %tmp-internals.h"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// Cell is the structure/union for all Rebol values. It's designed to be
// four C pointers in size (so 16 bytes on 32-bit platforms and 32 bytes
// on 64-bit platforms).  Operation will be most efficient with those sizes,
// and there are checks on boot to ensure that `sizeof(Cell)` is the
// correct value for the platform.  But from a mechanical standpoint, the
// system should be *able* to work even if the size is bigger.
//
// Of the four 32-or-64-bit slots that each value has, the first slot is used
// for the value's "Header":
//
// * NODE_BYTE: the first byte is a set of flags specially chosen to not
//   collide with the leading byte of a valid UTF-8 sequence.  The flags
//   establish whether this is a Cell or a "Stub", among other features.
//   See %struct-node.h for explanations of these flags.
//
// * HEART_BYTE: the second byte indicates what type of information the other
//   3 slots in the cell describe.  It corresponds to a datatype, such as
//   REB_INTEGER, REB_BLOCK, REB_TEXT, tec.
//
// * QUOTE_BYTE: the third byte indicates how quoted something is, or if it
//   is a quaisform or antiform.  See %sys-quoted.h for more on how the byte
//   is interpreted.
//
// * The fourth byte contains other cell flags.  Some of them apply to any
//   cell type (such as whether the cell should have a new-line after it when
//   molded out during display of its containing array), and others have a
//   different purpose depending on what the HEART_BYTE is.
//
// As for the other 3 slots...obviously, an arbitrary long string won't fit
// into the remaining 3*32 bits, or even 3*64 bits!  You can fit the data for
// an INTEGER! or DECIMAL! in that (at least until they become arbitrary
// precision) but it's not enough for a generic BLOCK!, FRAME!, TEXT!, etc.
// So these slots are often used to point to one or more Rebol "stubs" (see
// %sys-stub.h for an explanation of stubs, which are the base class of
// things like Flex*, Array*, Context*, and Map*.)
//
// So the next part of the structure is the "Extra".  This is the size of one
// pointer, which sits immediately after the header (that's also the size of
// one pointer).  For built-in types this can carry instance data for the
// cell--such as a binding, or extra bits for a fixed-point decimal.
//
// This sets things up for the "Payload"--which is the size of two pointers.
// It is broken into a separate structure at this position so that on 32-bit
// platforms, it can be aligned on a 64-bit boundary (assuming the cell's
// starting pointer was aligned on a 64-bit boundary to start with).  This is
// important for 64-bit value processing on 32-bit platforms, which will
// either be slow or crash if reads of 64-bit floating points/etc. are done
// on unaligned locations.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Forward declarations are in %reb-defs.h
//
// * See %struct-node.h for an explanation of FLAG_LEFT_BIT.
//

typedef struct StubStruct Stub;  // forward decl for DEBUG_USE_UNION_PUNS


#define CELL_MASK_NO_NODES 0  // no CELL_FLAG_FIRST_IS_NODE or SECOND_IS_NODE

#define CELL_MASK_0 0  // "Fresh" so initable, but not readable or writable
#define CELL_MASK_0_ROOT NODE_FLAG_ROOT  // same (but for API cells)


//=//// BITS 0-7: NODE FLAGS //////////////////////////////////////////////=//
//
// See the defininitions of NODE_FLAG_XXX for the design points explaining
// why the first byte of cells and stubs are engineered with these specific
// common flags.
//
// The use of NODE_FLAG_MARKED in cells is a little unusual, because it is a
// property of the cell location and not of the value (e.g. it is not included
// in CELL_MASK_COPY, and is part of CELL_MASK_PERSIST).  So writing a new
// value into the cell will not update the status of its mark.  It must be
// manually turned off once turned on, or the cell must be reformatted entirely
// with Erase_Cell().
//
// **IMPORTANT**: This means that a routine being passed an arbitrary value
//   should not make assumptions about the marked bit.  It should only be
//   used in circumstances where some understanding of being "in control"
//   of the bit are in place--like processing an array a routine itself made.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// * VAR_MARKED_HIDDEN -- This uses the NODE_FLAG_MARKED bit on args in
//   action frames, and in particular specialization uses it to denote which
//   arguments in a frame are actually specialized.  This helps notice the
//   difference during an APPLY of encoded partial refinement specialization
//   encoding from just a user putting random values in a refinement slot.
//
// * PARAMSPEC_SPOKEN_FOR -- When parameters are optimizing the blocks they
//   receive, this is applied to any elements whose information was subsumed
//   into parameter flags or optimization bytes.  If the parameter could not
//   be fully optimized and needs to process the array, then anything with
//   this mark on it can be skipped.
//
#define CELL_FLAG_VAR_MARKED_HIDDEN         NODE_FLAG_MARKED
#define CELL_FLAG_PARAMSPEC_SPOKEN_FOR      NODE_FLAG_MARKED


//=//// CELL_FLAG_FIRST_IS_NODE ///////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).first.node` should be marked as a node by the GC.
//
#define CELL_FLAG_FIRST_IS_NODE \
    NODE_FLAG_GC_ONE

#define CELL_FLAG_STRINGLIKE_HAS_NODE CELL_FLAG_FIRST_IS_NODE  // make findable
#define CELL_FLAG_SEQUENCE_HAS_NODE CELL_FLAG_FIRST_IS_NODE  // make findable



//=//// CELL_FLAG_SECOND_IS_NODE //////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).second.node` should be marked as a node by the GC.
//
#define CELL_FLAG_SECOND_IS_NODE \
    NODE_FLAG_GC_TWO


//=//// BITS 8-15: CELL LAYOUT TYPE BYTE ("HEART") ////////////////////////=//
//
// The "heart" is the fundamental datatype of a cell, dictating its payload
// layout and interpretation.
//
// Most of the time code wants to check the VAL_TYPE() of a cell and not it's
// HEART, because that treats quoted cells differently.  If you only check
// the heart, then (''''x) will equal (x) because both hearts are WORD!.
//
// 1. In lieu of typechecking cell is-a cell, we assume the macro finding
//    a field called ->header with .bits in it is good enough.  All methods of
//    checking seem to add overhead in the debug build that isn't worth it.
//    To help avoid accidentally passing stubs, the HeaderUnion in a Stub
//    is named "leader" instead of "header".
//
#define HEART_BYTE(cell) \
    SECOND_BYTE(&(cell)->header.bits)  // don't use ensure() [1]

#define FLAG_HEART_BYTE(heart)       FLAG_SECOND_BYTE(heart)


//=//// BITS 16-23: QUOTING DEPTH BYTE ("QUOTE") //////////////////////////=//
//
// Cells can be quote-escaped up to 126 levels.  Because the low bit of the
// quoting byte is reserved for whether the contained value is a quasiform,
// each quoting level effectively adds 2 to the quote byte.
//
// A cell's underlying "HEART" can report it as something like a REB_WORD, but
// if the quoting byte is > 1 VAL_TYPE() says it is REB_QUOTED.  This has the
// potential to cause confusion in the internals.  But the type system is used
// to check at compile-time so that different views of the same cell don't
// get conflated, e.g. Cell* can't have VAL_TYPE() taken on it.
//
#define QUOTE_BYTE(cell) \
    THIRD_BYTE(&(cell)->header.bits)  // don't use ensure(), see HEART_BYTE [1]

#define FLAG_QUOTE_BYTE(byte)       FLAG_THIRD_BYTE(byte)


#define ANTIFORM_0          0  // Also QUASI (e.g. with NONQUASI_BIT is clear)
#define NOQUOTE_1           1
#define NONQUASI_BIT        1
#define QUASIFORM_2         2
#define ONEQUOTE_3          3  // non-QUASI state of having one quote level

#define MAX_QUOTE_DEPTH     126  // highest legal quoting level

#define Quote_Shift(n)      ((n) << 1)  // help find manipulation sites


//=//// BITS 24-31: CELL FLAGS ////////////////////////////////////////////=//
//
// (description here)
//

//=//// CELL_FLAG_PROTECTED ///////////////////////////////////////////////=//
//
// Values can carry a user-level protection bit.  The bit is not copied by
// Copy_Cell(), and hence reading a protected value and writing it to
// another location will not propagate the protectedness from the original
// value to the copy.
//
// (A Flex has more than one kind of protection in "info" bits that can all
// be checked at once...hence there's not "NODE_FLAG_PROTECTED" in common.)
//
#define CELL_FLAG_PROTECTED \
    FLAG_LEFT_BIT(24)


//=//// CELL_FLAG_TYPE_SPECIFIC ////////////////////////////////////////////=//
//
// This flag is used for one bit that is custom to the datatype, and is
// persisted when the cell is copied.
//
// CELL_FLAG_REFINEMENT_LIKE (for ANY-SEQUENCE?)
//
// 2-element sequences can be stored in an optimized form if one of the two
// elements is a BLANK!.  This permits things like `/a` and `b.` to fit in
// a single cell.  It assumes that if the node flavor is FLAVOR_SYMBOL then
// the nonblank thing is a WORD!.
//
// FRAME! uses this to encode enfixedness for actions
//
#define CELL_FLAG_TYPE_SPECIFIC \
    FLAG_LEFT_BIT(25)

#define CELL_FLAG_REFINEMENT_LIKE   CELL_FLAG_TYPE_SPECIFIC  // ANY-SEQUENCE?

#define CELL_FLAG_ENFIX_FRAME   CELL_FLAG_TYPE_SPECIFIC  // FRAME!


//=//// CELL_FLAG_26 ///////////////////////////////////////////////////////=//
//
#define CELL_FLAG_26 \
    FLAG_LEFT_BIT(26)


//=//// CELL_FLAG_27 //////////////////////////////////////////////////////=//
//
#define CELL_FLAG_27 \
    FLAG_LEFT_BIT(27)


//=//// CELL_FLAG_NOTE ////////////////////////////////////////////////////=//
//
// Using the MARKED flag makes a permant marker on the cell, which will be
// there however you assign it.  That's not always desirable for a generic
// flag.  So the CELL_FLAG_NOTE is another general tool that can be used on
// a cell-by-cell basis and not be copied from the location where it is
// applied... but it will be overwritten if you put another value in that
// particular location.
//
// * STACK_NOTE_SEALED -- When building exemplar frames on the stack, you want
//   to observe when a value should be marked as VAR_MARKED_HIDDEN.  But you
//   aren't allowed to write "sticky" cell format bits on stack elements.  So
//   the more ephemeral "note" is used on the stack element and then changed
//   to the sticky flag on the paramlist when popping.
//
#define CELL_FLAG_NOTE \
    FLAG_LEFT_BIT(28)

#define CELL_FLAG_NOTE_REMOVE CELL_FLAG_NOTE
#define CELL_FLAG_BIND_NOTE_REUSE CELL_FLAG_NOTE
#define CELL_FLAG_STACK_NOTE_SEALED CELL_FLAG_NOTE
#define CELL_FLAG_FEED_NOTE_META CELL_FLAG_NOTE
#define CELL_FLAG_USE_NOTE_SET_WORDS CELL_FLAG_NOTE


//=//// CELL_FLAG_NEWLINE_BEFORE //////////////////////////////////////////=//
//
// When the array containing a value with this flag set is molding, that will
// output a new line *before* molding the value.  This flag works in tandem
// with a flag on the array itself which manages whether there should be a
// newline before the closing array delimiter: ARRAY_FLAG_NEWLINE_AT_TAIL.
//
// The bit is set initially by what the scanner detects, and then left to the
// user's control after that.
//
// !!! The native `new-line` is used set this, which has a somewhat poor
// name considering its similarity to `newline` the line feed char.
//
// !!! Currently, ANY-PATH? rendering just ignores this bit.  Some way of
// representing paths with newlines in them may be needed.
//
#define CELL_FLAG_NEWLINE_BEFORE \
    FLAG_LEFT_BIT(29)


//=//// CELL_FLAG_CONST ///////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any Flex data it points
// to, regardless of whether that data is in a locked Flex or not.  It is
// possible to get a mutable view on a const value by using MUTABLE, and a
// const view on a mutable value with CONST.
//
#define CELL_FLAG_CONST \
    FLAG_LEFT_BIT(30)  // NOTE: Must be SAME BIT as FEED_FLAG_CONST


//=//// CELL_FLAG_EXPLICITLY_MUTABLE //////////////////////////////////////=//
//
// While it may seem that a mutable value would be merely one that did not
// carry CELL_FLAG_CONST, there's a need for a separate bit to indicate when
// MUTABLE has been specified explicitly.  That way, evaluative situations
// like `eval mutable compose [...]` or `make object! mutable load ...` can
// realize that they should switch into a mode which doesn't enforce const
// by default--which it would ordinarily do.
//
// If this flag did not exist, then to get the feature of disabled mutability
// would require every such operation taking something like a /MUTABLE
// refinement.  This moves the flexibility onto the values themselves.
//
// While CONST can be added by the system implicitly during an evaluation,
// the MUTABLE flag should only be added by running MUTABLE.
//
#define CELL_FLAG_EXPLICITLY_MUTABLE \
    FLAG_LEFT_BIT(31)


//=//// CELL RESET AND COPY MASKS /////////////////////////////////////////=//
//
// It's important for operations that write to cells not to overwrite *all*
// the bits in the header, because some of those bits give information about
// the nature of the cell's storage and lifetime.  Similarly, if bits are
// being copied from one cell to another, those header bits must be masked
// out to avoid corrupting the information in the target cell.
//
// (!!! In the future, the 64-bit build may use more flags for optimization
// purposes, though not hinge core functionality on those extra 32 bits.)
//
// Additionally, operations that copy need to not copy any of those bits that
// are owned by the cell, plus additional bits that would be reset in the
// cell if overwritten but not copied.
//
// Note that this will clear NODE_FLAG_FREE, so it should be checked by the
// debug build before resetting.
//
// Notice that NODE_FLAG_MARKED is "sticky"; the mark persists with the cell.
// That makes it good for annotating when a frame field is hidden, such as
// when it is local...because you don't want a function assigning a local to
// make it suddenly visible in views of that frame that shouldn't have
// access to the implementation detail phase.  CELL_FLAG_NOTE is a generic
// and more transient flag.
//

#define CELL_MASK_PERSIST \
    (NODE_FLAG_MANAGED | NODE_FLAG_ROOT | NODE_FLAG_MARKED)

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST \
        | CELL_FLAG_NOTE \
        | CELL_FLAG_PROTECTED)

#define CELL_MASK_COPY_KEEP_NOTES \
    ~(CELL_MASK_PERSIST \
        | CELL_FLAG_PROTECTED)

#define CELL_MASK_ALL \
    ~cast(Flags, 0)

// Poison mask has NODE_FLAG_CELL but no NODE_FLAG_NODE, so Ensure_Readable()
// will fail, and it is CELL_FLAG_PROTECTED so Ensure_Writable() will fail.
// Nor can it be freshened with Freshen_Cell().  It has to be Erase_Cell()'d.
//
#define CELL_MASK_POISON \
    (NODE_FLAG_CELL | CELL_FLAG_PROTECTED)


//=//// CELL's `EXTRA` FIELD DEFINITION ///////////////////////////////////=//
//
// Each value cell has a header, "extra", and payload.  Having the header come
// first is taken advantage of by the byte-order-sensitive macros to be
// differentiated from UTF-8 strings, etc. (See: Detect_Rebol_Pointer())
//
// Conceptually speaking, one might think of the "extra" as being part of
// the payload.  But it is broken out into a separate field.  This is because
// the `binding` property is written using common routines for several
// different types.  If the common routine picked just one of the payload
// forms initialize, it would "disengage" the other forms.
//
// (C permits *reading* of common leading elements from another union member,
// even if that wasn't the last union used to write it.  But all bets are off
// for other unions if you *write* a leading member through another one.
// For longwinded details: http://stackoverflow.com/a/11996970/211160 )
//
// Another aspect of breaking out the "extra" is so that on 32-bit platforms,
// the starting address of the payload is on a 64-bit alignment boundary.
// See Reb_Integer and Reb_Decimal  for examples where the 64-bit quantity
// requires things like REBDEC to have 64-bit alignment.  At time of writing,
// this is necessary for the "C-to-Javascript" emscripten build to work.
// It's also likely preferred by x86.
//

struct CharacterExtraStruct {  // see %sys-char.h
    Codepoint codepoint;  // !!! Surrogates are "codepoints"...disallow them?
};

struct DateExtraStruct  // see %sys-time.h
{
    unsigned year:16;
    unsigned month:4;
    unsigned day:5;
    int zone:7; // +/-15:00 res: 0:15
};

struct ParameterExtraStruct  // see %sys-parameter.h
{
    Flags parameter_flags;  // PARAMETER_FLAG_XXX and PARAMCLASS_BYTE
};

struct BytesExtraStruct {
    Byte at_least_4[sizeof(uintptr_t)];
};

union AnyUnion {  // needed to beat strict aliasing, used in payload
    bool bit;  // "wasteful" to just use for one flag, but fast to read/write

    Flags flags;

    intptr_t i;
    int_fast32_t i32;

    uintptr_t u;
    uint_fast32_t u32;

    REBD32 d32;  // 32-bit float not in C standard, typically just `float`

    void *p;
    CFunction* cfunc;  // C function/data pointers pointers may differ in size

    // The NODE_FLAG_GC_ONE and NODE_FLAG_GC_TWO are used by Cells (for
    // Cell_Node1() and Cell_Node2()) and by Stubs (for LINK() and MISC()) to
    // be able to signal the GC to mark those slots if this node field
    // is in use.
    //
    // Care should be taken on extraction to give back a `const` reference
    // if the intent is immutability, or a conservative state of possible
    // immutability (e.g. the CONST usermode status hasn't been checked)
    //
    const Node* node;

    // The GC is only marking one field in the union...the node.  So that is
    // the only field that should be assigned and read.  These "type puns"
    // are unreliable, and for debug viewing only--in case they help.
    //
  #if DEBUG_USE_UNION_PUNS
    Stub* stub_pun;
    RebolValue* cell_pun;
  #endif

    Byte at_least_4[sizeof(uintptr_t)];

    // This should be initialized with ZERO_UNUSED, which permits optimization
    // in release builds and more likely to cause an error in debug builds.
    // See remarks in ZERO_UNUSED_CELL_FIELDS regarding the rationale.
    //
    void *corrupt;
};

// These indices are used into at_least_4 when used as in-cell storage.
//
#define IDX_EXTRA_USED 0
#define IDX_EXTRA_LEN 1
#define IDX_EXTRA_SIGIL 2

// optimized TUPLE! and PATH! byte forms must leave extra field empty, as
// it's used for binding/specifiers on these types.  So the length is in
// the payload itself.
//
#define IDX_SEQUENCE_USED 0  // index into at_least_8 when used for storage

union ExtraUnion { //=//////////////////////// ACTUAL EXTRA DEFINITION ////=//

    struct CharacterExtraStruct Character;
    struct DateExtraStruct Date;
    struct ParameterExtraStruct Parameter;
    struct BytesExtraStruct Bytes;

    union AnyUnion Any;
};


//=//// CELL's `PAYLOAD` FIELD DEFINITION /////////////////////////////////=//
//
// The payload is located in the second half of the cell.  Since it consists
// of four platform pointers, the payload should be aligned on a 64-bit
// boundary even on 32-bit platorms.
//
// `Custom` and `Bytes` provide a generic strategy for adding payloads
// after-the-fact.  This means clients (like extensions) don't have to have
// their payload declarations cluttering this file.
//
// IMPORTANT: `Bytes` should *not* be cast to an arbitrary pointer!!!  That
// would violate strict aliasing.  Only direct payload types should be used:
//
//     https://stackoverflow.com/q/41298619/
//

struct CharacterPayloadStruct {  // see %sys-char.h
    Byte size_then_encoded[8];
};

struct IntegerPayloadStruct { REBI64 i64; };  // see %sys-integer.h

struct DecimalPayloadStruct { REBDEC dec; };  // see %sys-decimal.h

struct TimePayloadStruct {  // see %sys-time.h
    REBI64 nanoseconds;
};

struct BytesPayloadStruct
{
    Byte at_least_8[sizeof(uintptr_t) * 2];  // size depends on platform
};

// COMMA! is not Cell_Extra_Needs_Mark(), and doesn't use its payload.
//
// That is exploited by feeds when they are variadic instead of arrays.  The
// feed cell is used to store va_list information along with a specifier in
// a value cell slot.
//
// !!! Now that more than 64 types are available, it is probably clearer to
// make a special type for this.  But it hasn't been a problem so far.
//
struct CommaPayloadStruct {
    // A feed may be sourced from a va_list of pointers, or not.  If this is
    // NULL it is assumed that the values are sourced from a simple array.
    //
    va_list* vaptr;  // may be nullptr

    // The feed could also be coming from a packed array of pointers...this
    // is used by the C++ interface, which creates a `std::array` on the
    // C stack of the processed variadic arguments it enumerated.
    //
    const void* const* packed;
};

struct AnyPayloadStruct  // generic, for adding payloads after-the-fact
{
    union AnyUnion first;
    union AnyUnion second;
};

union PayloadUnion { //=//////////////////// ACTUAL PAYLOAD DEFINITION ////=//

    // Due to strict aliasing, if a routine is going to generically access a
    // node (e.g. to exploit common checks for mutability) it has to do a
    // read through the same field that was assigned.  Hence, many types
    // whose payloads are nodes use the generic "Any" payload, which is
    // two separate variant fields.  If CELL_FLAG_FIRST_IS_NODE is set, then
    // if that is a Flex Node it will be used to answer questions about
    // mutability (beyond CONST, which the cell encodes itself)
    //
    // ANY-WORD?  // see %sys-word.h
    //     String* spelling;  // word's non-canonized spelling, UTF-8 string
    //     REBINT index;  // index of word in context (if binding is not null)
    //
    // ANY-CONTEXT?  // see %sys-context.h
    //     Array* varlist;  // has MISC.meta, LINK.keysource
    //     Action* phase;  // used by FRAME! contexts, see %sys-frame.h
    //
    // ANY-SERIES?  // see %sys-series.h
    //     Flex* flex;  // vector/double-ended-queue of equal-sized items
    //     REBLEN index;  // 0-based position (e.g. 0 means Rebol index 1)
    //
    // ACTION!  // see %sys-action.h
    //     Array* paramlist;  // has MISC.meta, LINK.underlying
    //     Details* details;  // has MISC.dispatcher, LINK.specialty
    //
    // VARARGS!  // see %sys-varargs.h
    //     REBINT signed_param_index;  // if negative, consider arg enfixed
    //     Action* phase;  // where to look up parameter by its offset

    struct CharacterPayloadStruct Character;
    struct IntegerPayloadStruct Integer;
    struct DecimalPayloadStruct Decimal;
    struct TimePayloadStruct Time;
    struct BytesPayloadStruct Bytes;
    struct CommaPayloadStruct Comma;

    struct AnyPayloadStruct Any;

  #if !defined(NDEBUG) // unsafe "puns" for easy debug viewing in C watchlist
    int64_t int64_pun;
  #endif
};


//=//// COMPLETED 4-PLATFORM POINTER CELL DEFINITION //////////////////////=//
//
// 1. Regardless of what build is made, the %rebol.h file expects to find the
//    name `struct RebolValueStruct` exported as what the API uses.  In the
//    C build that's the only cell struct, but in the C++ build it can be a
//    derived structure if DEBUG_USE_CELL_SUBCLASSES is enabled.
//
// 2. The DEBUG_TRACK_EXTEND_CELLS option doubles the cell size, but is a
//    *very* helpful debug option.  See %sys-track.h for explanation.
//
// 3. The C++ build disables copying and direct assignment of cells, such as:
//
//       Cell dest = *src;  // illegal!
//       *cell1 = *cell2;  // illegal!
//
//    The reason this is done is because not all flags from the source should
//    be copied (see CELL_MASK_COPY) and some flags in the destination must
//    be preserved (see CELL_MASK_PERSIST).  Copy mechanics are handled with
//    C functions (Copy_Cell(), Derelativize()) so really all the C++ build is
//    doing here is helping notice if you try to use a raw byte copy.
//
// 4. In cases where you do want to copy a Cell (or structure containing a
//    Cell) in a bytewise fashion, the copy disablement in [2] throws a wrench
//    into things.  Some compilers will disable memcpy() and memset() under
//    the assumption that those should count as "copying and assignment".
//    This is defeated by casting the destination address to a void*, so
//    Mem_Copy() and Mem_Fill() are macros that do that:
//
//    https://stackoverflow.com/a/76426676
//
#if DEBUG_USE_CELL_SUBCLASSES
    struct alignas(ALIGN_SIZE) Cell : public Node  // VAL_TYPE() illegal
#elif CPLUSPLUS_11
    struct alignas(ALIGN_SIZE) RebolValueStruct : public Node
#elif C_11
    struct alignas(ALIGN_SIZE) RebolValueStruct  // exported name for API [1]
#else
    struct RebolValueStruct  // ...have to just hope the alignment "works out"
#endif
    {
        union HeaderUnion header;
        union ExtraUnion extra;
        union PayloadUnion payload;

      #if DEBUG_TRACK_EXTEND_CELLS  // can be VERY handy [2]
        const char *file;  // is Byte (UTF-8), but char* for debug watch
        uintptr_t line;
        uintptr_t tick;
        uintptr_t touch;  // see Touch_Cell(), pads out to 4 * sizeof(void*)
      #endif

    #if DEBUG_USE_CELL_SUBCLASSES
      public:
        Cell () = default;

      private:  // disable assignment and copying [3]
        Cell (const Cell& other) = default;
        Cell& operator= (const Cell& rhs) = default;
    #elif CPLUSPLUS_11
      public:
        RebolValueStruct () = default;

      private:  // disable assignment and copying [3]
        RebolValueStruct (const RebolValueStruct& other) = default;
        RebolValueStruct& operator= (const RebolValueStruct& rhs) = default;
    #endif
    };

#define Mem_Copy(dst,src,size) \
    memcpy(cast(void*, (dst)), (src), (size))  // [4]

#define Mem_Fill(dst,val,size) \
    memset(cast(void*, (dst)), (val), (size))  // [4]


//=//// CELL SUBCLASSES FOR QUARANTINING STABLE AND UNSTABLE ANTIFORMS /////=//
//
// Systemically, we want to stop antiforms from being put into the array
// elements of blocks, groups, paths, and tuples.  We also want to prevent
// unstable antiforms from being the values of variables.  To make it easier
// to do this, the C++ build offers the ability to make `Element` that
// can't hold any antiforms, `Value` that can hold stable antiforms, and
// `Atom` that can hold anything--including unstable isotopes.
//
// * Class Hierarchy: Atom as base, Value derived, Element derived
//   (upside-down for compile-time error preferences--we want passing an
//   Atom to a routine that expects only Element to fail)
//
// * Primary Goal: Prevent passing Atoms/Values to Element-only routines,
//   or Atoms to Value-only routines.
//
// * Secondary Goal: Prevent things like passing Element cells to writing
//   routines that may potentially produce antiforms in that cell.
//
// * Tertiary Goal: Detect things like superfluous Is_Antiform() calls
//   being made on Elements.
//
// The primary goal is achieved by choosing Element as a most-derived type
// instead of a base type.  The next two goals are trickier, and require a
// smart pointer class to wrap the pointers and invert the class hierarchy
// in terms of what are accepted for initialization (see Sink() and Need()).
//
// Additionally, the Cell* class is differentiated by not allowing you to
// ask for its "type".  This makes it useful in passing to routines that
// are supposed to act agnostically regarding the quoting level of the cell,
// such as molding...where the quoting level is accounted for by the core
// molding process, and mold callbacks are only supposed to account for the
// cell payloads.
//
#if (! DEBUG_USE_CELL_SUBCLASSES)
    typedef struct RebolValueStruct Cell;
    typedef struct RebolValueStruct Atom;
    typedef struct RebolValueStruct Element;
#else
    struct Atom : public Cell  // can hold unstable antiforms
    {
      #if !defined(NDEBUG)
        Atom() = default;
        ~Atom() {
            assert(
                (this->header.bits & (NODE_FLAG_NODE | NODE_FLAG_CELL))
                or this->header.bits == CELL_MASK_0
            );
        }
      #endif
    };

    struct RebolValueStruct : public Atom {  // can't hold unstable antiforms
      #if !defined(NDEBUG)
        RebolValueStruct () = default;
        ~RebolValueStruct () {
            assert(
                (this->header.bits & (NODE_FLAG_NODE | NODE_FLAG_CELL))
                or this->header.bits == CELL_MASK_0
            );
        }
      #endif
    };

    struct Element : public RebolValueStruct {  // can't hold antiforms at all
      #if !defined(NDEBUG)
        Element () = default;
        ~Element () {
            assert(
                (this->header.bits & (NODE_FLAG_NODE | NODE_FLAG_CELL))
                or this->header.bits == CELL_MASK_0
            );
        }
      #endif
    };
#endif


//=//// ENSURE CELL TYPES ARE STANDARD LAYOUT /////////////////////////////=//
//
// Using too much C++ magic can potentially lead to the exported structure
// not having "standard layout" and being incompatible with C.  We want to be
// able to memcpy() cells safely, so check to ensure that is still the case.
//
#if CPLUSPLUS_11
    static_assert(
        std::is_standard_layout<Cell>::value
            and std::is_standard_layout<Atom>::value
            and std::is_standard_layout<Value>::value
            and std::is_standard_layout<Element>::value,
        "C++ Cells must match C Cells: http://stackoverflow.com/a/7189821/"
    );
#endif


#if DEBUG_USE_CELL_SUBCLASSES
    //
    // rebReleaseAndNull is in the API, but because the API doesn't make
    // distinctions between Element and Value the double pointer trips it up
    // in the C++ build.  Add an overload.
    //
    static inline void rebReleaseAndNull(Element** e) {
        rebRelease(*e);
        *e = nullptr;
    }
#endif
