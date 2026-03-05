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
// * HEARTSIGIL_BYTE: the second byte indicates what type of information the
//   other 3 slots in the cell describe.  It holds a fundamental enum known
//   as a "Heart", with values like HEART_INTEGER, HEART_BLOCK, HEART_TEXT.
//   It also reserves two bits to hold the "Sigil" of the cell, which is
//   whether it's annotated with `@` or `^` or `$`.
//
// * TYPE_BYTE: the third byte equals the HEARTSIGIL_BYTE for "normal" values,
//   but has higher levels indicates how quoted something is, or if it is a
//   quaisform or antiform.  See %sys-quoted.h for more on the interpretation.
//
// * The fourth byte contains other cell flags.  Some of them apply to any
//   cell type (such as whether the cell should have a new-line after it when
//   molded out during display of its containing array), and others have a
//   different purpose depending on what the HEARTSIGIL_BYTE is.
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


//=//// BITS 0-7: BASE FLAGS //////////////////////////////////////////////=//
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
// * SLOT_MARKED_HIDDEN -- This uses the BASE_FLAG_MARKED bit on args in
//   VarLists, corresponding to the usermode PROTECT:HIDE feature (which was
//   fairly half-baked, but preserved in Ren-C due to applicability in
//   FRAME! VarLists).
//
// * PARAM_MARKED_SEALED -- Since Param* are Slot*, this is really the same
//   thing as SLOT_MARKED_HIDDEN, but used when the particular emphasis is
//   in FRAME! ParamLists.  This is the mechanic by which AUGMENT can add
//   new parameters to a function frame that "override" existing specialized
//   parameters, potentially having the same names.
//
// * TYPE_MARKED_SPOKEN_FOR -- When parameters are optimizing the blocks they
//   receive, this is applied to any elements whose information was subsumed
//   into parameter flags or optimization bytes.  If the parameter could not
//   be fully optimized and needs to process the array, then anything with
//   this mark on it can be skipped.
//
#define CELL_FLAG_SLOT_MARKED_HIDDEN        BASE_FLAG_MARKED
#define CELL_FLAG_PARAM_MARKED_SEALED       BASE_FLAG_MARKED
#define CELL_FLAG_TYPE_MARKED_SPOKEN_FOR    BASE_FLAG_MARKED


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
// Heart and Sigil are multiplexed into a single byte, called HEARTSIGIL_BYTE.
//
// Most of the time code wants to check the Type_Of() of a cell and not it's
// Heart_Of(), as Type treats quoted/quasi/antiform cells differently.  If you
// only check Heart, then (''''x) equals (x) because both hearts are WORD!.
//
// 1. It's good to document where a Byte means a HeartsigilByte.  This doesn't
//    come up too often because most code isn't set up to handle the
//    multiplexing of the Heart byte with a Sigil, so most code should just
//    be dealing with Heart or Option(Heart).
//
// 2. There's a complex runtime check to ensure coherence in the HEARTSIGIL_BYTE
//    with the rest of the cell, which is triggered in some C++ builds when
//    you use HEARTSIGIL_BYTE() directly.  This raw accessor is used to implement
//    that layer, and can also be used for efficiency in some cases that
//    want to subvert those checks.

typedef Byte HeartsigilByte;  // help document when Byte is Heart + Sigil [1]

#define HEARTSIGIL_BYTE(cell) /* don't go through KindHolder() [2] */ \
    SECOND_BYTE(&(cell)->header.bits)

#define FLAG_HEARTSIGIL_BYTE(byte) \
    FLAG_SECOND_BYTE(exactly(int, (byte)))

#define FLAG_HEART(heart) \
    FLAG_SECOND_BYTE(ii_cast(Byte, known(Option(Heart), (heart))))

#define HEARTSIGIL_BYTEMASK_HEART  0x3F

#define CELL_MASK_HEART_NO_SIGIL \
    FLAG_HEARTSIGIL_BYTE(HEARTSIGIL_BYTEMASK_HEART)  /* minus 2 bit Sigil */

#define CELL_MASK_HEART_AND_SIGIL \
    FLAG_HEARTSIGIL_BYTE(255)


//=//// CELL 2-bit SIGIL! /////////////////////////////////////////////////=//
//
// The HEARTSIGIL_BYTE() is structured so that the top two bits of the byte are
// used for the "Sigil".  This can be [$ @ ^] or nothing.
//
// The TYPE_BYTE() values are chosen so that the non-quoted/quasi Sigilized
// value states are the 1, 2, and 3 values.
//

#define SIGIL_0 /* add safety of the Option() */ \
    u_cast(Option(Sigil), SIGIL_0_constexpr)

#define FLAG_SIGIL_CRUMB(crumb) \
    FLAG_HEARTSIGIL_BYTE(known(int, (crumb)) << BYTE_SIGIL_SHIFT)

#define FLAG_SIGIL(sigil) \
    FLAG_SIGIL_CRUMB(ii_cast(int, known(Option(Sigil), (sigil))))

#define CELL_MASK_SIGIL  FLAG_SIGIL_CRUMB(3)  // 0b11 << UINTPTR_SIGIL_SHIFT


//=//// BITS 16-23: TYPE AND QUOTE/QUASI/ANTI/DUAL BYTE ("LIFT") //////////=//
//
// A Cell's underlying "HEART" can report it as something like TYPE_WORD, but
// that is only reported as the result of Type_Of() when TYPE_BYTE() is also
// TYPE_WORD.  Higher TYPE_BYTE() will give back various TYPE_XXX answers
// corresponding to the quoted, quasiform, or antiform states.
//
// Due to how it's designed, the TYPE_BYTE() gives back the answer to what
// a Cell's Type_Of() is with a single byte read.  But different quoting and
// quasiform combinations will give distinct answers, so there is no canon
// value of TYPE_QUOTED.  This means comparing Type values directly doesn't
// work in a general sense, so the `==` and `!=` operators are disabled for
// direct Type to Type comparisons in the C++ build.
//
// Cells can be quote-escaped up to MAX_QUOTE_DEPTH_64 levels.  When quoted,
// the low bit of the lifting byte is reserved for whether the contained value
// is a quasiform...so each quoting level effectively adds 2 to the lift byte.
//
// 1. There's a complex runtime check to ensure coherence in the lift byte
//    with the rest of the cell, which is triggered in some C++ builds when
//    you use TYPE_BYTE() directly.  This raw accessor is used to implement
//    that layer, and can also be used for efficiency in some cases that
//    want to subvert those checks.
//
// 2. The reason the third byte is used for the TYPE_BYTE is so that the
//    second byte can be a zero to signal END while still being able to
//    check a byte that is multiplexed as either TYPE_BLANK for a canon END
//    that fits in a Cell or the type byte of the actual cell.  This speeds
//    up a check for either an END or a BLANK in the Stepper_Executor().  It
//    isn't fundamental but it's a nice optimization, all things being equal.

typedef Byte TypeByte;   // any byte value (but represents a Type/Lift)

#define TYPE_BYTE(cell) /* don't go through Tweak_Cell_Type_Byte() [1] */ \
    THIRD_BYTE(&(cell)->header.bits)  // third byte for g_cell_aligned_end [2]

#define Type_Of_Raw(v) \
    i_cast(Type, TYPE_BYTE(v))



#define MAX_QUOTE_DEPTH_64     64         // highest legal quoting level

#define Quote_Shift(n)      ((n) << 1)  // help find manipulation sites


#define Type_From_Heart(h) \
    Type_From_Byte(Byte_From_Heart(h))

#define FLAG_TYPE_BYTE(type) \
    FLAG_THIRD_BYTE(exactly(int, (type)))

#define FLAG_TYPE(type) \
    FLAG_THIRD_BYTE(ii_cast(Byte, known(Option(TypeEnum), (type))))

#define CELL_MASK_LIFTED_OR_ANTIFORM_OR_DUAL \
    FLAG_THIRD_BYTE(192)  // 128 + 64, the 2 high bits set

#define CELL_MASK_HEART_AND_SIGIL_AND_LIFT \
    (CELL_MASK_HEART_AND_SIGIL | FLAG_TYPE_BYTE(255))


//=//// BITS 24-31: CELL FLAGS ////////////////////////////////////////////=//
//
// Because the header for cells is only 32 bits on 32-bit platforms, there
// are only 8 bits left over when you've used up the BASE_BYTE, HEARTSIGIL_BYTE,
// and TYPE_BYTE.  These 8 scarce remaining cell bits have to be used very
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
// being CELL_FLAG_TYPE_SPECIFIC_C, as it's needed to track "vanishbility"
// of functions.
//
#define CELL_FLAG_WEIRD \
    FLAG_LEFT_BIT(25)

#define CELL_FLAG_WEIRD_VANISHABLE  CELL_FLAG_WEIRD


//=//// CELL_FLAG_FINAL ///////////////////////////////////////////////////=//
//
// Because this bit is important for "pure" functions, it needs to be copied
// so things like (x: (((final 10)))) will correctly transmit immutability
// of the 10 across any intermediary cells to reach X.  However, this finality
// is discarded when variables are fetched or picked (although the value
// itself becomes immutable, it won't create more unmodifiable variables).
//
// This puts the responsibility of stripping the bit onto the PICK-ing
// machinery.
//
#define CELL_FLAG_FINAL \
    FLAG_LEFT_BIT(26)

#define CELL_FLAG_BINDING_MUST_BE_FINAL  CELL_FLAG_FINAL


//=//// CELL_FLAG_AURA ////////////////////////////////////////////////////=//
//
// While running out of words, the "Aura" flag is a flag that's a mark on the
// Cell itself, out of band of usage by specific cell types.  It is used
// for example by object slots to indicate userlevel protection.
//
#define CELL_FLAG_AURA \
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
#define CELL_FLAG_NOTE \
    FLAG_LEFT_BIT(28)

#define CELL_FLAG_NOTE_REMOVE  CELL_FLAG_NOTE

// !!! These two should likely be unified, review the mechanics
#define CELL_FLAG_SCRATCH_VAR_NOTE_ONLY_ACTION  CELL_FLAG_NOTE
#define CELL_FLAG_CURRENT_NOTE_RUN_WORD  CELL_FLAG_NOTE


//=//// CELL_FLAG_FORMAT //////////////////////////////////////////////////=//
//
// This flag is part of CELL_MASK_PERSIST and hence is not copied by default.
//
// In Source arrays, it is used to track the newline status.  This means that
// if you overwrite a cell it will have the same newline status as the cell
// that it overwrote by default, and must be manually manipulated if that's
// not what you wanted.
//
#define CELL_FLAG_FORMAT \
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
// CELL_FLAG_LEADING_BLANK (for ANY-SEQUENCE?)
//
// 2-element sequences can be stored in an optimized form if one of the two
// elements is a SPACE.  This permits things like `/a` and `b.` to fit in
// a single cell.  It assumes that if the Stub flavor is FLAVOR_SYMBOL then
// the nonblank thing is a WORD!.
//
#define CELL_FLAG_TYPE_SPECIFIC_B \
    FLAG_LEFT_BIT(31)

#define CELL_FLAG_LEADING_BLANK   CELL_FLAG_TYPE_SPECIFIC_B  // ANY-SEQUENCE?


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


// BLANK! doesn't use its payload (or extra) for anything.
//
// That is exploited by feeds when they are variadic instead of arrays.  The
// feed cell is used to store va_list information along with a binding in
// a value cell slot.
//
// !!! Now that more than 64 types are available, it would be a lot clearer
// to make a FEED! type... but that should be unified with VARARGS! somehow.
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
    //     Index index;  // 0-based position (e.g. 0 means Rebol index 1)
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
//    C functions (Copy_Cell(), Copy_Cell_May_Bind()) so really the C++ build
//    is just helping notice if you try to use a raw byte copy.
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
        HeaderUnion track_flags;  // unique to this cell, not copied
        const char *file;  // is Byte (UTF-8), but char* for debug watch
        uintptr_t line;
        uintptr_t tick;
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
