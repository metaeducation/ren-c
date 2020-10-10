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
// CELL_FLAG_XXX so they can be used with those macros.
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

#define CELL_FLAG_MANAGED NODE_FLAG_MANAGED
#define CELL_FLAG_ROOT NODE_FLAG_ROOT

#define CELL_FLAG_ARG_MARKED_CHECKED NODE_FLAG_MARKED
#define CELL_FLAG_OUT_MARKED_STALE NODE_FLAG_MARKED
#define CELL_FLAG_VAR_MARKED_REUSE NODE_FLAG_MARKED
#define CELL_FLAG_MARKED_REMOVE NODE_FLAG_MARKED
#define CELL_FLAG_BIND_MARKED_REUSE NODE_FLAG_MARKED
#define CELL_FLAG_FETCHED_MARKED_TEMPORARY NODE_FLAG_MARKED


// v-- BEGIN GENERAL CELL BITS HERE, third byte in the header


//=//// CELL_FLAG_PROTECTED ///////////////////////////////////////////////=//
//
// Values can carry a user-level protection bit.  The bit is not copied by
// Move_Value(), and hence reading a protected value and writing it to
// another location will not propagate the protectedness from the original
// value to the copy.
//
// (Series have more than one kind of protection in "info" bits that can all
// be checked at once...hence there's not "NODE_FLAG_PROTECTED" in common.)
//
#define CELL_FLAG_PROTECTED \
    FLAG_LEFT_BIT(16)


//=//// CELL_FLAG_FIRST_IS_NODE ///////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).first.node` should be marked as a node by the GC.
//
#define CELL_FLAG_FIRST_IS_NODE \
    FLAG_LEFT_BIT(17)


//=//// CELL_FLAG_SECOND_IS_NODE //////////////////////////////////////////=//
//
// This flag is used on cells to indicate that they use the "Any" Payload,
// and `PAYLOAD(Any, v).second.node` should be marked as a node by the GC.
//
#define CELL_FLAG_SECOND_IS_NODE \
    FLAG_LEFT_BIT(18)


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
    FLAG_LEFT_BIT(19)


//=//// CELL_FLAG_20 //////////////////////////////////////////////////////=//
//
// Reserved for future use... however for the moment it is used to mark
// partial refinements, see usage in %c-special.c.  (This could probably
// multplex with some other bit.)
//
#define CELL_FLAG_20 \
    FLAG_LEFT_BIT(20)
#define CELL_FLAG_PUSH_PARTIAL \
    FLAG_LEFT_BIT(20)


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
    FLAG_LEFT_BIT(21)


//=//// CELL_FLAG_CONST ///////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any series or data it points
// to, regardless of whether that data is in a locked series or not.  It is
// possible to get a mutable view on a const value by using MUTABLE, and a
// const view on a mutable value with CONST.
//
#define CELL_FLAG_CONST \
    FLAG_LEFT_BIT(22)  // NOTE: Must be SAME BIT as FEED_FLAG_CONST


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
    FLAG_LEFT_BIT(23)


// After 8 bits for node flags, 8 bits for the datatype, and 8 generic value
// bits...there's only 8 more bits left on 32-bit platforms in the header.
//
// !!! This is slated for an interesting feature of fitting an immutable
// single element array into a cell.  The proposal is called "mirror bytes".

#define FLAG_MIRROR_BYTE(b)         FLAG_FOURTH_BYTE(b)
#define MIRROR_BYTE(v)              FOURTH_BYTE((v)->header)
#define mutable_MIRROR_BYTE(v)      mutable_FOURTH_BYTE((v)->header)


// Endlike headers have the second byte clear (to pass the IS_END() test).
// But they also have leading bits `10` so they don't look like a UTF-8
// string, and don't have NODE_FLAG_CELL set to prevents writing to them.
//
// !!! One must be careful in reading and writing bits initialized via
// different structure types.  As it is, setting and testing for ends is done
// with `unsigned char*` access of a whole byte, so it is safe...but there
// are nuances to be aware of:
//
// https://stackoverflow.com/q/51846048
//
inline static union Reb_Header Endlike_Header(uintptr_t bits) {
    assert(
        0 == (bits & (
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
            | FLAG_SECOND_BYTE(255)
        ))
    );
    union Reb_Header h;
    h.bits = bits | NODE_FLAG_NODE;
    return h;
}


//=//// CELL RESET AND COPY MASKS /////////////////////////////////////////=//
//
// It's important for operations that write to cells not to overwrite *all*
// the bits in the header, because some of those bits give information about
// the nature of the cell's storage and lifetime.  Similarly, if bits are
// being copied from one cell to another, those header bits must be masked
// out to avoid corrupting the information in the target cell.
//
// !!! In the future, the 64-bit build may put the integer stack level of a
// cell in the header--which would be part of the cell's masked out format.
//
// Additionally, operations that copy need to not copy any of those bits that
// are owned by the cell, plus additional bits that would be reset in the
// cell if overwritten but not copied.  For now, this is why `foo: :+` does
// not make foo an enfixed operation.
//
// Note that this will clear NODE_FLAG_FREE, so it should be checked by the
// debug build before resetting.
//
// Note also that NODE_FLAG_MARKED usage is a relatively new concept, e.g.
// to allow REMOVE-EACH to mark values in a locked series as to which should
// be removed when the enumeration is finished.  This *should* not be able
// to interfere with the GC, since userspace arrays don't use that flag with
// that meaning, but time will tell if it's a good idea to reuse the bit.
//

#define CELL_MASK_PERSIST \
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_MANAGED | NODE_FLAG_ROOT)

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | NODE_FLAG_MARKED | CELL_FLAG_PROTECTED \
        | CELL_FLAG_UNEVALUATED)


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

struct Reb_Binding_Extra  // see %sys-bind.h
{
    REBNOD* node;
};

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
    REBNOD *node;

    // The GC is only marking one field in the union...the node.  So that is
    // the only field that should be assigned and read.  These "type puns"
    // are unreliable, and for debug viewing only--in case they help.
    //
  #if defined(DEBUG_USE_UNION_PUNS)
    REBSER *rebser_pun;
    REBVAL *rebval_pun;
  #endif
};

union Reb_Bytes_Extra {
    REBYTE exactly_4[sizeof(uint32_t) * 1];
    REBYTE at_least_4[sizeof(void*) * 1];
};

#define IDX_EXTRA_USED 0  // index into exactly_4 when used for in cell storage
#define IDX_EXTRA_LEN 1  // index into exactly_4 when used for in cell storage

union Reb_Value_Extra { //=/////////////////// ACTUAL EXTRA DEFINITION ////=//

    struct Reb_Character_Extra Character;
    struct Reb_Binding_Extra Binding;
    struct Reb_Datatype_Extra Datatype;
    struct Reb_Date_Extra Date;
    struct Reb_Typeset_Extra Typeset;

    union Reb_Any Any;
    union Reb_Bytes_Extra Bytes;

  #if defined(DEBUG_COUNT_TICKS)
    //
    // A tick field is included in all debug builds, not just those which
    // DEBUG_TRACK_CELLS...because negative signs are used to give a distinct
    // state to unreadable voids.  See %sys-track.h and %sys-void.h
    //
    intptr_t tick;  // Note: will be negative for unreadable voids
  #endif

    // The release build doesn't put anything in the ->extra field by default,
    // so sensitive compilers notice when cells are moved without that
    // initialization.  Rather than disable the warning, this can be used to
    // put some junk into it, but TRASH_POINTER_IF_DEBUG() won't subvert the
    // warning.  So just poke whatever pointer is at hand that is likely to
    // already be in a register and not meaningful (e.g. nullptr is a poor
    // choice, because that could look like a valid non-binding)
    //
    void *trash;
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

#if defined(DEBUG_TRACK_CELLS)
    struct Reb_Track_Payload  // see %sys-track.h
    {
        const char *file;  // is REBYTE (UTF-8), but char* for debug watch
        int line;
    };
#endif

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

  #if defined(DEBUG_TRACK_CELLS) && !defined(DEBUG_TRACK_EXTEND_CELLS)
    //
    // Debug builds put the file and line number of initialization for a cell
    // into the payload.  It will remain there after initialization for types
    // that do not need a payload (NULL, VOID!, BLANK!, LOGIC!).  See the
    // DEBUG_TRACK_EXTEND_CELLS option for tracking even types with payloads,
    // and also see TOUCH_CELL() for how to update tracking at runtime.
    //
    struct Reb_Track_Payload Track;
  #endif

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
// instead.  (See: Move_Value(), Blit_Cell(), Derelativize())
//
// Note: It is annoying that this means any structure that embeds a value cell
// cannot be assigned.  However, `struct Reb_Value` must be the type exported
// in both C and C++ under the same name and bit patterns.  Pretty much any
// attempt to work around this and create a base class that works in C too
// (e.g. Reb_Cell) would wind up violating strict aliasing.  Think *very hard*
// before changing!
//

#if defined(CPLUSPLUS_11)
    struct alignas(ALIGN_SIZE) Reb_Cell
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
        // Lets you preserve the tracking info even if the cell has a payload.
        // This doubles the cell size, but can be a very helpful debug option.
        //
        struct Reb_Track_Payload track;
        uintptr_t tick; // stored in the Reb_Value_Extra for basic tracking
        uintptr_t touch; // see TOUCH_CELL(), pads out to 4 * sizeof(void*)
      #endif

      #ifdef CPLUSPLUS_11
      public:
        Reb_Cell () = default;
      private:
        Reb_Cell (Reb_Cell const & other) = delete;
        void operator= (Reb_Cell const &rhs) = delete;
      #endif
    };

#ifdef CPLUSPLUS_11
    //
    // A Reb_Relative_Value is a point of view on a cell where VAL_TYPE() can
    // be called and will always give back a value in range < REB_MAX.  All
    // KIND_BYTE() > REB_64 are considered to be REB_QUOTED variants of the
    // byte modulo 64.
    //
    struct Reb_Relative_Value : public Reb_Cell {};
#endif


#define PAYLOAD(Type, v) \
    (v)->payload.Type

#define EXTRA(Type, v) \
    (v)->extra.Type


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


// !!! Consider a more sophisticated macro/template, like in DEBUG_CHECK_CASTS
// though this is good enough for many usages for now.

#if !defined(CPLUSPLUS_11)
    #define VAL(p) \
        cast(RELVAL*, (p))
#else
    inline static REBVAL* VAL(void *p)
      { return cast(REBVAL*, p); }

    inline static const REBVAL* VAL(const void *p)
      { return cast(const REBVAL*, p); }
#endif
