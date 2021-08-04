//
//  File: %sys-rebval.h
//  Summary: {any-value! defs BEFORE %tmp-internals.h (see: %sys-value.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2019 Ren-C Open Source Contributors
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
// REBVAL is the structure/union for all Rebol values. It's designed to be
// four C pointers in size (so 16 bytes on 32-bit platforms and 32 bytes
// on 64-bit platforms).  Operation will be most efficient with those sizes,
// and there are checks on boot to ensure that `sizeof(REBVAL)` is the
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
// explanation of REBSER, REBARR, REBCTX, and REBMAP.)
//
// So the next part of the structure is the "Extra".  This is the size of one
// pointer, which sits immediately after the header (that's also the size of
// one pointer).  For built-in types this can carry instance data for the
// value--such as a binding, or extra bits for a fixed-point decimal.  But
// since all extension types have the same identification (REB_CUSTOM), this
// cell slot must be yielded for a pointer to the real type information.
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


#define CELL_MASK_NONE 0

// The GET_CELL_FLAG()/etc. macros splice together CELL_FLAG_ with the text
// you pass in (token pasting).  Since it does this, alias NODE_FLAG_XXX to
// CELL_FLAG_XXX so they can be used with those macros.  MARKED is kept in
// the name to stress you can't have more than one use in effect at a time...
// so you must know what kind of cell you are dealing with and that it won't
// conflict with other uses.
//
// IMPORTANT: The marked flag is a property of the cell location and not of
// the value...so writing a new value into the cell will not update the
// status of its mark.  It must be manually turned off once turned on, or
// the cell must be reformatted entirely with Prep_Cell().
//
// * VAR_MARKED_HIDDEN -- This uses the NODE_FLAG_MARKED bit on args in
//   action frames, and in particular specialization uses it to denote which
//   arguments in a frame are actually specialized.  This helps notice the
//   difference during an APPLY of encoded partial refinement specialization
//   encoding from just a user putting random values in a refinement slot.
//
// **IMPORTANT**: This means that a routine being passed an arbitrary value
//   should not make assumptions about the marked bit.  It should only be
//   used in circumstances where some understanding of being "in control"
//   of the bit are in place--like processing an array a routine itself made.
//

#define CELL_FLAG_MANAGED NODE_FLAG_MANAGED
#define CELL_FLAG_ROOT NODE_FLAG_ROOT

#define CELL_FLAG_VAR_MARKED_HIDDEN NODE_FLAG_MARKED


//=//// CELL_FLAG_FIRST_IS_NODE ///////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).first.node` should be marked as a node by the GC.
//
#define CELL_FLAG_FIRST_IS_NODE \
    NODE_FLAG_GC_ONE


//=//// CELL_FLAG_SECOND_IS_NODE //////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).second.node` should be marked as a node by the GC.
//
#define CELL_FLAG_SECOND_IS_NODE \
    NODE_FLAG_GC_TWO


//=//// BITS 16-23: CELL LAYOUT BYTE ("HEART") ////////////////////////////=//
//
// The heart byte corresponds to the actual bit layout of the cell; it's what
// the GC marks a cell as.  The CELL_HEART() will often match the CELL_KIND(),
// but won't in cases where the KIND is REB_PATH but the HEART is REB_BLOCK...
// indicating that the path is using the underlying implementation of a block.
//

#define FLAG_HEART_BYTE(b)         FLAG_THIRD_BYTE(b)
#define HEART_BYTE(v)              THIRD_BYTE((v)->header)
#define mutable_HEART_BYTE(v)      mutable_THIRD_BYTE((v)->header)


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


//=//// CELL_FLAG_ISOTOPE //////////////////////////////////////////////////=//
//
// This flag is currently for BAD-WORD!.  If bad words like ~void~ or ~none~
// do not carry the flag, then they act like normal values...and can appear
// in blocks, and be handled like anything else.  But if the ISOTOPE flag is
// set, then BAD-WORD! values in variables will return errors.  There will
// also be other "mystery" properties of decaying or acting in ways that are
// specific to particular BAD-WORD!s if it's an isotope.  e.g. ~null~ isotopes
// will turn into NULL when assigned to variables, and are also falsey.
//
// !!! Odds are that this should be unified with CELL_FLAG_UNEVALUATED, to
// save a bit...although it's in the reverse sense.
//
// !!! Thought: Could FRAME! use this bit to encode when the frame is actually
// a frame for the IDENTITY, and the value is just something that came from
// `make frame! ^ 10` or similar?  It would weave a bit of a tangled web for
// things like `action of f` for such frames.  Still might be worth it to
// lie, and that way you could make ACTION!s and FRAME!s which were really
// just QUOTED!s under the hood.
//
#define CELL_FLAG_ISOTOPE \
    FLAG_LEFT_BIT(25)


//=//// CELL_FLAG_26 //////////////////////////////////////////////////////=//
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
#define CELL_FLAG_UNEVALUATED \
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
// * OUT_NOTE_STALE -- This application of CELL_FLAG_NOTE helps show
//   when an evaluation step didn't add any new output, but it does not
//   overwrite the contents of the out cell.  This allows the evaluator to
//   leave a value in the output slot even if there is trailing invisible
//   evaluation to be done, such as in `[1 + 2 elide (print "Hi")]`, where
//   something like ALL would want to hold onto the 3 without needing to
//   cache it in some other location.  Stale out cells cannot be used as
//   left side input for enfix.
//
// * STACK_NOTE_SEALED -- When building exemplar frames on the stack, you want
//   to observe when a value should be marked as VAR_MARKED_HIDDEN.  But you
//   aren't allowed to write "sticky" cell format bits on stack elements.  So
//   the more ephemeral "note" is used on the stack element and then changed
//   to the sticky flag on the paramlist when popping.
//
// * STACK_NOTE_CIRCLED - When doing multi-returns, this marks which stack
//   element representing a return slot should act as the main return value.
//   (It could be done with a DSP position, but this thinks ahead to stackless,
//   so storing in the data stack makes it survive the evaluation.)
//

#define CELL_FLAG_NOTE \
    FLAG_LEFT_BIT(28)

#define CELL_FLAG_OUT_NOTE_STALE CELL_FLAG_NOTE
#define CELL_FLAG_NOTE_REMOVE CELL_FLAG_NOTE
#define CELL_FLAG_BIND_NOTE_REUSE CELL_FLAG_NOTE
#define CELL_FLAG_STACK_NOTE_SEALED CELL_FLAG_NOTE
#define CELL_FLAG_STACK_NOTE_CIRCLED CELL_FLAG_NOTE


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
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_MANAGED | NODE_FLAG_ROOT \
        | NODE_FLAG_MARKED | CELL_FLAG_PROTECTED)

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | CELL_FLAG_NOTE | CELL_FLAG_UNEVALUATED)

#define CELL_MASK_ALL \
    ~cast(REBFLGS, 0)

#define CELL_MASK_POISON \
    (FLAG_KIND3Q_BYTE(REB_T_UNSAFE) | FLAG_HEART_BYTE(REB_T_UNSAFE))


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
// See Reb_Integer, Reb_Decimal, and Reb_Typeset for examples where the 64-bit
// quantity requires things like REBDEC to have 64-bit alignment.  At time of
// writing, this is necessary for the "C-to-Javascript" emscripten build to
// work.  It's also likely preferred by x86.
//

struct Reb_Character_Extra { REBUNI codepoint; };  // see %sys-char.h

struct Reb_Datatype_Extra  // see %sys-datatype.h
{
    enum Reb_Kind kind;
};

struct Reb_Date_Extra  // see %sys-time.h
{
    REBYMD ymdz;  // month/day/year/zone (time payload *may* hold nanoseconds)
};

struct Reb_Typeset_Extra  // see %sys-typeset.h
{
    uint_fast32_t high_bits;  // 64 typeflags, can't all fit in payload second
};

union Reb_Any {  // needed to beat strict aliasing, used in payload
    bool flag;  // "wasteful" to just use for one flag, but fast to read/write

    intptr_t i;
    int_fast32_t i32;

    uintptr_t u;
    uint_fast32_t u32;

    REBD32 d32;  // 32-bit float not in C standard, typically just `float`

    void *p;
    CFUNC *cfunc;  // C function/data pointers pointers may differ in size

    // This is not legal to use in an EXTRA(), only the `PAYLOAD().first` slot
    // (and perhaps in the future, the payload second slot).  If you do use
    // a node in the cell, be sure to set CELL_FLAG_FIRST_IS_NODE!
    //
    // No REBNODs (REBSER or REBVAL) are ever actually declared const, but
    // care should be taken on extraction to give back a `const` reference
    // if the intent is immutability, or a conservative state of possible
    // immutability (e.g. the CONST usermode status hasn't been checked)
    //
    const REBNOD *node;

    // The GC is only marking one field in the union...the node.  So that is
    // the only field that should be assigned and read.  These "type puns"
    // are unreliable, and for debug viewing only--in case they help.
    //
  #if defined(DEBUG_USE_UNION_PUNS)
    REBSER *rebser_pun;
    REBVAL *rebval_pun;
  #endif

    void *trash;  // see remarks in ZERO_UNUSED_CELL_FIELDS regarding this
};

union Reb_Bytes_Extra {
    REBYTE exactly_4[sizeof(uint32_t) * 1];
    REBYTE at_least_4[sizeof(void*) * 1];
};

#define IDX_EXTRA_USED 0  // index into exactly_4 when used for in cell storage
#define IDX_EXTRA_LEN 1  // index into exactly_4 when used for in cell storage

union Reb_Value_Extra { //=/////////////////// ACTUAL EXTRA DEFINITION ////=//

    struct Reb_Character_Extra Character;
    const REBNOD *Binding;  // see %sys-bind.h
    struct Reb_Datatype_Extra Datatype;
    struct Reb_Date_Extra Date;
    struct Reb_Typeset_Extra Typeset;

    union Reb_Any Any;
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
// So for custom types, use the correct union field in Reb_Custom_Payload,
// and only read back from the exact field written to.
//

struct Reb_Logic_Payload { bool flag; };  // see %sys-logic.h

struct Reb_Character_Payload {  // see %sys-char.h
    REBYTE size_then_encoded[8];
};

struct Reb_Integer_Payload { REBI64 i64; };  // see %sys-integer.h

struct Reb_Decimal_Payload { REBDEC dec; };  // see %sys-decimal.h

struct Reb_Time_Payload {  // see %sys-time.h
    REBI64 nanoseconds;
};

struct Reb_Any_Payload  // generic, for adding payloads after-the-fact
{
    union Reb_Any first;
    union Reb_Any second;
};

union Reb_Bytes_Payload  // IMPORTANT: Do not cast, use `Pointers` instead
{
    REBYTE exactly_8[sizeof(uint32_t) * 2];  // same on 32-bit/64-bit platforms
    REBYTE at_least_8[sizeof(void*) * 2];  // size depends on platform
};

// COMMA! is evaluative, but you wouldn't usually think of it as being
// bindable because of its "inert-seeming" content.  To make the ANY_INERT()
// test fast, REB_COMMA is pushed to a high value, making it bindable.  That
// is exploited by feeds, which use it to store va_list information along
// with a specifier in a value cell slot.  (Most commas don't have this.)
//
struct Reb_Comma_Payload {
    // A frame may be sourced from a va_list of pointers, or not.  If this is
    // NULL it is assumed that the values are sourced from a simple array.
    //
    va_list* vaptr;  // may be nullptr

    // The feed could also be coming from a packed array of pointers...this
    // is used by the C++ interface, which creates a `std::array` on the
    // C stack of the processed variadic arguments it enumerated.
    //
    const void* const* packed;
};

union Reb_Value_Payload { //=/////////////// ACTUAL PAYLOAD DEFINITION ////=//

    // Due to strict aliasing, if a routine is going to generically access a
    // node (e.g. to exploit common checks for mutability) it has to do a
    // read through the same field that was assigned.  Hence, many types
    // whose payloads are nodes use the generic "Any" payload, which is
    // two separate variant fields.  If CELL_FLAG_FIRST_IS_NODE is set, then
    // if that is a series node it will be used to answer questions about
    // mutability (beyond CONST, which the cell encodes itself)
    //
    // ANY-WORD!  // see %sys-word.h
    //     REBSTR *spelling;  // word's non-canonized spelling, UTF-8 string
    //     REBINT index;  // index of word in context (if binding is not null)
    //
    // ANY-CONTEXT!  // see %sys-context.h
    //     REBARR *varlist;  // has MISC.meta, LINK.keysource
    //     REBACT *phase;  // used by FRAME! contexts, see %sys-frame.h
    //
    // ANY-SERIES!  // see %sys-series.h
    //     REBSER *rebser;  // vector/double-ended-queue of equal-sized items
    //     REBLEN index;  // 0-based position (e.g. 0 means Rebol index 1)
    //
    // QUOTED!  // see %sys-quoted.h
    //     REBVAL *paired;  // paired value handle
    //     REBLEN depth;  // how deep quoting level is (> 3 if payload needed)
    //
    // ACTION!  // see %sys-action.h
    //     REBARR *paramlist;  // has MISC.meta, LINK.underlying
    //     REBARR *details;  // has MISC.dispatcher, LINK.specialty
    //
    // VARARGS!  // see %sys-varargs.h
    //     REBINT signed_param_index;  // if negative, consider arg enfixed
    //     REBACT *phase;  // where to look up parameter by its offset

    struct Reb_Any_Payload Any;

    struct Reb_Logic_Payload Logic;
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
// This bundles up the cell into a structure.  The C++ build includes some
// special checks to make sure that overwriting one cell with another can't
// be done with direct assignment, such as `*dest = *src;`  Cells contain
// formatting bits that must be preserved, and some flag bits shouldn't be
// copied. (See: CELL_MASK_PRESERVE)
//
// Also, copying needs to be sensitive to the target slot.  If that slot is
// at a higher stack level than the source (or persistent in an array) then
// special handling is necessary to make sure any stack constrained pointers
// are "reified" and visible to the GC.
//
// Goal is that the mechanics are managed with low-level C, so the C++ build
// is just there to notice when you try to use a raw byte copy.  Use functions
// instead.  (See: Copy_Cell(), Derelativize())
//
// Note: It is annoying that this means any structure that embeds a value cell
// cannot be assigned.  However, `struct Reb_Value` must be the type exported
// in both C and C++ under the same name and bit patterns.  Pretty much any
// attempt to work around this and create a base class that works in C too
// (e.g. Reb_Cell) would wind up violating strict aliasing.  Think *very hard*
// before changing!
//

#if defined(CPLUSPLUS_11)
    struct alignas(ALIGN_SIZE) Reb_Cell : public Reb_Node
#elif defined(C_11)
    struct alignas(ALIGN_SIZE) Reb_Value
#else
    struct Reb_Value  // ...have to just hope the alignment "works out"
#endif
    {
        union Reb_Header header;
        union Reb_Value_Extra extra;
        union Reb_Value_Payload payload;

      #if defined(DEBUG_TRACK_EXTEND_CELLS)
        //
        // This doubles the cell size, but is a *very* helpful debug option.
        // See %sys-track.h for explanation.
        //
        const char *file;  // is REBYTE (UTF-8), but char* for debug watch
        uintptr_t line;
        uintptr_t tick;  // stored in the Reb_Value_Extra for basic tracking
        uintptr_t touch;  // see TOUCH_CELL(), pads out to 4 * sizeof(void*)
      #endif
    };

#ifdef CPLUSPLUS_11
    //
    // A Reb_Relative_Value is a point of view on a cell where VAL_TYPE() can
    // be called and will always give back a value in range < REB_MAX.  All
    // KIND3Q_BYTE() > REB_64 are considered to be REB_QUOTED variants of the
    // byte modulo 64.
    //
    struct Reb_Relative_Value : public Reb_Cell {
      #ifdef CPLUSPLUS_11
      public:
        Reb_Relative_Value () = default;
      private:
        Reb_Relative_Value (Reb_Relative_Value const & other) = delete;
        void operator= (Reb_Relative_Value const &rhs) = delete;
      #endif
    };
#endif


#define PAYLOAD(Type, v) \
    (v)->payload.Type

#define EXTRA(Type, v) \
    (v)->extra.Type

#define mutable_BINDING(v) \
    (v)->extra.Binding

#define BINDING(v) \
    x_cast(REBSER*, m_cast(REBNOD*, (v)->extra.Binding))


//=////////////////////////////////////////////////////////////////////////=//
//
//  RELATIVE AND SPECIFIC VALUES (difference enforced in C++ build only)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A RELVAL is an equivalent struct layout to to REBVAL, but is allowed to
// have a REBACT* as its binding.  A relative value pointer can point to a
// specific value, but a relative word or array cannot be pointed to by a
// plain REBVAL*.  The RELVAL-vs-REBVAL distinction is purely commentary
// in the C build, but the C++ build makes REBVAL a type derived from RELVAL.
//
// RELVAL exists to help quarantine the bit patterns for relative words into
// the deep-copied-body of the function they are for.  To actually look them
// up, they must be paired with a FRAME! matching the actual instance of the
// running function on the stack they correspond to.  Once made specific,
// a word may then be freely copied into any REBVAL slot.
//
// In addition to ANY-WORD!, an ANY-ARRAY! can also be relative, if it is
// part of the deep-copied function body.  The reason that arrays must be
// relative too is in case they contain relative words.  If they do, then
// recursion into them must carry forward the resolving "specifier" pointer
// to be combined with any relative words that are seen later.
//

#ifdef CPLUSPLUS_11
    static_assert(
        std::is_standard_layout<struct Reb_Relative_Value>::value,
        "C++ RELVAL must match C layout: http://stackoverflow.com/a/7189821/"
    );

    struct Reb_Value : public Reb_Relative_Value
    {
      #if !defined(NDEBUG)
        Reb_Value () = default;
        ~Reb_Value () {
            assert(this->header.bits & (NODE_FLAG_NODE | NODE_FLAG_CELL));
        }
      #endif
    };

    static_assert(
        std::is_standard_layout<struct Reb_Value>::value,
        "C++ REBVAL must match C layout: http://stackoverflow.com/a/7189821/"
    );
#endif



//=//// VARS and PARAMs ///////////////////////////////////////////////////=//
//
// These are lightweight classes on top of cells that help catch cases of
// testing for flags that only apply if you're sure something is a parameter
// cell or variable cell.
//

#ifdef CPLUSPLUS_11
    struct REBVAR : public REBVAL {};
    struct REBPAR : public REBVAR {};

    inline static const REBPAR* cast_PAR(const REBVAL *v)
        { return cast(const REBPAR*, v); }

    inline static REBPAR* cast_PAR(REBVAL *v)
        { return cast(REBPAR*, v); }
#else
    #define REBVAR REBVAL
    #define REBPAR REBVAL

    #define cast_PAR(v) (v)
#endif
