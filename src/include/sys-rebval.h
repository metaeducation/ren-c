//
//  File: %sys-rebval.h
//  Summary: {any-value! defs BEFORE %tmp-internals.h (see: %sys-value.h)}
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
// of a block).  Followed by that are bits which are custom to each type (for
// instance whether a key in an object is hidden or not).
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
// one pointer).
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



// v-- BEGIN GENERAL CELL BITS HERE, third byte in the header


//=//// CELL_FLAG_PROTECTED ///////////////////////////////////////////////=//
//
// Values can carry a user-level protection bit.  The bit is not copied by
// Move_Value(), and hence reading a protected value and writing it to
// another location will not propagate the protectedness from the original
// value to the copy.
//
// This is called a CELL_FLAG and not a VALUE_FLAG because any formatted cell
// can be tested for it, even if it is "trash".  This means writing routines
// that are putting data into a cell for the first time can check the bit.
// (Series, having more than one kind of protection, put those bits in the
// "info" so they can all be checked at once...otherwise there might be a
// shared NODE_FLAG_PROTECTED in common.)
//
#define CELL_FLAG_PROTECTED \
    FLAG_LEFT_BIT(16)


//=//// VALUE_FLAG_FALSEY /////////////////////////////////////////////////=//
//
// This flag is used as a quick cache on NULL, BLANK! or LOGIC! false values.
// These are the only three values that return true from the NOT native
// (a.k.a. "conditionally false").  All other types return true from TO-LOGIC
// or its synonym, "DID".
//
// (It is also placed on END cells and TRASH cells, to speed up the VAL_TYPE()
// check for finding illegal types...by only checking falsey types.)
//
// Because of this cached bit, LOGIC! does not need to store any data in its
// payload... its data of being true or false is already covered by this
// header bit.
//
#define VALUE_FLAG_FALSEY \
    FLAG_LEFT_BIT(17)


//=//// VALUE_FLAG_NEWLINE_BEFORE /////////////////////////////////////////=//
//
// When the array containing a value with this flag set is molding, that will
// output a new line *before* molding the value.  This flag works in tandem
// with a flag on the array itself which manages whether there should be a
// newline output before the closing array delimiter: ARRAY_FLAG_TAIL_NEWLINE.
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
#define VALUE_FLAG_NEWLINE_BEFORE \
    FLAG_LEFT_BIT(18)


//=//// VALUE_FLAG_UNEVALUATED ////////////////////////////////////////////=//
//
// Some functions wish to be sensitive to whether or not their argument came
// as a literal in source or as a product of an evaluation.  While all values
// carry the bit, it is only guaranteed to be meaningful on arguments in
// function frames...though it is valid on any result at the moment of taking
// it from Eval_Core_Throws().
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
#define VALUE_FLAG_UNEVALUATED \
    FLAG_LEFT_BIT(19)


//=//// VALUE_FLAG_ENFIXED ////////////////////////////////////////////////=//
//
// In Ren-C, there is only one kind of function (ACTION!).  But it's possible
// to tag a function value cell in a context as being "enfixed", hence it
// will acquire its first argument from the left.  See SET/ENFIX and ENFIX.
//
// The reasion it is a generic VALUE_FLAG_XXX and not an ACTION_FLAG_XXX is
// so that it can be dealt with without specifically knowing that the cell
// involved is an action.  One benefit is that testing for an enfix action
// can be done just by looking at this bit--since only actions have it set.
//
// But also, this bit is not copied by Move_Value.  As a result, if you say
// something like `foo: :+`, foo will contain the non-enfixed form of the
// function.  To do that would require more nuance in Move_Value if it were
// an ACTION_FLAG_XXX, testing for action-ness vs. just masking it out.
//
#define VALUE_FLAG_ENFIXED \
    FLAG_LEFT_BIT(20)


//=//// VALUE_FLAG_EVAL_FLIP //////////////////////////////////////////////=//
//
// This is a bit which should not be present on cells in user-exposed arrays.
//
// If a DO is happening with DO_FLAG_EXPLICIT_EVALUATE, only values which
// carry this bit will override it.  It may be the case that the flag on a
// value would signal a kind of quoting to suppress evaluation in ordinary
// evaluation (without DO_FLAG_EXPLICIT_EVALUATE), hence it is a "flip" bit.
//
#define VALUE_FLAG_EVAL_FLIP \
    FLAG_LEFT_BIT(21) // IMPORTANT: Same bit as DO_FLAG_EXPLICIT_EVALUATE


//=//// VALUE_FLAG_CONST //////////////////////////////////////////////////=//
//
// A value that is CONST has read-only access to any series or data it points
// to, regardless of whether that data is in a locked series or not.  It is
// possible to get a mutable view on a const value by using MUTABLE, and a
// const view on a mutable value with CONST.
//
#define VALUE_FLAG_CONST \
    FLAG_LEFT_BIT(22) // NOTE: Must be SAME BIT as DO_FLAG_CONST


//=//// VALUE_FLAG_EXPLICITLY_MUTABLE /////////////////////////////////////=//
//
// While it may seem that a mutable value would be merely one that did not
// carry VALUE_FLAG_CONST, there's a need for a separate bit to indicate when
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
#define VALUE_FLAG_EXPLICITLY_MUTABLE \
    FLAG_LEFT_BIT(23)


// ^-- STOP BEFORE TYPE_SPECIFIC_BIT
//
// After 8 bits for node flags, 8 bits for the datatype, and 8 generic value
// bits...there's only 8 more bits left on 32-bit platforms in the header.
// Those are used for flags that are sensitive to the datatype.
//
#define TYPE_SPECIFIC_BIT (24)
STATIC_ASSERT(23 < TYPE_SPECIFIC_BIT);


// v-- BEGIN PER-TYPE CUSTOM BITS HERE, fourth byte in the header

#define CUSTOM_BYTE(v) \
    FOURTH_BYTE((v)->header)

#define mutable_CUSTOM_BYTE(v) \
    mutable_FOURTH_BYTE((v)->header)


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


//=////////////////////////////////////////////////////////////////////////=//
//
//  Cell Reset and Copy Masks
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's important for operations that write to cells not to overwrite *all*
// the bits in the header, because some of those bits give information about
// the nature of the cell's storage and lifetime.  Similarly, if bits are
// being copied from one cell to another, those header bits must be masked
// out to avoid corrupting the information in the target cell.
//
// !!! Future optimizations may put the integer stack level of the cell in
// the header in the unused 32 bits for the 64-bit build.  That would also
// be kept in this mask.
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
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_MANAGED | NODE_FLAG_ROOT \
        | CELL_FLAG_TRANSIENT | CELL_FLAG_STACK)

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | NODE_FLAG_MARKED | CELL_FLAG_PROTECTED \
        | VALUE_FLAG_ENFIXED | VALUE_FLAG_UNEVALUATED | VALUE_FLAG_EVAL_FLIP)


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACK payload (not a value type, only in DEBUG)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// `Reb_Track_Payload` is the value payload in debug builds for any REBVAL
// whose VAL_TYPE() doesn't need any information beyond the header.  This
// offers a chance to inject some information into the payload to help
// know where the value originated.  It is used by NULL cells, VOID!, BLANK!,
// LOGIC!, and BAR!.
//
// In addition to the file and line number where the assignment was made,
// the "tick count" of the DO loop is also saved.  This means that it can
// be possible in a repro case to find out which evaluation step produced
// the value--and at what place in the source.  Repro cases can be set to
// break on that tick count, if it is deterministic.
//
// If tracking information is desired for all cell types, that means the cell
// size has to be increased.  See DEBUG_TRACK_EXTEND_CELLS for this setting,
// which can be useful in extreme debugging cases.
//

#if defined(DEBUG_TRACK_CELLS)
    struct Reb_Track_Payload {
        const char *file; // is REBYTE (UTF-8), but char* for debug watch
        int line;
    };
#endif

struct Reb_Datatype_Payload {
    enum Reb_Kind kind;
    REBARR *spec;
};

// !!! In R3-alpha, the money type was implemented under a type called "deci".
// The payload for a deci was more than 64 bits in size, which meant it had
// to be split across the separated union components in Ren-C.  (The 64-bit
// aligned "payload" and 32-bit aligned "extra" were broken out independently,
// so that setting one union member would not disengage the other.)

struct Reb_Money_Payload {
    unsigned m1:32; /* significand, continuation */
    unsigned m2:23; /* significand, highest part */
    unsigned s:1;   /* sign, 0 means nonnegative, 1 means nonpositive */
    int e:8;        /* exponent */
};


// The same payload is used for TIME! and DATE!.  The extra bits needed by
// DATE! (as REBYMD) fit into 32 bits, so can live in the ->extra field,
// which is the size of a platform pointer.
//
struct Reb_Time_Payload {
    REBI64 nanoseconds;
};

typedef struct Reb_Tuple_Payload {
    REBYTE tuple[8];
} REBTUP;


struct Reb_Quoted_Payload {
    //
    // It's necessary to create some storage outside the value--at least for
    // some levels of depth--because the cell itself can't be big enough to
    // hold any other cell (by definition!).  Cell pointer cached for speed.
    //
    RELVAL *cell; // lives in singular array, find with Singular_From_Cell()

    // The depth is the number of apostrophes, e.g. ''''X is a depth of 4.
    // It is stored in the cell payload and not the MISC() or LINK() of the
    // singular, so that when you add or remove quote levels to the same value
    // a new series isn't required...the cell just has a different count.
    //
    REBCNT depth;
};


struct Reb_Series_Payload {
    REBSER *series; // vector of equal-sized items, see %sys-series.h

    // `index` is the 0-based position into the series represented by this
    // ANY-VALUE! (so if it is 0 then that means a Rebol index of 1).
    //
    // It is possible that the index could be to a point beyond the range of
    // the series.  This is intrinsic, because the series can be modified
    // through other values and not update the others referring to it.  Hence
    // VAL_INDEX() must be checked, or the routine called with it must.
    //
    // !!! Review that it doesn't seem like these checks are being done
    // in a systemic way.  VAL_LEN_AT() bounds the length at the index
    // position by the physical length, but VAL_ARRAY_AT() doesn't check.
    //
    REBCNT index;
};

struct Reb_Typeset_Payload {
    REBU64 bits; // One bit for each DATATYPE! (use with FLAGIT_KIND)
};


struct Reb_Word_Payload {
    //
    // This is the word's non-canonized spelling.  It is a UTF-8 string.
    //
    REBSTR *spelling;

    // Index of word in context (if word is bound, e.g. `binding` is not null)
    //
    // !!! Intended logic is that if the index is positive, then the word
    // is looked for in the context's pooled memory data pointer.  If the
    // index is negative or 0, then it's assumed to be a stack variable,
    // and looked up in the call's `stackvars` data.
    //
    // But now there are no examples of contexts which have both pooled
    // and stack memory, and the general issue of mapping the numbers has
    // not been solved.  However, both pointers are available to a context
    // so it's awaiting some solution for a reasonably-performing way to
    // do the mapping from [1 2 3 4 5 6] to [-3 -2 -1 0 1 2] (or whatever)
    //
    REBINT index;
};


struct Reb_Action_Payload {
    //
    // `paramlist` is a Rebol Array whose 1..NUM_PARAMS values are all
    // TYPESET! values, with an embedded symbol (a.k.a. a "param") as well
    // as other bits, including the parameter class (PARAM_CLASS).  This
    // is the list that is processed to produce WORDS-OF, and which is
    // consulted during invocation to fulfill the arguments
    //
    // In addition, its [0]th element contains an ACTION! value which is
    // self-referentially the function itself.  This means that the paramlist
    // can be passed around as a single pointer from which a whole REBVAL
    // for the function can be found (although this value is archetypal, and
    // loses the `binding` property--which must be preserved other ways)
    //
    // Paramlists may contain hidden fields, if they are specializations...
    // because they have to have the right number of slots to line up with
    // the frame of the underlying function.
    //
    // The `misc.meta` field of the paramlist holds a meta object (if any)
    // that describes the function.  This is read by help.
    //
    REBARR *paramlist;

    // `details` holds the instance data used by the dispatcher (which lives
    // in MISC(details).dispatcher) to run this particular action.  What the
    // details array holds varies:
    //
    // USER FUNCTIONS: 1-element array w/a BLOCK!, the body of the function
    // ACTIONS: 1-element array w/WORD! verb of the action (OPEN, APPEND, etc)
    // SPECIALIZATIONS: 1-element array containing a FRAME! value
    // ROUTINES/CALLBACKS: stylized array (REBRIN*)
    //
    // Since plain natives only need the C function, the body is optionally
    // used to store a block of Rebol code that is equivalent to the native,
    // for illustrative purposes.  (a "fake" answer for SOURCE)
    //
    // By storing the function dispatcher in the `details` array node instead
    // of in the value cell itself, it also means the dispatcher can be
    // HIJACKed--or otherwise hooked to affect all instances of a function.
    //
    REBARR *details;
};

struct Reb_Context_Payload {
    //
    // `varlist` is a Rebol Array that from 1..NUM_VARS contains REBVALs
    // representing the stored values in the context.
    //
    // As with the `paramlist` of an ACTION!, the varlist uses the [0]th
    // element specially.  It stores a copy of the ANY-CONTEXT! value that
    // refers to itself.
    //
    // The `keylist` is held in the varlist's Reb_Series.link field, and it
    // may be shared with an arbitrary number of other contexts.  Changing
    // the keylist involves making a copy if it is shared.
    //
    // REB_MODULE depends on a property stored in the "meta" Reb_Series.link
    // field of the keylist, which is another object's-worth of data *about*
    // the module's contents (e.g. the processed header)
    //
    REBARR *varlist;

    // A single FRAME! can go through multiple phases of evaluation, some of
    // which should expose more fields than others.  For instance, when you
    // specialize a function that has 10 parameters so it has only 8, then
    // the specialization frame should not expose the 2 that have been
    // removed.  It's as if the WORDS-OF the spec is shorter than the actual
    // length which is used.
    //
    // Hence, each independent value that holds a frame must remember the
    // function whose "view" it represents.  This field is only applicable
    // to frames, and so it could be used for something else on other types
    //
    // Note that the binding on a FRAME! can't be used for this purpose,
    // because it's already used to hold the binding of the function it
    // represents.  e.g. if you have a definitional return value with a
    // binding, and try to MAKE FRAME! on it, the paramlist alone is not
    // enough to remember which specific frame that function should exit.
    //
    REBACT *phase;
};


struct Reb_Varargs_Payload {
    //
    // If the extra->binding of the varargs is not UNBOUND, it represents the
    // frame in which this VARARGS! was tied to a parameter.  This 0-based
    // offset can be used to find the param the varargs is tied to, in order
    // to know whether it is quoted or not (and its name for error delivery).
    //
    // It can also find the arg.  Similar to the param, the arg is only good
    // for the lifetime of the FRAME! in extra->binding...but even less so,
    // because VARARGS! can (currently) be overwritten with another value in
    // the function frame at any point.  Despite this, we proxy the
    // VALUE_FLAG_UNEVALUATED from the last TAKE to reflect its status.
    //
    REBCNT param_offset;

    REBACT *phase; // where to look up parameter by its offset
};


// SPECIALIZE attempts to be smart enough to do automatic partial specializing
// when it can, and to allow you to augment the APPLY-style FRAME! with an
// order of refinements that is woven into the single operation.  It links
// all the partially specialized (or unspecified) refinements as it traverses
// in order to revisit them and fill them in more efficiently.  This special
// payload is used along with a singly linked list via extra.next_partial

#define PARTIAL_FLAG_IN_USE \
    FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT)

#define PARTIAL_FLAG_SAW_NULL_ARG \
    FLAG_LEFT_BIT(TYPE_SPECIFIC_BIT + 1)

struct Reb_Partial_Payload { // Used with REB_X_PARTIAL
    REBDSP dsp; // the DSP of this partial slot (if ordered on the stack)
    REBCNT index; // maps to the index of this parameter in the paramlist
};


// Handles hold a pointer and a size...which allows them to stand-in for
// a binary REBSER.
//
// Since a function pointer and a data pointer aren't necessarily the same
// size, the data has to be a union.
//
// Note that the ->extra field of the REBVAL may contain a singular REBARR
// that is leveraged for its GC-awareness.
//
struct Reb_Handle_Payload {
    union {
        void *pointer;
        CFUNC *cfunc;
    } data;

    uintptr_t length;
};


// File descriptor in singular->link.fd
// Meta information in singular->misc.meta
//
struct Reb_Library_Payload {
    REBARR *singular; // singular array holding this library value
};

typedef REBARR REBLIB;


// The general FFI direction is to move it so that it is "baked in" less,
// and represents an instance of a generalized extension mechanism (like GOB!
// should be).  On that path, a struct's internals are simplified to being
// just an array:
//
// [0] is a specification array which contains all the information about
// the structure's layout, regardless of what offset it would find itself at
// inside of a data blob.  This includes the total size, and arrays of
// field definitions...essentially, the validated spec.  It also contains
// a HANDLE! which contains the FFI-type.
//
// [1] is the content BINARY!.  The VAL_INDEX of the binary indicates the
// offset within the struct.  See notes in ADDR-OF from the FFI about how
// the potential for memory instability of content pointers may not be a
// match for the needs of an FFI interface.
//
struct Reb_Struct_Payload {
    REBARR *stu; // [0] is canon self value, ->misc.schema is schema
    REBSER *data; // binary data series (may be shared with other structs)
};

// To help document places in the core that are complicit in the "extension
// hack", alias arrays being used for the FFI to another name.
//
typedef REBARR REBSTU;
typedef REBARR REBFLD;


#include "reb-gob.h"

struct Reb_Gob_Payload {
    REBGOB *gob;
    REBCNT index;
};



//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE CELL DEFINITION (`struct Reb_Value` or `struct Reb_Relative_Value`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Each value cell has a header, "extra", and payload.  Having the header come
// first is taken advantage of by the byte-order-sensitive macros to be
// differentiated from UTF-8 strings, etc. (See: Detect_Rebol_Pointer())
//
// Conceptually speaking, one might think of the "extra" as being part of
// the payload.  But it is broken out into a separate union.  This is because
// the `binding` property is written using common routines for several
// different types.  If the common routine picked just one of the payload
// unions to initialize, it would "disengage" the other unions.
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

union Reb_Value_Extra {
    //
    // The release build doesn't put anything in the ->extra field by default,
    // so sensitive compilers notice when cells are moved without that
    // initialization.  Rather than disable the warning, this can be used to
    // put some junk into, but TRASH_POINTER_IF_DEBUG() won't subvert the
    // warning.  So just poke whatever pointer is at hand that is likely to
    // already be in a register and not meaningful (e.g. nullptr is a bad
    // value, because that could look like a valid non-binding)
    //
    void *trash;

    // The binding will be either a REBACT (relative to a function) or a
    // REBCTX (specific to a context), or simply a plain REBARR such as
    // EMPTY_ARRAY which indicates UNBOUND.  ARRAY_FLAG_VARLIST and
    // ARRAY_FLAG_PARAMLIST can be used to tell which it is.
    //
    // ANY-WORD!: binding is the word's binding
    //
    // ANY-ARRAY!: binding is the relativization or specifier for the REBVALs
    // which can be found inside of the frame (for recursive resolution
    // of ANY-WORD!s)
    //
    // ACTION!: binding is the instance data for archetypal invocation, so
    // although all the RETURN instances have the same paramlist, it is
    // the binding which is unique to the REBVAL specifying which to exit
    //
    // ANY-CONTEXT!: if a FRAME!, the binding carries the instance data from
    // the function it is for.  So if the frame was produced for an instance
    // of RETURN, the keylist only indicates the archetype RETURN.  Putting
    // the binding back together can indicate the instance.
    //
    // VARARGS!: the binding identifies the feed from which the values are
    // coming.  It can be an ordinary singular array which was created with
    // MAKE VARARGS! and has its index updated for all shared instances.
    //
    REBNOD* binding;

    // See REB_X_PARTIAL.
    //
    REBVAL *next_partial; // links to next potential partial refinement arg

    // The remaining properties are the "leftovers" of what won't fit in the
    // payload for other types.  If those types have a quanitity that requires
    // 64-bit alignment, then that gets the priority for being in the payload,
    // with the "Extra" pointer-sized item here.

    REBSTR *key_spelling; // if typeset is key of object or function parameter
    REBDAT date; // time's payload holds the nanoseconds, this is the date
    REBCNT struct_offset; // offset for struct in the possibly shared series

    // !!! Biasing Ren-C to helping solve its technical problems led the
    // REBEVT stucture to get split up.  The "eventee" is now in the extra
    // field, while the event payload is elsewhere.  This brings about a long
    // anticipated change where REBEVTs would need to be passed around in
    // clients as REBVAL-sized entities.
    //
    // See also rebol_devreq->requestee

    union Reb_Eventee eventee;

    unsigned m0:32; // !!! significand, lowest part - see notes on Reb_Money

    // There are two types of HANDLE!, and one version leverages the GC-aware
    // ability of a REBSER to know when no references to the handle exist and
    // call a cleanup function.  The GC-aware variant allocates a "singular"
    // array, which is the exact size of a REBSER and carries the canon data.
    // If the cheaper kind that's just raw data and no callback, this is null.
    //
    REBARR *singular;

  #if !defined(NDEBUG)
    //
    // Reb_Track_Payload is not big enough for a tick as well as a file and a
    // line number, so it's put here.  It's included in all debug builds,
    // not just those which have DEBUG_TRACK_CELLS...because it is used to
    // implement a distinct state for unreadable blanks.  It will simply be
    // a -1 for unreadable blanks, and a +1 for ordinary ones if there is
    // no tick available, otherwise it will be the negative value of the
    // tick if unreadable.  This keeps from stealing a header bit, as well
    // as avoiding the variations which could occur if the VAL_TYPE() was
    // changed between debug and release builds.
    //
    intptr_t tick;
  #endif
};

union Reb_Value_Payload {

  #if defined(DEBUG_TRACK_CELLS) && !defined(DEBUG_TRACK_EXTEND_CELLS)
    struct Reb_Track_Payload track; // NULL, VOID!, BLANK!, LOGIC!, BAR!
  #endif

    REBUNI character; // It's CHAR! (for now), but 'char' is a C keyword
    REBI64 integer;
    REBDEC decimal;

    REBVAL *pair; // actually a "pairing" pointer
    struct Reb_Money_Payload money;
    struct Reb_Handle_Payload handle;
    struct Reb_Time_Payload time;
    struct Reb_Tuple_Payload tuple;
    struct Reb_Datatype_Payload datatype;
    struct Reb_Typeset_Payload typeset;

    struct Reb_Library_Payload library;
    struct Reb_Struct_Payload structure; // STRUCT!, but 'struct' is C keyword

    struct Reb_Event_Payload event;
    struct Reb_Gob_Payload gob;

    // These use `specific` or `relative` in `binding`, based on IS_RELATIVE()

    struct Reb_Word_Payload any_word;
    struct Reb_Quoted_Payload quoted;
    struct Reb_Series_Payload any_series;
    struct Reb_Action_Payload action;
    struct Reb_Context_Payload any_context;
    struct Reb_Varargs_Payload varargs;

    // Internal-only payloads for cells that use > REB_MAX as the VAL_TYPE()
    //
    struct Reb_Partial_Payload partial; // used with REB_X_PARTIAL
};

#ifdef CPLUSPLUS_11
    struct Reb_Cell
#else
    struct Reb_Value
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
        //
        // Overwriting one cell with another can't be done with a direct
        // assignment, such as `*dest = *src;`  Cells contain formatting bits
        // that must be preserved, and some flag bits shouldn't be copied.
        // (See: CELL_MASK_PRESERVE)
        //
        // Also, copying needs to be sensitive to the target slot.  If that
        // slot is at a higher stack level than the source (or persistent in
        // an array) then special handling is necessary to make sure any stack
        // constrained pointers are "reified" and visible to the GC.
        //
        // Goal is that the mechanics are managed with low-level C, so the
        // C++ build gives errors on bit copy.  Use functions instead.
        // (See: Move_Value(), Blit_Cell(), Derelativize())
        //
        // Note: It is annoying that this means any structure that embeds a
        // value cell cannot be assigned.  However, `struct Reb_Value` must
        // be the type exported in both C and C++ under the same name and
        // bit patterns.  Pretty much any attempt to work around this and
        // create a base class that works in C too (e.g. Reb_Cell) would wind
        // up violating strict aliasing.  Think *very hard* before changing!
        //
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

#define VAL(p) \
    cast(const RELVAL*, (p))
