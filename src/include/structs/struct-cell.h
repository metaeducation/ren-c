//
//  file: %struct-cell.h
//  summary: "Cell structure definitions preceding %tmp-internals.h"
//  project: "Ren-C Interpreter and Run-time"
//  homepage: https://github.com/metaeducation/ren-c/
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
// Cell is the struct underlying all Rebol values.  It's designed to be four C
// pointers in size (so 16 bytes on 32-bit platforms and 32 bytes on 64-bit
// platforms).  Operation will be most efficient with those sizes, and there
// are checks on boot to ensure that `sizeof(Cell)` is the correct value for
// the platform.  But mechanically, the system should be *able* to work even
// if the size is bigger.
//
// Of the four 32-or-64-bit slots that each value has, the first slot is used
// for the value's "Header".  Those four bytes are:
//
// * BASE_BYTE: the first byte is a set of flags specially chosen to not
//   collide with the leading byte of a valid UTF-8 sequence.  The flags
//   establish whether this is a Cell or a "Stub", among other features.
//   See %struct-base.h for explanations of these flags.
//
// * KIND_BYTE: the second byte indicates what type of information the other
//   3 slots in the cell describe.  It holds a fundamental datatype known
//   as a "Heart", which has values like TYPE_INTEGER, TYPE_BLOCK, TYPE_TEXT.
//   It also reserves two bits to hold the "Sigil" of the cell, which is
//   whether it's annotated with `@` or `^` or `$`.
//
// * LIFT_BYTE: the third byte indicates how quoted something is, or if it
//   is a quaisform or antiform.  See %sys-quoted.h for more on how the byte
//   is interpreted.
//
// * The fourth byte contains other cell flags.  Some of them apply to any
//   cell type (such as whether the cell should have a new-line after it when
//   molded out during display of its containing array), and others have a
//   different purpose depending on what the KIND_BYTE is.
//
// As for the other 3 slots...obviously, an arbitrary long string won't fit
// into the remaining 3*32 bits, or even 3*64 bits!  You can fit the data for
// an INTEGER! or DECIMAL! in that (at least until they become arbitrary
// precision) but it's not enough for a generic BLOCK!, FRAME!, TEXT!, etc.
// So these slots are often used to point to one or more Rebol "stubs" (see
// %sys-stub.h for an explanation of stubs, which are the base class of
// things like Flex*, Array*, VarList*, and Map*.)
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
// * See %struct-base.h for an explanation of FLAG_LEFT_BIT.
//

typedef struct StubStruct Stub;  // forward decl for DEBUG_USE_UNION_PUNS


//=//// BITS 0-7: NODE FLAGS //////////////////////////////////////////////=//
//
// See the defininitions of BASE_FLAG_XXX for the design points explaining
// why the first byte of cells and stubs are engineered with these specific
// common flags.
//
// The use of BASE_FLAG_MARKED in cells is a little unusual, because it is a
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
// * VAR_MARKED_HIDDEN -- This uses the BASE_FLAG_MARKED bit on args in
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
#define CELL_FLAG_VAR_MARKED_HIDDEN         BASE_FLAG_MARKED
#define CELL_FLAG_PARAMSPEC_SPOKEN_FOR      BASE_FLAG_MARKED


//=//// CELL_FLAG_DONT_MARK_PAYLOAD_1 //////////////////////////////////////=//
//
// If this flag is *NOT* set, that indicates the cell uses the "Split" payload
// and Cell.payload.split.one.base should be marked as a Base by the GC
// (if it is not nullptr)
//
// IT'S IN THE REVERSE SENSE ON PURPOSE.  This means a "free" cell can have
// the following bit pattern WHICH IS NOT A LEGAL LEADING BYTE FOR UTF-8:
//
//    11111xxx: Flags: BASE | UNREADABLE | GC_ONE | GC_TWO | CELL | ...
//
// The free bit denotes an Init_Unreadable() cell, and so long as we set the
// GC_ONE and GC_TWO flags we can still have free choices of `xxx` (e.g.
// arbitrary ROOT, MANAGED, and MARKED flags), while Detect_Rebol_Pointer()
// can be certain it's a cell and not UTF-8.
//
#define CELL_FLAG_DONT_MARK_PAYLOAD_1 \
    BASE_FLAG_GC_ONE


//=//// CELL_FLAG_DONT_MARK_PAYLOAD_2 //////////////////////////////////////=//
//
// If this flag is *NOT* set, that indicates the cell uses the "Split" payload
// and Cell.payload.split.two.base should be marked as a Base by the GC
// (if it is not nullptr)
//
// IT'S IN THE REVERSE SENSE ON PURPOSE.  See CELL_FLAG_DONT_MARK_PAYLOAD_1.
//
#define CELL_FLAG_DONT_MARK_PAYLOAD_2 \
    BASE_FLAG_GC_TWO

#define CELL_MASK_NO_MARKING \
    (CELL_FLAG_DONT_MARK_PAYLOAD_1 | CELL_FLAG_DONT_MARK_PAYLOAD_2)


//=//// BITS 8-15: CELL "KIND" BYTE, HEART AND SIGIL //////////////////////=//
//
// The "Heart" is the fundamental datatype of a Cell, dictating its payload
// layout and interpretation.  It's 64 fundamental types taking up 6 bits,
// and then an extra 2 bits ("crumb") that that encode whether one of 3
// different Sigil [@ ^ $] are present.
//
// Heart and Sigil are multiplexed into a single byte, called KIND_BYTE.
//
// Most of the time code wants to check the Type_Of() of a cell and not it's
// Heart_Of(), as Type treats quoted/quasi/antiform cells differently.  If you
// only check Heart, then (''''x) equals (x) because both hearts are WORD!.
//
// 1. It's good to document where a Byte means a KindByte.  This doesn't
//    come up too often because most code isn't set up to handle the
//    multiplexing of the Heart byte with a Sigil, so most code should just
//    be dealing with Heart or Option(Heart).
//
// 2. There's a complex runtime check to ensure coherence in the KIND_BYTE
//    with the rest of the cell, which is triggered in some C++ builds when
//    you use KIND_BYTE() directly.  This raw accessor is used to implement
//    that layer, and can also be used for efficiency in some cases that
//    want to subvert those checks.

typedef Byte KindByte;  // help document when Byte is Heart + Sigil [1]

#define KIND_BYTE_RAW(cell) /* don't go through KindHolder() [2] */ \
    SECOND_BYTE(&(cell)->header.bits)

#define FLAG_KIND_BYTE(byte) \
    FLAG_SECOND_BYTE(byte)

#define FLAG_HEART(heart) \
    FLAG_KIND_BYTE(u_cast(KindByte, known(HeartEnum, (heart))))

#define MOD_HEART_64  64  /* 64 fundamental types, 2 bit crumb for Sigil */
#define CELL_MASK_HEART_NO_SIGIL  FLAG_SECOND_BYTE(MOD_HEART_64 - 1)

#define CELL_MASK_HEART_AND_SIGIL  FLAG_KIND_BYTE(255)

#define KIND_SIGIL_SHIFT  6


//=//// CELL 2-bit SIGIL! /////////////////////////////////////////////////=//
//
// The KIND_BYTE() is structured so that the top two bits of the byte are
// used for the "Sigil".  This can be [$ @ ^] or nothing.
//

typedef enum {
    SIGIL_0 = 0,
    SIGIL_META = 1,     // ^
    SIGIL_PIN = 2,      // @
    SIGIL_TIE = 3,      // $
    MAX_SIGIL = SIGIL_TIE
} Sigil;

#define FLAG_SIGIL_CRUMB(crumb) \
    FLAG_KIND_BYTE((crumb) << KIND_SIGIL_SHIFT)

#define FLAG_SIGIL(sigil) \
    FLAG_SIGIL_CRUMB(u_cast(Byte, known(Option(Sigil), (sigil))))

#define CELL_MASK_SIGIL  FLAG_SIGIL_CRUMB(3)  // 0b11 << KIND_SIGIL_SHIFT


//=//// BITS 16-23: QUOTED/QUASIFORM/ANTIFORM BYTE ("LIFT") ///////////////=//
//
// Cells can be quote-escaped up to 126 levels.  Because the low bit of the
// lifting byte is reserved for whether the contained value is a quasiform,
// each quoting level effectively adds 2 to the lift byte.
//
// A Cell's underlying "HEART" can report it as something like TYPE_WORD, but
// that is only reported as the result of Type_Of() when LIFT_BYTE() is 2.
// When LIFT_BYTE() is 3 it says it is TYPE_QUASIFORM, and when it's greater
// than 4 then Type_Of() reports TYPE_QUOTED.  A LIFT_BYTE() of 1 will give
// back various TYPE_XXX answers corresponding to the antiforms (such as
// TYPE_SPLICE, TYPE_TRASH, TYPE_ERROR, etc.)
//
// 1. There's a complex runtime check to ensure coherence in the lift byte
//    with the rest of the cell, which is triggered in some C++ builds when
//    you use LIFT_BYTE() directly.  This raw accessor is used to implement
//    that layer, and can also be used for efficiency in some cases that
//    want to subvert those checks.
//
// 2. Not all datatypes have quasiforms/antiforms (e.g. ~/foo/~ is a PATH!
//    with a Quasi-Space in the first and last slots, not a quasiform).  To
//    help avoid casual assignments to LIFT_BYTE() of the 1 and 3 values
//    we prohibit them in certain builds, requiring LIFT_BYTE_RAW() to be
//    used if you are truly sure it's safe.
//

typedef Byte LiftByte;  // help document when Byte means a lifting byte

#define LIFT_BYTE_RAW(cell) /* don't go through LiftHolder() [1] */ \
    THIRD_BYTE(&(cell)->header.bits)

#define DUAL_0  0

#if DEBUG_HOOK_LIFT_BYTE  // Stop `LIFT_BYTE(cell) = ANTIFORM_1` [2]
    struct Antiform_1_Struct { operator LiftByte() const { return 1; } };
    struct Quasiform_3_Struct { operator LiftByte() const { return 3; } };

    constexpr Antiform_1_Struct antiform_1;
    constexpr Quasiform_3_Struct quasiform_3;

    #define ANTIFORM_1      antiform_1
    #define QUASIFORM_3     quasiform_3
#else
    #define ANTIFORM_1      1  // also "QUASI" (QUASI_BIT is set)
    #define QUASIFORM_3     3
#endif

// see above for ANTIFORM_1
#define NOQUOTE_2               2
#define QUASI_BIT               1
// see above for QUASIFORM_3
#define ONEQUOTE_NONQUASI_4     4  // non-quasiquoted state of 1 quote

#define MAX_QUOTE_DEPTH     126         // highest legal quoting level
#define Quote_Shift(n)      ((n) << 1)  // help find manipulation sites

#define FLAG_LIFT_BYTE(byte)         FLAG_THIRD_BYTE(byte)

#define CELL_MASK_LIFT  FLAG_LIFT_BYTE(255)

#define CELL_MASK_HEART_AND_SIGIL_AND_LIFT \
    (CELL_MASK_HEART_AND_SIGIL | CELL_MASK_LIFT)


//=//// BITS 24-31: CELL FLAGS ////////////////////////////////////////////=//
//
// Because the header for cells is only 32 bits on 32-bit platforms, there
// are only 8 bits left over when you've used up the BASE_BYTE, KIND_BYTE,
// and LIFT_BYTE.  These 8 scarce remaining cell bits have to be used very
// carefully...and are multiplexed across types that can be tricky.
//


//=//// CELL_FLAG_CONST ///////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any Flex data it points
// to, regardless of whether that data is in a locked Flex or not.  It is
// possible to get a mutable view on a const value by using MUTABLE, and a
// const view on a mutable value with CONST.
//
// !!! Note: values that don't have meaning for const might use this for
// other things, e.g. actions might use it for "PURE".  But beware that
// types like INTEGER! might have mutable forms like BIGINT, so think twice
// before reusing this bit.
//
#define CELL_FLAG_CONST \
    FLAG_LEFT_BIT(24)  // NOTE: Must be SAME BIT as FEED_FLAG_CONST


//=//// CELL_FLAG_WEIRD ///////////////////////////////////////////////////=//
//
// The "Weird" flag is another sticky flag.  It probably is going to wind up
// being CELL_FLAG_TYPE_SPECIFIC_C, as it's needed to track "ghostability"
// of functions.
//
#define CELL_FLAG_WEIRD \
    FLAG_LEFT_BIT(25)

#define CELL_FLAG_WEIRD_GHOSTABLE  CELL_FLAG_WEIRD


//=//// CELL_FLAG_HINT ////////////////////////////////////////////////////=//
//
// "Hint" is another name for a non-sticky flag like CELL_FLAG_NOTE.  We are
// running out of bits (in 32-bit builds) and competing purposes for bits
// on the evaluative output cell required this to be taken.
//
// Its original purpose has disappeared, but it came back as CELL_FLAG_NOTE
// was being used for typechecking cells, and that was sticky in frames,
// with the frame cells being passed directly to APIs.  So it could not be
// used to indicate antiform API splices in the feed as well, as a typechecked
// value pointer passed directly would just get used.
//
#define CELL_FLAG_HINT \
    FLAG_LEFT_BIT(26)

#define CELL_FLAG_FEED_HINT_ANTIFORM  CELL_FLAG_HINT


//=//// CELL_FLAG_PROTECTED ///////////////////////////////////////////////=//
//
// Values can carry a user-level protection bit.  The bit is not copied by
// Copy_Cell(), and hence reading a protected value and writing it to
// another location will not propagate the protectedness from the original
// value to the copy.
//
// (A Flex has more than one kind of protection in "info" bits that can all
// be checked at once...hence there's not "BASE_FLAG_PROTECTED" in common.)
//
#define CELL_FLAG_PROTECTED \
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

#define CELL_FLAG_NOTE_REMOVE  CELL_FLAG_NOTE
#define CELL_FLAG_STACK_NOTE_SEALED  CELL_FLAG_NOTE

// !!! These two should likely be unified, review the mechanics
#define CELL_FLAG_SCRATCH_VAR_NOTE_ONLY_ACTION  CELL_FLAG_NOTE
#define CELL_FLAG_CURRENT_NOTE_RUN_WORD  CELL_FLAG_NOTE


//=//// CELL_FLAG_NEWLINE_BEFORE //////////////////////////////////////////=//
//
// When the array containing a value with this flag set is molding, that will
// output a new line *before* molding the value.  This flag works in tandem
// with a flag on the array itself which manages whether there should be a
// newline before the closing array delimiter: SOURCE_FLAG_NEWLINE_AT_TAIL.
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
// !!! Note: Antiforms could use this for something else.  So could the
// products of evaluation that are resident in single cells (such as the
// OUT or SPARE).  But being able to set this bit on those cells can be
// useful as a way of preserving notes about the newline status of the
// original cell--for instance.
//
#define CELL_FLAG_NEWLINE_BEFORE \
    FLAG_LEFT_BIT(29)


//=//// CELL_FLAG_TYPE_SPECIFIC_A /////////////////////////////////////////=//
//
// This flag may be used independently, or as part of CELL_MASK_CRUMB.
//
#define CELL_FLAG_TYPE_SPECIFIC_A \
    FLAG_LEFT_BIT(30)


//=//// CELL_FLAG_TYPE_SPECIFIC_B /////////////////////////////////////////=//
//
// This flag may be used independently, or as part of CELL_MASK_CRUMB.
//
// If independent, it's one bit that is custom to the datatype, and is
// persisted when the cell is copied.
//
// CELL_FLAG_LEADING_SPACE (for ANY-SEQUENCE?)
//
// 2-element sequences can be stored in an optimized form if one of the two
// elements is a SPACE.  This permits things like `/a` and `b.` to fit in
// a single cell.  It assumes that if the Stub flavor is FLAVOR_SYMBOL then
// the nonblank thing is a WORD!.
//
#define CELL_FLAG_TYPE_SPECIFIC_B \
    FLAG_LEFT_BIT(31)

#define CELL_FLAG_LEADING_SPACE   CELL_FLAG_TYPE_SPECIFIC_B  // ANY-SEQUENCE?


//=//// PLATFORM-POINTER-SIZED VARIANT UNION //////////////////////////////=//
//
// This is a grab bag of all the different types that can be put in Cell and
// Stub slots that are the size of a platform pointer.  It's what is used for
// generic data representation in Stub.link, Stub.misc, Stub.info, Cell.extra,
// Cell.payload.split.one, and Cell.payload.split.two.
//
// The idea is that extensions that want to have their own custom Stub or
// Cell types would be able to define those custom types without needing to
// rebuild the core, since there's enough data types here and they can
// indicate whether they need GC marking with STUB_FLAG_LINK_NEEDS_MARK
// and CELL_FLAG_DONT_MARK_PAYLOAD_1, etc.  But for built-in Cells and Stubs it's
// okay to add fields here just for the sake of clarity...though all Base
// subclasses have to be assigned to just base. [1]
//
// 1. The garbage collector is designed to generically mark `Base*` entities
//    living in Cell and Stub slots.  To do this generic marking, no matter
//    what Base subclass was used, the same ->base field in a Union has to be
//    assigned in all cases...because if differently typed or different named
//    fields were assigned, then C++ compilers are not obligated to allow
//    access through some kind of canon "type pun":
//
//      https://en.wikipedia.org/wiki/Type_punning#Use_of_union
//
//    This means generic slots that want to have nodes marked by the GC have
//    to use the ->node field of this union...regardless of what Base subtype
//    (Stub, Cell, VarList, Array, String, etc.) they refer to.
//
//    Care should be taken on extraction to give back a `const` reference
//    if the intent is immutability, or a conservative state of possible
//    immutability (e.g. the CONST usermode status hasn't been checked)

struct YmdzStruct  // see %sys-time.h
{
    unsigned year:16;
    unsigned month:4;
    unsigned day:5;
    int zone:7; // +/-15:00 res: 0:15
};

typedef union {
    Base* base;  // all Base subclasses should be assigned to this [1]

  #if DEBUG_USE_UNION_PUNS  // dodgy, use in debug watch at your own risk!
    Stub* stub_pun;  // *maybe* see base as a Stub
    RebolValue* cell_pun;  // *maybe* see base as a Cell
  #endif

    bool bit;  // "wasteful" to just use for one flag, but fast read and write

    char ch;

    Flags flags;

    intptr_t i;
    int_fast32_t i32;

    uintptr_t u;
    uint_fast32_t u32;

    REBD32 d32;  // 32-bit float not in C standard, typically just `float`

    Codepoint codepoint;  // !!! Surrogates are "codepoints"...disallow them?

    struct YmdzStruct ymdz;

    void *p;
    CFunction* cfunc;  // C function/data pointers pointers may differ in size

    Byte at_least_4[sizeof(uintptr_t)];  // 8 bytes on 64-bit systems...

    void *corrupt;  // see NEEDFUL_ASSIGNS_UNUSED_FIELDS

    LineNumber line;  // see ARRAY_FLAG_FILE_LINE

    Length length;  // UTF-8 Everywhere caches to get num_codepoints
} UintptrUnion;

STATIC_ASSERT(sizeof(UintptrUnion) == sizeof(uintptr_t));


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
// See IntegerPayload + DecimalPayload for examples where the 64-bit quantity
// requires things like REBDEC to have 64-bit alignment.  At time of writing,
// this is necessary for the "C-to-Javascript" emscripten build to work.
// It's also likely preferred by x86.
//

// These indices are used into at_least_4 when used as in-cell storage.
//
#define IDX_EXTRA_USED 0
#define IDX_EXTRA_LEN 1

// optimized TUPLE! and PATH! byte forms must leave extra field empty, as
// it's used for binding on these types.  Length is in the payload itself.
//
#define IDX_SEQUENCE_USED 0  // index into at_least_8 when used for storage


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


// COMMA! doesn't use its payload (or extra) for anything.
//
// That is exploited by feeds when they are variadic instead of arrays.  The
// feed cell is used to store va_list information along with a binding in
// a value cell slot.
//
// !!! Now that more than 64 types are available, it is probably clearer to
// make a special type for this.  But it hasn't been a problem so far.
//
struct CommaPayloadStruct {
    //
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

struct SplitPayloadStruct  // generic, for adding payloads after-the-fact
{
    UintptrUnion one;
    UintptrUnion two;
};

typedef union { //=///////////////////////// ACTUAL PAYLOAD DEFINITION ////=//
    //
    // The i64 field is used by INTEGER!
    //
    // TIME! and DATE! also use an integer payload, but they use a different
    // field for it.  This helps in searching for references to uses that
    // relate to those types instead of integer.  But be careful not to
    // assign to one and then read from the other, that isn't guaranteed
    // to work in C++!
    //
    REBI64 i64;
    REBI64 nanoseconds;

    // The dec field is used by DECIMAL!
    //
    REBDEC dec;

    // Small RUNE!s which can fit entirely inside a cell use this space to
    // store their UTF-8 data.  It's at least 8 bytes on 32-bit platforms,
    // but 16 bytes on 64-bit.
    //
    // It's also used by TUPLE! and PATH! and CHAIN! for short byte sequences,
    // like 1.2.3.4 -- not so much because this is important, but because
    // historical Rebol did this for its only behavior of the TUPLE! type.
    //
    Byte at_least_8[sizeof(uintptr_t) * 2];

    struct CommaPayloadStruct comma;

    // Due to strict aliasing, if a routine is going to generically access a
    // Base (e.g. to exploit common checks for mutability) it has to do a
    // read through the same field that was assigned.  Hence, many types
    // whose payloads are nodes use the "Split" payload, which is two separate
    // variant fields.
    //
    // ANY-WORD?  // see %sys-word.h
    //     Symbol* symbol;  // word's non-canonized spelling, UTF-8 string
    //     REBINT index;  // index of word in context (if binding is not null)
    //
    // ANY-CONTEXT?  // see %sys-context.h
    //     VarList* varlist;  // has MISC.meta, LINK.keysource
    //     Phase* phase;  // used by FRAME! contexts, see %sys-frame.h
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
    //     REBINT signed_param_index;  // if negative, consider arg infix
    //     Phase* phase;  // where to look up parameter by its offset
    //
    struct SplitPayloadStruct split;
} PayloadUnion;

STATIC_ASSERT(sizeof(PayloadUnion) == sizeof(uintptr_t) * 2);


//=//// COMPLETED 4-PLATFORM POINTER CELL DEFINITION //////////////////////=//
//
// 1. Regardless of what build is made, the %rebol.h file expects to find the
//    name `struct RebolValueStruct` exported as what the API uses.  In the
//    C build that's the only cell struct, but in the C++ build it can be a
//    derived structure if CHECK_CELL_SUBCLASSES is enabled.
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
//    This is defeated by casting the destination address to a char* (void*
//    seems to have strict aliasing issues).  So Mem_Copy() and Mem_Fill()
//    are macros that do that:
//
//    https://stackoverflow.com/a/76426676
//
#if CHECK_CELL_SUBCLASSES
    struct alignas(ALIGN_SIZE) Cell : public Base  // Type_Of() illegal
#elif CPLUSPLUS_11
    struct alignas(ALIGN_SIZE) RebolValueStruct : public Base
#elif C_11
    struct alignas(ALIGN_SIZE) RebolValueStruct  // exported name for API [1]
#else
    struct RebolValueStruct  // ...have to just hope the alignment "works out"
#endif
    {
        HeaderUnion header;
        UintptrUnion extra;
        PayloadUnion payload;

      #if DEBUG_TRACK_EXTEND_CELLS  // can be VERY handy [2]
        const char *file;  // is Byte (UTF-8), but char* for debug watch
        uintptr_t line;
        uintptr_t tick;
        uintptr_t touch;  // see Touch_Cell(), pads out to 4 * sizeof(void*)
      #endif

    #if CHECK_CELL_SUBCLASSES
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
    memcpy(x_cast(char*, (dst)), (src), (size))  // [4]

#define Mem_Fill(dst,byte,size) \
    memset(x_cast(char*, (dst)), (byte), (size))  // [4]


//=//// CELL SUBCLASSES FOR QUARANTINING STABLE/UNSTABLE ANTIFORMS ////////=//
//
// Systemically, we want to stop antiforms from being put into the array
// elements of blocks, groups, paths, and tuples.  We also want to prevent
// unstable antiforms from being the values of variables.  To make it easier
// to do this, the C++ build offers the ability to make `Element` that
// can't hold any antiforms, `Stable*` that can hold stable antiforms, and
// `Value` that can hold anything--including unstable isotopes.
//
// * Class Hierarchy: Value as base, Stable* derived, Element derived
//   (upside-down for compile-time error preferences--we want passing an
//   Value to a routine that expects only Element to not compile)
//
// * Primary Goal: Prevent passing Atoms/Stables to Element-only routines,
//   or Atoms to Stable*-only routines.
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
// in terms of what are accepted for initialization (see Sink() and Exact()).
//
// Additionally, the Cell* class is differentiated by not allowing you to
// ask for its "type".  This makes it useful in passing to routines that
// are supposed to act agnostically regarding the quoting level of the cell,
// such as molding...where the quoting level is accounted for by the core
// molding process, and mold callbacks are only supposed to account for the
// cell payloads.
//
#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Cell;
    typedef struct RebolValueStruct Stable;
    typedef struct RebolValueStruct Element;
#else
    struct RebolValueStruct : public Cell {};  // can hold unstable antiforms
    struct Stable : public RebolValueStruct {};  // can't hold unstable forms
    struct Element : public Stable {};  // can't hold any antiforms
#endif


//=//// SLOTS /////////////////////////////////////////////////////////////=//
//
// Contexts like OBJECT!, MODULE!, FRAME!, LET!, etc. store "variables".  A
// Cell that holds a variable's value is called a "Slot".  Slots have special
// considerations for handling, because they may store bit patterns that
// indicate a function should be run to fulfill the variable (a "GETTER") or
// a function should be run to accept a value to store (a "SETTER").
// These considerations apply when the Slot's LIFT_BYTE() is DUAL_0.
//
// This means you can't casually use something like Init_Integer() or
// Copy_Cell() to blindly write bit patterns into a Slot, because it might
// overlook handling of the special cases.  And you can't use functions like
// Type_Of() to read a Slot, either.  This means Slots have to go through
// special functions that in the general case, may run arbitrary code in
// the evaluator.
//
// There is one exception: an Init(Slot) e.g. what you get from adding a
// fresh variable to a context, is able to be initialized by any routine
// that could do an Init(Value/Stable*/Element).
//

#if DONT_CHECK_CELL_SUBCLASSES
    typedef struct RebolValueStruct Slot;
#else
    struct Slot : public Cell {};  // can hold unstable antiforms

  #if NEEDFUL_SINK_USES_WRAPPER
  namespace needful {
    template<>
    struct AllowSinkConversion<Slot*, Value> : std::true_type {};

    template<>
    struct AllowSinkConversion<Slot*, Stable> : std::true_type {};

    template<>
    struct AllowSinkConversion<Slot*, Element> : std::true_type {};
  }
  #endif
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
            and std::is_standard_layout<Value>::value
            and std::is_standard_layout<Stable*>::value
            and std::is_standard_layout<Element>::value
            and std::is_standard_layout<Slot>::value,
        "C++ Cells must match C Cells: http://stackoverflow.com/a/7189821/"
    );
#endif
