//
//  File: %struct-cell.h
//  Summary: "Cell structure definitions preceding %tmp-internals.h"
//  Project: "Ren-C Interpreter and Run-time"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// system should be *able* to work even if the size is different.
//
// Of the four 32-or-64-bit slots that each value has, the first is used for
// the value's "Header".  This includes the data type, such as REB_INTEGER,
// REB_BLOCK, REB_TEXT, etc.  Then there are flags which are for general
// purposes that could apply equally well to any type of value (including
// whether the value should have a new-line after it when molded out inside
// of a block).
//
// Obviously, an arbitrary long string won't fit into the remaining 3*32 bits,
// or even 3*64 bits!  You can fit the data for an INTEGER or DECIMAL in that
// (at least until they become arbitrary precision) but it's not enough for
// a generic BLOCK! or an ACTION! (for instance).  So the remaining bits
// often will point to one or more Rebol "nodes" (see %sys-series.h for an
// explanation of Series*, Array*, Context*, and Map*.)
//
// So the next part of the structure is the "Extra".  This is the size of one
// pointer, which sits immediately after the header (that's also the size of
// one pointer).  For built-in types this can carry instance data for the
// cell--such as a binding, or extra bits for a fixed-point decimal.
//
// This sets things up for the "Payload"--which is the size of two pointers.
// It is broken into a separate structure at this position so that on 32-bit
// platforms, it can be aligned on a 64-bit boundary (assuming the REBVAL's
// starting pointer was aligned on a 64-bit boundary to start with).  This is
// important for 64-bit value processing on 32-bit platforms, which will
// either be slow or crash if reads of 64-bit floating points/etc. are done
// on unaligned locations.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Forward declarations are in %reb-defs.h
//
// * See %sys-rebnod.h for an explanation of FLAG_LEFT_BIT.  This file defines
//   those flags which are common to every value of every type.  Due to their
//   scarcity, they are chosen carefully.
//

// In the C build, there is simply one structure definition for all value
// cells: the `ValueStruct`.  This is defined in %sys-rebval.h, and most of
// the contents of this file are a no-op.
//
// However, the C++ build breaks down various base classes for values that
// serve roles in type-checking.  The underlying bit pattern is the same,
// but which functions will accept the subclass varies according to what
// is legal for that pattern to do.  (In C, such conventions can only be
// enforced by rule-of-thumb...so building as C++ gives the rules teeth.)
//

typedef struct StubStruct Stub;  // forward decl for DEBUG_USE_UNION_PUNS


#define CELL_MASK_NO_NODES 0  // no CELL_FLAG_FIRST_IS_NODE or SECOND_IS_NODE

#define CELL_MASK_0 0  // considered "Fresh" but not WRITABLE()/READABLE()
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

#define CELL_FLAG_ISSUE_HAS_NODE CELL_FLAG_FIRST_IS_NODE  // make findable
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
// The "heart" is the fundamental datatype a cell, dictating its payload
// layout and interpretation.
//
// Most of the time code wants to check the VAL_TYPE() of a cell and not it's
// HEART, because that treats QUOTED! cells differently.  If you only check
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

#define FLAG_HEART_BYTE(kind)       FLAG_SECOND_BYTE(kind)


//=//// BITS 16-23: QUOTING DEPTH BYTE ("QUOTE") //////////////////////////=//
//
// Cells can be quote-escaped up to 126 levels.  Because the low bit of the
// quoting byte is reserved for whether the value is "QUASI!", hence each
// quoting level effectively adds 2 to the quote byte.
//
// A cell's underlying "HEART" can report it as something like a REB_WORD, but
// if the quoting byte is > 1 VAL_TYPE() says it is REB_QUOTED.  This has the
// potential to cause confusion in the internals.  But the type system is used
// to check at compile-time so that different views of the same cell don't
// get conflated.  See `NoQuote(const Cell*)` for some of that mechanic.
//
#define QUOTE_BYTE(cell) \
    THIRD_BYTE(ensure(const Cell*, cell))

#define FLAG_QUOTE_BYTE(byte)       FLAG_THIRD_BYTE(byte)


#define ISOTOPE_0           0  // Also QUASI (e.g. with NONQUASI_BIT is clear)
#define UNQUOTED_1          1
#define NONQUASI_BIT        1
#define QUASI_2             2
#define ONEQUOTE_3          3  // non-QUASI state of having one quote level

#define AddQuote(byte)           ((byte) + 2)
#define SubtractQuote(byte)      ((byte) - 2)

#define MAX_QUOTE_DEPTH     126  // highest legal quoting level

#define Is_Isotope(v) \
    (QUOTE_BYTE(READABLE(v)) == ISOTOPE_0)

#define Is_Unquoted(v) \
    (QUOTE_BYTE(READABLE(v)) == UNQUOTED_1)

#define Is_Quasi(v) \
    (QUOTE_BYTE(READABLE(v)) == QUASI_2)

#define Is_Quoted(v) \
    (QUOTE_BYTE(READABLE(v)) >= ONEQUOTE_3)  // '''~a~ is quoted, not quasi


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
// (Series have more than one kind of protection in "info" bits that can all
// be checked at once...hence there's not "NODE_FLAG_PROTECTED" in common.)
//
#define CELL_FLAG_PROTECTED \
    FLAG_LEFT_BIT(24)


//=//// CELL_FLAG_TYPE_SPECIFIC ////////////////////////////////////////////=//
//
// This flag is used for one bit that is custom to the datatype, and is
// persisted when the cell is copied.
//
// CELL_FLAG_REFINEMENT_LIKE (for ANY-SEQUENCE!)
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

#define CELL_FLAG_REFINEMENT_LIKE   CELL_FLAG_TYPE_SPECIFIC  // ANY-SEQUENCE!

#define CELL_FLAG_ENFIX_FRAME   CELL_FLAG_TYPE_SPECIFIC  // FRAME!


//=//// CELL_FLAG_26 ///////////////////////////////////////////////////////=//
//
#define CELL_FLAG_26 \
    FLAG_LEFT_BIT(26)


//=//// CELL_FLAG_UNEVALUATED /////////////////////////////////////////////=//
//
// Some functions wish to be sensitive to whether or not their argument came
// as a literal in source or as a product of an evaluation.  While all values
// carry the bit, it is only guaranteed to be meaningful on arguments in
// function frames...though it is valid on any result at the moment of taking
// it from Eval_Core().
//
// It is in the negative sense because the act of requesting it is uncommon,
// e.g. from the QUOTE operator.  So most Init_Blank() or other assignment
// should default to being "evaluative".
//
// !!! This concept is somewhat dodgy and experimental, but it shows promise
// in addressing problems like being able to give errors if a user writes
// something like `if [x > 2] [print "true"]` vs. `if x > 2 [print "true"]`,
// while still tolerating `item: [a b c] | if item [print "it's an item"]`.
// That has a lot of impact for the new user experience.
//
#define CELL_FLAG_27 \
    FLAG_LEFT_BIT(27)

#define CELL_FLAG_UNEVALUATED CELL_FLAG_27


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
// !!! Currently, ANY-PATH! rendering just ignores this bit.  Some way of
// representing paths with newlines in them may be needed.
//
#define CELL_FLAG_NEWLINE_BEFORE \
    FLAG_LEFT_BIT(29)


//=//// CELL_FLAG_CONST ///////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any series or data it points
// to, regardless of whether that data is in a locked series or not.  It is
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
// like `do mutable compose [...]` or `make object! mutable load ...` can
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
        | CELL_FLAG_UNEVALUATED \
        | CELL_FLAG_PROTECTED)

#define CELL_MASK_ALL \
    ~cast(Flags, 0)

// The poison mask has NODE_FLAG_CELL but no NODE_FLAG_NODE, so it is not
// READABLE(), and it is CELL_FLAG_PROTECTED so it's not WRITABLE() and nor
// can it be FRESHEN().  It has to be ERASE()'d.
//
#define CELL_MASK_POISON \
    (NODE_FLAG_CELL | CELL_FLAG_PROTECTED)


//=//// UNITS OF ARRAYS (CELLS) ///////////////////////////////////////////=//
//
// Cells are array units that don't (necessarily) have fully resolved binding.
// If they are something like a WORD!, they cannot be looked up to find a
// variable unless coupled with a "specifier".
//
// (The bit pattern inside the cell may actually be "absolute"--e.g. no
// specifier needed--but many routines accept a relative view as a principle
// of least privilege.  e.g. you can get the symbol of a word regardless of
// whether it is absolute or relative).
//
// Note that in the C build, %rebol.h forward-declares `struct ValueStruct` and
// then #defines REBVAL to that.
//
#if (! CPLUSPLUS_11)
    typedef struct ValueStruct Cell;
#else
    struct Cell;  // won't implicitly downcast to REBVAL
#endif


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

struct Reb_Character_Extra { Codepoint codepoint; };  // see %sys-char.h

struct Reb_Date_Extra  // see %sys-time.h
{
    unsigned year:16;
    unsigned month:4;
    unsigned day:5;
    int zone:7; // +/-15:00 res: 0:15
};

struct Reb_Parameter_Extra  // see %sys-parameter.h
{
    Flags parameter_flags;  // PARAMETER_FLAG_XXX and PARAMCLASS_BYTE
};

union AnyUnion {  // needed to beat strict aliasing, used in payload
    bool flag;  // "wasteful" to just use for one flag, but fast to read/write

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
    Cell* cell_pun;
  #endif

    Byte exactly_4[sizeof(uint32_t)];
    Byte at_least_4[sizeof(uintptr_t)];

    // This should be initialized with ZEROTRASH, which permits optimization in
    // release builds and more likely to cause an error in debug builds.  See
    // remarks in ZERO_UNUSED_CELL_FIELDS regarding the rationale.
    //
    void *trash;
};

union Reb_Bytes_Extra {
    Byte exactly_4[sizeof(uint32_t)];
    Byte at_least_4[sizeof(uintptr_t)];
};

#define IDX_EXTRA_USED 0  // index into exactly_4 when used for in cell storage
#define IDX_EXTRA_LEN 1  // index into exactly_4 when used for in cell storage

// optimized TUPLE! and PATH! byte forms must leave extra field empty, as
// it's used for binding/specifiers on these types.  So the length is in
// the payload itself.
//
#define IDX_SEQUENCE_USED 0  // index into at_least_8 when used for storage

union ValueExtraUnion { //=/////////////////// ACTUAL EXTRA DEFINITION ////=//

    struct Reb_Character_Extra Character;
    const Node* Binding;  // const Node for GC mark w/update consistency
    struct Reb_Date_Extra Date;
    struct Reb_Parameter_Extra Parameter;

    union AnyUnion Any;
    union Reb_Bytes_Extra Bytes;
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

struct Reb_Character_Payload {  // see %sys-char.h
    Byte size_then_encoded[8];
};

struct Reb_Integer_Payload { REBI64 i64; };  // see %sys-integer.h

struct Reb_Decimal_Payload { REBDEC dec; };  // see %sys-decimal.h

struct Reb_Time_Payload {  // see %sys-time.h
    REBI64 nanoseconds;
};

struct AnyUnion_Payload  // generic, for adding payloads after-the-fact
{
    union AnyUnion first;
    union AnyUnion second;
};

union Reb_Bytes_Payload  // IMPORTANT: Do not cast, use `Pointers` instead
{
    Byte exactly_8[sizeof(uint32_t) * 2];  // same on 32-bit/64-bit platforms
    Byte at_least_8[sizeof(void*) * 2];  // size depends on platform
};

// COMMA! is evaluative, but you wouldn't usually think of it as being
// bindable because of its "inert-seeming" content.  To make the Any_Inert()
// test fast, REB_COMMA is pushed to a high value, making it bindable.  That
// is exploited by feeds, which use it to store va_list information along
// with a specifier in a value cell slot.  (Most commas don't have this.)
//
struct Reb_Comma_Payload {
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

union ValuePayloadUnion { //=/////////////// ACTUAL PAYLOAD DEFINITION ////=//

    // Due to strict aliasing, if a routine is going to generically access a
    // node (e.g. to exploit common checks for mutability) it has to do a
    // read through the same field that was assigned.  Hence, many types
    // whose payloads are nodes use the generic "Any" payload, which is
    // two separate variant fields.  If CELL_FLAG_FIRST_IS_NODE is set, then
    // if that is a series node it will be used to answer questions about
    // mutability (beyond CONST, which the cell encodes itself)
    //
    // ANY-WORD!  // see %sys-word.h
    //     String* spelling;  // word's non-canonized spelling, UTF-8 string
    //     REBINT index;  // index of word in context (if binding is not null)
    //
    // ANY-CONTEXT!  // see %sys-context.h
    //     Array* varlist;  // has MISC.meta, LINK.keysource
    //     Action* phase;  // used by FRAME! contexts, see %sys-frame.h
    //
    // ANY-SERIES!  // see %sys-series.h
    //     Series* rebser;  // vector/double-ended-queue of equal-sized items
    //     REBLEN index;  // 0-based position (e.g. 0 means Rebol index 1)
    //
    // ACTION!  // see %sys-action.h
    //     Array* paramlist;  // has MISC.meta, LINK.underlying
    //     Details* details;  // has MISC.dispatcher, LINK.specialty
    //
    // VARARGS!  // see %sys-varargs.h
    //     REBINT signed_param_index;  // if negative, consider arg enfixed
    //     Action* phase;  // where to look up parameter by its offset

    struct AnyUnion_Payload Any;

    struct Reb_Character_Payload Character;
    struct Reb_Integer_Payload Integer;
    struct Reb_Decimal_Payload Decimal;
    struct Reb_Time_Payload Time;

    union Reb_Bytes_Payload Bytes;
    struct Reb_Comma_Payload Comma;

  #if !defined(NDEBUG) // unsafe "puns" for easy debug viewing in C watchlist
    int64_t int64_pun;
  #endif
};


//=//// COMPLETED 4-PLATFORM POINTER CELL DEFINITION //////////////////////=//
//
// 1. Regardless of what build is made, the %rebol.h file expects to find
//    the name `struct ValueStruct` exported as what the API uses.  In the
//    C build that's the only cell struct, but in the C++ build it's a
//    derived structure.
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
#if CPLUSPLUS_11
    struct alignas(ALIGN_SIZE) CellBase : public Node  // VAL_TYPE() illegal
#elif C_11
    struct alignas(ALIGN_SIZE) ValueStruct  // exported name for API [1]
#else
    struct ValueStruct  // ...have to just hope the alignment "works out"
#endif
    {
        union HeaderUnion header;
        union ValueExtraUnion extra;
        union ValuePayloadUnion payload;

      #if DEBUG_TRACK_EXTEND_CELLS  // can be VERY handy [2]
        const char *file;  // is Byte (UTF-8), but char* for debug watch
        uintptr_t line;
        uintptr_t tick;  // stored in the ValueExtraUnion for basic tracking
        uintptr_t touch;  // see TOUCH_CELL(), pads out to 4 * sizeof(void*)
      #endif

      #if CPLUSPLUS_11
      public:
        CellBase () = default;

      private:  // disable assignment and copying [3]
        CellBase (const CellBase& other) = default;
        CellBase& operator= (const CellBase& rhs) = default;
      #endif
    };

#define Mem_Copy(dst,src,size) \
    memcpy(cast(void*, (dst)), (src), (size))  // [4]

#define Mem_Fill(dst,val,size) \
    memset(cast(void*, (dst)), (val), (size))  // [4]

#if CPLUSPLUS_11
    struct Cell : public CellBase {};  // VAL_TYPE() legal

    static_assert(
        std::is_standard_layout<Cell>::value,
        "C++ Cell must match C Value: http://stackoverflow.com/a/7189821/"
    );
#endif


#define PAYLOAD(Type, v) \
    (v)->payload.Type

#define EXTRA(Type, v) \
    (v)->extra.Type


//=//// ESCAPE-ALIASABLE CELLS ////////////////////////////////////////////=//
//
// The header contains a QUOTE_BYTE() that can encode up to 126 levels of
// quoting (with an extra bit for being a quasiform, and an isotope if the
// byte is 0).  This is independent of the cell's "heart", or underlying
// layout for its unquoted type.
//
// Most of the time, routines want to see these as QUOTED!/QUASI!/ISOTOPE!.
// But some lower-level routines (like molding or comparison) want to act
// on them in-place without making a copy.  To ensure they see the value for
// the "type that it is" and use Cell_Heart() and not VAL_TYPE(), this subtype
// prevents VAL_TYPE() operations.
//
// The way it works is simply to be CellBase...base class of a regular Cell.
// Because a Cell can be passed anywhere a CellBase can, but not vice-versa.
// Then overloading is used to turn off functions like VAL_TYPE() when used
// on a CellBase.
//
// Notationally, the choice is to make it appear that the NoQuote modifies
// the type as `NoQuote(const Cell*)`.
//
// Note: This needs special handling in %make-headers.r to recognize the
// format.  See the `typemacro_parentheses` rule.
//
#if (! CPLUSPLUS_11)
    #define NoQuote(maybe_const_cell_star) \
        maybe_const_cell_star
#else
    template<typename T>
    struct NoQuoteTypeHelper {
        static_assert(not std::is_same<T,T>::value, "[Cell*, const Cell*]");
    };

    template<>
    struct NoQuoteTypeHelper<const Cell*>
      { typedef const CellBase* type; };

    template<>
    struct NoQuoteTypeHelper<Cell*>
      { typedef CellBase* type; };

    #define NoQuote(maybe_const_cell_star) \
        NoQuoteTypeHelper<maybe_const_cell_star>::type
#endif
