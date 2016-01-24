//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
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
//  Summary: Definitions for the Rebol Value Struct (REBVAL) and Helpers
//  File: %sys-value.h
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
// REB_BLOCK, REB_STRING, etc.  Then there are 8 flags which are for general
// purposes that could apply equally well to any type of value (including
// whether the value should have a new-line after it when molded out inside
// of a block).  There are 8 bits which are custom to each type--for
// instance whether a key in an object is hidden or not.  Then there are
// 8 bits currently reserved for future use.
//
// The remaining content of the REBVAL struct is the "Payload".  It is the
// size of three (void*) pointers, and is used to hold whatever bits that
// are needed for the value type to represent itself.  Perhaps obviously,
// an arbitrarily long string will not fit into 3*32 bits, or even 3*64 bits!
// You can fit the data for an INTEGER or DECIMAL in that (at least until
// they become arbitrary precision) but it's not enough for a generic BLOCK!
// or a FUNCTION! (for instance).  So those pointers are used to point to
// things, and often they will point to one or more Rebol Series (see
// %sys-series.h for an explanation of REBSER, REBARR, REBCTX, and REBMAP.)
//
// While some REBVALs are in C stack variables, most reside in the allocated
// memory block for a Rebol series.  The memory block for a series can be
// resized and require a reallocation, or it may become invalid if the
// containing series is garbage-collected.  This means that many pointers to
// REBVAL are unstable, and could become invalid if arbitrary user code
// is run...this includes values on the data stack, which is implemented as
// a series under the hood.  (See %sys-stack.h)
//
// A REBVAL in a C stack variable does not have to worry about its memory
// address becoming invalid--but by default the garbage collector does not
// know that value exists.  So while the address may be stable, any series
// it has in the payload might go bad.  Use PUSH_GUARD_VALUE() to protect a
// stack variable, and then DROP_GUARD_VALUE() when the protection is not
// needed.  (You must always drop the last guard pushed.)
//
// For a means of creating a temporary array of GC-protected REBVALs, see
// the "chunk stack" in %sys-stack.h.  This is used when building function
// argument frames, which means that the REBVAL* arguments to a function
// accessed via ARG() will be stable as long as the function is running.
//

#ifndef VALUE_H
#define VALUE_H


// Forward declaration.  The actual structure for REBVAL can't be defined
// until all the structs and unions it builds on have been defined.  So you
// will find it near the end of this file, as `struct Reb_Value`.
//
struct Reb_Value;
typedef struct Reb_Value REBVAL;


//
// Forward declarations of the series subclasses defined in %sys-series.h
// Because the Reb_Series structure includes a Reb_Value by value, it
// must be included *after* %sys-value.h
//

struct Reb_Series;
typedef struct Reb_Series REBSER; // Rebol series node

struct Reb_Array;
typedef struct Reb_Array REBARR; // REBSER containing REBVALs ("Rebol Array")

struct Reb_Context;
typedef struct Reb_Context REBCTX; // parallel REBARR key/var arrays, +2 values

struct Reb_Func;
typedef struct Reb_Func REBFUN; // function parameters plus function REBVAL

struct Reb_Map;
typedef struct Reb_Map REBMAP; // REBARR listing key/value pairs with hash


// A `#pragma pack` of 4 was requested by the R3-Alpha source for the
// duration of %sys-value.h:
//
//     http://stackoverflow.com/questions/3318410/
//
// Pushed here and popped at the end of the file to the previous value
//
// !!! Compilers are free to ignore pragmas (or error on them), and this can
// cause a loss of performance...so it might not be worth doing.  Especially
// because the ordering of fields in REBVAL members is alignment-conscious
// already.  Since Rebol series indexes (REBINT) and length counts (REBCNT)
// are still 32-bits on 64-bit platforms, it means that often REBINTs are
// "paired up" in structures to create a 64-bit alignment for a pointer
// that comes after them.  So everything is pretty well aligned as-is.
//
#pragma pack(push,4)


//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE HEADER (`struct Reb_Value_Header`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The layout of the header corresponds to the following bitfield
// structure on big endian machines:
//
//    unsigned resv:8;      // !!! <reserved for future use>
//    unsigned exts:8;      // extensions to datatype
//    unsigned opts:8;      // options that can apply to any value
//    unsigned type:6;      // datatype (64 possibilities)
//    unsigned settable:1;  // Debug build only--"formatted" for setting
//    unsigned not_end:1;   // not an end marker
//
// Due to a desire to be able to assign all the header bits in one go
// with a native-platform-sized int, this is done with bit masking.
// Using bitfields would bring in questions of how smart the
// optimizer is, as well as the endianness of the underlyling machine.
//
// We use REBUPT (Rebol's pre-C99 compatibility type for uintptr_t,
// which is just uintptr_t from C99 on).  Only the low 32 bits are used
// on 64-bit machines in order to make sure all the features work on
// 32-bit machines...but could be used for some optimization or caching
// purpose to enhance the 64-bit build.  No such uses implemented yet.
//

struct Reb_Value_Header {
    REBUPT bits;
};

// `NOT_END_MASK`
//
// If set, it means this is *not* an end marker.  The bit has been picked
// strategically to be in the negative sense, and in the lowest bit position.
// This means that any even-valued unsigned integer REBUPT value can be used
// to implicitly signal an end.
//
// If this bit is 0, it means that *no other header bits are valid*, as it
// may contain arbitrary data used for non-REBVAL purposes.
//
// Note that the value doing double duty as a number for one purpose and an
// END marker as another *must* be another REBUPT.  It cannot be a pointer
// (despite being guaranteed-REBUPT-sized, and despite having a value that
// is 0 when you mod it by 2.  So-called "type-punning" is unsafe with a
// likelihood of invoking "undefined behavior", while it's the compiler's
// responsibility to guarantee that pointers to memory of the same type of
// data be compatibly read-and-written.
//
#define NOT_END_MASK 0x01

#define GENERAL_VALUE_BIT 8
#define TYPE_SPECIFIC_BIT 16

// `WRITABLE_MASK_DEBUG`
//
// This is for the debug build, to make it safer to use the implementation
// trick of NOT_END_MASK.  It indicates the slot is "REBVAL sized", and can
// be written into--including to be written with SET_END().
//
// It's again a strategic choice--the 2nd lowest bit and in the negative.
// This means any REBUPT value whose % 4 within a container doing
// double-duty as an implicit terminator for the contained values can
// trigger an alert if the values try to overwrite it.
//
#ifdef NDEBUG
    //
    // Though in the abstract it is desirable to have a way to protect an
    // individual value from writing even in the release build, this is
    // a debug-only check...it makes every value header initialization have
    // to do two writes instead of one (one to format, then later to write
    // and check that it is properly formatted space)
    //
#else
    // We want to be assured that we are not trying to take the type of a
    // value that is actually an END marker, because end markers chew out only
    // one bit--the rest of the REBUPT bits besides the bottom two may be
    // anything necessary for the purpose.
    //
    #define WRITABLE_MASK_DEBUG 0x02
#endif

// The type mask comes up a bit and it's a fairly obvious constant, so this
// hardcodes it for obviousness.  High 6 bits of the lowest byte.
//
#define HEADER_TYPE_MASK 0xFC


//=////////////////////////////////////////////////////////////////////////=//
//
//  END marker (not a value type, only writes `struct Reb_Value_Flags`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Historically Rebol arrays were always one value longer than their maximum
// content, and this final slot was used for a special REBVAL called END!.
// Like a null terminator in a C string, it was possible to start from one
// point in the series and traverse to find the end marker without needing
// to maintain a count.  Rebol series store their length also--but it's
// faster and more general to use the terminator.
//
// Ren-C changed this so that end is not a data type, but rather seeing a
// header slot with the lowest bit set to 0.  (See NOT_END_MASK for
// an explanation of this choice.)  The upshot is that a data structure
// designed to hold Rebol arrays is able to terminate an array at full
// capacity with a pointer-sized integer with the lowest 2 bits clear, and
// use the rest of the bits for other purposes.  (See WRITABLE_MASK_DEBUG
// for why it's the low 2 bits and not just the lowest bit.)
//
// This means not only is a full REBVAL not needed to terminate, the sunk cost
// of an existing 32-bit or 64-bit number (depending on platform) can be used
// to avoid needing even 1/4 of a REBVAL for a header to terminate.  (See the
// `size` field in `struct Reb_Chunk` from %sys-stack.h for a simple example
// of the technique.)
//
// !!! Because Rebol Arrays (REBARR) have both a length and a terminator, it
// is important to keep these in sync.  R3-Alpha sought to give code the
// freedom to work with unterminated arrays if the cost of writing terminators
// was not necessary.  Ren-C pushed back against this to try and be more
// uniform to get the invariants under control.  A formal balance is still
// being sought of when terminators will be required and when they will not.
//

// The debug build puts REB_MAX in the type slot, to distinguish it from the
// 0 that signifies REB_TRASH.  That can be checked to ensure a writable
// value isn't a trash, but a non-writable value (e.g. a pointer) could be
// any bit pattern in the type slot.  Only check if it's a Rebol-initialized
// value slot...and then, tolerate "GC safe trash" (an unset in release)
//
#define IS_END(v) \
    (assert( \
        !((v)->header.bits & WRITABLE_MASK_DEBUG) \
        || (((v)->header.bits & HEADER_TYPE_MASK) != REB_TRASH \
            || GET_VAL_FLAG((v), TRASH_FLAG_SAFE) \
        ) \
    ), (v)->header.bits % 2 == 0)

#define NOT_END(v)          NOT(IS_END(v))

#ifdef NDEBUG
    #define SET_END(v)      ((v)->header.bits = 0)
#else
    //
    // The slot we are trying to write into must have at least been formatted
    // in the debug build VAL_INIT_WRITABLE_DEBUG().  Otherwise it could be a
    // pointer with its low bit clear, doing double-duty as an IS_END(),
    // marker...which we cannot overwrite...not even with another END marker.
    //
    #define SET_END(v) \
        (Assert_REBVAL_Writable((v), __FILE__, __LINE__), \
            (v)->header.bits = WRITABLE_MASK_DEBUG | REB_MAX)
#endif

// Pointer to a global END marker.  Though this global value is allocated to
// the size of a REBVAL, only the header is initialized.  This means if any
// of the value payload is accessed, it will trip memory checkers like
// Valgrind or Address Sanitizer to warn of the mistake.
//
#define END_VALUE           PG_End_Val


//=////////////////////////////////////////////////////////////////////////=//
//
//  OPTS FLAGS common to every REBVAL
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The value option flags are 8 individual bitflags which apply to every
// value of every type.  Due to their scarcity, they are chosen carefully.
//

enum {
    // `VALUE_FLAG_FALSE`
    //
    // This flag indicates that the attached value is one of the two cases of
    // Rebol values that are considered "conditionally false".  This means
    // that IF or WHILE or CASE would consider them to not be a test-positive
    // for running the associated code.
    //
    // The two cases of conditional falsehood are (LOGIC! FALSE), and the
    // NONE! value.  In order to optimize tests used by conditional constructs,
    // this header bit is set to 1 for those two values...while all others
    // set it to 0.
    //
    // This means that a LOGIC! does not need to use its data payload, and
    // can just check this bit to know if it is true or false.  Also, testing
    // for something being (LOGIC! TRUE) or (LOGIC! FALSE) can be done with
    // a bit mask against one memory location in the header--not two tests
    // against the type in the header and some byte in the payload.
    //
    VALUE_FLAG_FALSE = 1 << (GENERAL_VALUE_BIT + 0),

    // `VALUE_FLAG_LINE`
    //
    // If the line marker bit is 1, then when the value is molded it will put
    // a newline before the value.  The logic is a bit more subtle than that,
    // because an ANY-PATH! could not be LOADed back if this were allowed.
    // The bit is set initially by what the scanner detects, and then left
    // to the user's control after that.
    //
    // !!! The native `new-line` is used set this, which has a somewhat
    // poor name considering its similarity to `newline` the line feed char.
    //
    VALUE_FLAG_LINE = 1 << (GENERAL_VALUE_BIT + 1),

    // `VALUE_FLAG_THROWN`
    //
    // The thrown bit is being phased out, as the concept of a value itself
    // being "thrown" does not make a lot of sense, compared to the idea
    // that the evaluator itself is in a "throwing state".  If a thrown bit
    // can get on a value, then one has to worry about that value getting
    // copied and showing up somewhere that it doesn't make sense.
    //
    // Originally, R3-Alpha did not have a thrown bit on values, rather the
    // throw itself was represented as a certain kind of ERROR! value.  Ren-C
    // modifications extended THROW to allow a /NAME that could be a full
    // REBVAL (instead of a selection from a limited set of words).  This
    // made it possible to identify a throw by an object, function,
    // fully bound word, etc.
    //
    // But even after the change, the "thrown-ness" was still a property
    // of the "throw-name REBVAL".  By virtue of being a property on a
    // value *it could be dropped on the floor and ignored*.  There were
    // countless examples of this originating in the code.
    //
    // As part of the process of stamping out the idea that thrownness comes
    // from a value, all routines that can potentially return thrown values
    // have been adapted to return a boolean and adopt the XXX_Throws()
    // naming convention, so one can write:
    //
    //     if (XXX_Throws()) {
    //        /* handling code */
    //     }
    //
    // This forced every caller to consciously have a code path dealing with
    // potentially thrown values, reigning in the previous problems.  At
    // time of writing, the situation is much more under control, and natives
    // return a flag indicating that they wish to throw a value vs. return
    // one.  This is checked redundantly against the value bit for now, but
    // it is likely that the bit will be removed in favor of pushing the
    // responsibility into the evaluator state.
    //
    VALUE_FLAG_THROWN = 1 << (GENERAL_VALUE_BIT + 2),

    // This is a bit used in conjunction with VALUE_FLAG_THROWN, which could
    // also be folded in to be a model of being in an "exiting state".  The
    // usage is for definitionally scoped RETURN, RESUME/AT, and EXIT/FROM
    // where the frame desired to be targeted is marked with this flag.
    // Currently it is indicated by either the object of the call frame (for
    // a CLOSURE!) or the paramlist for all other ANY-FUNCTION!.
    //
    // !!! WARNING - In the current scheme this will only jump up to the most
    // recent instantiation of the function, as it does not have unique
    // identity in relative binding.  When FUNCTION! and its kind are updated
    // to use a new technique that brings it to parity with CLOSURE! in this
    // regard, then that will fix this.
    //
    VALUE_FLAG_EXIT_FROM = 1 << (GENERAL_VALUE_BIT + 3)
};

// VALUE_FLAG_e flags both generically applicable to all values and specific
// to a single value type.  To be a little on the safe side, the masking
// routines
//
#ifdef NDEBUG
    #define SET_VAL_FLAG(v,f) \
        ((v)->header.bits |= (f))

    #define GET_VAL_FLAG(v,f) \
        LOGICAL((v)->header.bits & (f))

    #define CLEAR_VAL_FLAG(v,f) \
        ((v)->header.bits &= ~cast(REBUPT, (f)))
#else
    // For safety in the debug build, all the type-specific flags include
    // their type as part of the flag.  This type is checked first, and then
    // masked out to use the single-bit-flag value which is intended.  The
    // check uses the bits of an exemplar type to identify the category
    // (e.g. REB_FUNCTION for ANY-FUNCTION!, REB_OBJECT for ANY-CONTEXT!)

    #define SET_VAL_FLAG(v,f) \
        (Assert_Flags_Are_For_Value((v), (f)), \
            (v)->header.bits |= ((f) & ~HEADER_TYPE_MASK))

    #define GET_VAL_FLAG(v,f) \
        (Assert_Flags_Are_For_Value((v), (f)), \
            LOGICAL((v)->header.bits & ((f) & ~HEADER_TYPE_MASK)))

    #define CLEAR_VAL_FLAG(v,f) \
        (Assert_Flags_Are_For_Value((v), (f)), \
            (v)->header.bits &= ~cast(REBUPT, (f) & ~HEADER_TYPE_MASK))
#endif

//
// Setting and clearing multiple flags works, so these names make that "clear"
//

#define SET_VAL_FLAGS(v,f) \
    SET_VAL_FLAG((v), (f))

#define CLEAR_VAL_FLAGS(v,f) \
    CLEAR_VAL_FLAG((v), (f))


//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE TYPE and per-type EXTS flags
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Every value has 6 bits reserved for its VAL_TYPE().  The reason only 6
// are used is because low-level TYPESET!s are only 64-bits (so they can fit
// into a REBVAL payload, along with a key symbol to represent a function
// parameter).  If there were more types, they couldn't be flagged in a
// typeset that fit in a REBVAL under that constraint.
//
// VAL_TYPE() should obviously not be called on uninitialized memory.  But
// it should also not be called on an END marker, as those markers only
// guarantee the low bit as having Rebol-readable-meaning.  In debug builds,
// this is asserted by VAL_TYPE_Debug.
//

#ifdef NDEBUG
    #define VAL_TYPE(v) \
        cast(enum Reb_Kind, (v)->header.bits & HEADER_TYPE_MASK)
#else
    #define VAL_TYPE(v) \
        VAL_TYPE_Debug((v), __FILE__, __LINE__)
#endif

// For the processor's convenience, the 64 basic Rebol types are shifted to
// the left by 2 bits.  This makes their range go from 0..251 instead of
// from 0..63.  The choice to do this is because most of the type the types
// are just used in comparisons or switch statements, and it's cheaper to
// not have to shift out the 2 low bits that are used for END and WRITABLE
// flags in the header most of the time.
//
// However, to index into a zero based array with 64 elements, the shift
// needs to be done.  If that's required these defines adjust for the shift.
// See also REB_MAX_0
//
#define KIND_FROM_0(z) cast(enum Reb_Kind, (z) << 2)
#define TO_0_FROM_KIND(k) (cast(REBCNT, (k)) >> 2)
#define VAL_TYPE_0(v) TO_0_FROM_KIND(VAL_TYPE(v))

// SET_TYPE_BITS only sets the type, with other header bits intact.  This
// should be used when you are sure that the new type payload is in sync with
// the type and bits (for instance, changing from one ANY-WORD! type to
// another, the binding needs to be in sync with the header bits)
//
// NOTE: The header MUST already be valid and initialized to use this!  For
// fresh value creation, one wants to use VAL_RESET_HEADER to clear bits and
// set the type.
//
// !!! Is it worth the effort to add a debugging flag into the value to
// disallow calling this routine if VAL_RESET_HEADER has not been run, or
// are there too few instances to be worth it and is _BITS enough a hint?
//
#define VAL_SET_TYPE_BITS(v,t) \
    ((v)->header.bits &= ~cast(REBUPT, HEADER_TYPE_MASK), \
        (v)->header.bits |= (t))

// VAL_RESET_HEADER clears out the header and sets it to a new type (and also
// sets the option bits indicating the value is *not* an END marker, and
// that the value is a full cell which can be written to).
//
#ifdef NDEBUG
    #define VAL_RESET_HEADER(v,t) \
        ((v)->header.bits = NOT_END_MASK | (t))
#else
    // The debug build includes an extra check that the value we are about
    // to write the header of is actually a full REBVAL-sized slot...and not
    // just an implicit END marker that's really doing double-duty as some
    // internal pointer of a container structure.
    //
    #define VAL_RESET_HEADER(v,t) \
        (Assert_REBVAL_Writable((v), __FILE__, __LINE__), \
            (v)->header.bits = NOT_END_MASK | WRITABLE_MASK_DEBUG | (t))
#endif

// !!! SET_ZEROED is a macro-capture of a dodgy behavior of R3-Alpha,
// which was to assume that clearing the payload of a value and then setting
// the header made it the `zero?` of that type.  Review uses.
//
#define SET_ZEROED(v,t) \
    (VAL_RESET_HEADER((v),(t)), \
        CLEAR(&(v)->payload, sizeof(union Reb_Value_Payload)))

//
// The ability to read and write all the EXTS at once as an 8-bit value.
// Review uses to see if they could be done all as part of the initialization.
//

#define VAL_EXTS_DATA(v) \
    (((v)->header.bits & \
        (cast(REBUPT, 0xFF) << TYPE_SPECIFIC_BIT)) >> TYPE_SPECIFIC_BIT)

#define VAL_SET_EXTS_DATA(v,e) \
    (((v)->header.bits &= ~(cast(REBUPT, 0xFF) << TYPE_SPECIFIC_BIT)), \
        (v)->header.bits |= ((e) << TYPE_SPECIFIC_BIT))


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACK payload (not a value type, only in DEBUG)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// `struct Reb_Track` is the value payload in debug builds for any REBVAL
// whose VAL_TYPE() doesn't need any information beyond the header.  This
// offers a chance to inject some information into the payload to help
// know where the value originated.  It is used by TRASH!, UNSET!, NONE!
// and LOGIC!.
//
// In addition to the file and line number where the assignment was made,
// the "tick count" of the DO loop is also saved.  This means that it can
// be possible in a repro case to find out which evaluation step produced
// the value--and at what place in the source.  Repro cases can be set to
// break on that tick count, if it is deterministic.
//
// This feature can be helpful to enable, but it can also create problems
// in terms of making memory that would look "free" appear available.
//
#define TRACK_EMPTY_PAYLOADS // for now, helpful to know...
#if !defined(NDEBUG)
    #ifdef TRACK_EMPTY_PAYLOADS
        struct Reb_Track {
            const char *filename;
            int line;
            REBCNT count;
        };

        #define SET_TRACK_PAYLOAD(v) \
            ( \
                (v)->payload.track.filename = __FILE__, \
                (v)->payload.track.line = __LINE__, \
                (v)->payload.track.count = TG_Do_Count, \
                NOOP \
            )

        #define VAL_TRACK_FILE(v)       ((v)->payload.track.filename)
        #define VAL_TRACK_LINE(v)       ((v)->payload.track.line)
        #define VAL_TRACK_COUNT(v)      ((v)->payload.track.count)
    #else
        #define SET_TRACK_PAYLOAD(v) NOOP
    #endif
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRASH! (may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Trash is a debugging-only concept.  Nevertheless, REB_TRASH consumes
// VAL_TYPE #0 (of 64) in the release build.  That offers some benefit since
// it means one of the 64-bits in a typeset is always available for another
// use (as trash is not ever supposed to be seen by the user...)
//
// It's intended to be a value written into cells in the debug build when
// the cell is expected to be overwitten with valid data.  By default, the
// garbage collector will raise an alert if a TRASH! value is not overwritten
// by the time it sees it...and any attempt to read the type of a trash
// value with VAL_TYPE() will cause the debug build to assert.  Hence it
// must be tested for specially.
//
// There are some uses of trash which the GC should be able to run while it
// is extant.  For example, when a native function is called the cell where
// it is supposed to write its output is set to trash.  However, the garbage
// collector may run before the native has written its result...so for these
// cases, use SET_TRASH_SAFE().  It will still trigger assertions if other
// code tries to use it--it's just that the GC will treat it like an UNSET!.
//
// The operations for setting trash are available in both debug and release
// builds.  An unsafe trash set turns into a NOOP in release builds (it will
// be "trash" in the sense of being uninitialized memory).  Meanwhile a safe
// trash set turns into a SET_UNSET() in release builds--so for instance any
// native that does not write its return result will return unset in release
// builds.  IS_TRASH_DEBUG() can be used to test for trash in debug builds,
// but not in release builds...as there is no test for "uninitialized memory".
//
// Because the trash value saves the filename and line where it originated,
// the REBVAL has that info in debug builds to inspect in its `trash` union
// member.  It also saves the Do tick count in which it was created, to
// make it easier to pinpoint precisely when it came into existence.
//
// !!! If we're not using TRACK_EMPTY_PAYLOADS, should this POISON_MEMORY() on
// the payload to help catch invalid reads?  Trash values don't hang around
// that long, except for the case of the values in the extra "->rest" capacity
// of series.  Would that be too many memory poisonings to handle efficiently?
//
#ifdef NDEBUG
    #define MARK_VAL_READ_ONLY_DEBUG(v) NOOP

    #define VAL_INIT_WRITABLE_DEBUG(v) NOOP

    #define SET_TRASH_IF_DEBUG(v) NOOP

    #define SET_TRASH_SAFE(v) SET_UNSET(v)
#else
    enum {
        // GC safe trash (UNSET! in release build)
        //
        TRASH_FLAG_SAFE = (1 << TYPE_SPECIFIC_BIT) | REB_TRASH
    };

    // Special type check...we don't want to use a VAL_TYPE() == REB_TRASH
    // because VAL_TYPE is supposed to assert on trash
    //
    #define IS_TRASH_DEBUG(v) \
        (((v)->header.bits & HEADER_TYPE_MASK) == 0)

    // This particularly virulent form of trashing will make the resultant
    // cell unable to be used with SET_END() or VAL_RESET_HEADER() until
    // a SET_TRASH_IF_DEBUG() or SET_TRASH_SAFE() is used to overrule it.
    //
    #define MARK_VAL_READ_ONLY_DEBUG(v) \
        ((v)->header.bits &= ~cast(REBUPT, WRITABLE_MASK_DEBUG), NOOP)

    // The debug build requires that any value slot that's going to be written
    // to via VAL_RESET_HEADER() be marked writable.  Series and other value
    // containers do this automatically, but if you make a REBVAL as a stack
    // variable then it will have to be done before any write can happen.
    //
    // An alignment check to the size of a pointer is commented out due to
    // being expensive to run on every value in the system, but should be
    // added under an "intense checks" switch.
    //
    #define VAL_INIT_WRITABLE_DEBUG(v) \
        ( \
            /* assert(cast(REBUPT, (v)) % sizeof(REBUPT) == 0), */ \
            (v)->header.bits = NOT_END_MASK | WRITABLE_MASK_DEBUG, \
            SET_TRACK_PAYLOAD(v) \
        )

    #define SET_TRASH_IF_DEBUG(v) \
        ( \
            VAL_RESET_HEADER((v), REB_TRASH), \
            SET_TRACK_PAYLOAD(v) \
        )

    #define SET_TRASH_SAFE(v) \
        ( \
            VAL_RESET_HEADER((v), REB_TRASH), \
            SET_VAL_FLAG((v), TRASH_FLAG_SAFE), \
            SET_TRACK_PAYLOAD(v) \
        )
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  UNSET! (unit type - fits in header bits, may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unset is one of Rebol's two unit types (the other being NONE!).  Whereas
// integers can be values like 1, 2, 3... and LOGIC! can be true or false,
// an UNSET! can be only... unset.  Its type is its value.
//
// By default, variable lookups that come back as unset will result in an
// error, and the average user would not be putting them into blocks on
// purpose.  Hence the Ren-C branch of Rebol3 pushed forward an initiative
// to make unsets frequently denote an intent to "opt out" of things.  This
// involved
//
// Since they have no data payload, unsets in the debug build use Reb_Track
// as their structure to show where-and-when they were assigned.  This is
// helpful because unset is the default initialization value for several
// constructs.
//

#ifdef NDEBUG
    #define SET_UNSET(v) \
        VAL_RESET_HEADER((v), REB_UNSET)

#else
    #define SET_UNSET(v) \
        (VAL_RESET_HEADER((v), REB_UNSET), SET_TRACK_PAYLOAD(v))
#endif

// Pointer to a global protected unset value that can be used when a read-only
// UNSET! value is needed.
//
#define UNSET_VALUE (&PG_Unset_Value[0])

// In legacy mode Ren-C still supports the old convention that IFs that don't
// take the true branch or a WHILE loop that never runs a body return a NONE!
// value instead of an UNSET!.  To track the locations where this decision is
// made more easily, SET_UNSET_UNLESS_LEGACY_NONE() is used.
//
#ifdef NDEBUG
    #define SET_UNSET_UNLESS_LEGACY_NONE(v) \
        SET_UNSET(v) // LEGACY() only available in Debug builds
#else
    #define SET_UNSET_UNLESS_LEGACY_NONE(v) \
        (LEGACY(OPTIONS_NONE_INSTEAD_OF_UNSETS) ? SET_NONE(v) : SET_UNSET(v))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  BAR! and LIT-BAR! (fits in header bits, may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The "expression barrier" is denoted by a lone vertical bar `|`, and it
// has the special property of being rejected for argument fulfillment, but
// ignored at interstitial positions.  So:
//
//     append [a b c] | [d e f] print "Hello"   ;-- will cause an error
//     append [a b c] [d e f] | print "Hello"   ;-- is legal
//
// This makes it similar to an UNSET! in behavior, but given its specialized
// purpose unlikely to conflict as being needed to be passed as an actual
// parameter.  Literal unsets in source are treated differently by the
// evaluator than unsets in variables or coming from the result of a function
// call, so that `append [a b c] something-that-returns-a-bar` is legal.
//
// The other loophole for making barriers is the LIT-BAR!, which can allow
// passing a BAR! by value.  So `append [a b c] '|` would work.
//

#ifdef NDEBUG
    #define SET_BAR(v) \
        VAL_RESET_HEADER((v), REB_BAR)

    #define SET_LIT_BAR(v) \
        VAL_RESET_HEADER((v), REB_LIT_BAR)
#else
    #define SET_BAR(v) \
        (VAL_RESET_HEADER((v), REB_BAR), SET_TRACK_PAYLOAD(v))

    #define SET_LIT_BAR(v) \
        (VAL_RESET_HEADER((v), REB_LIT_BAR), SET_TRACK_PAYLOAD(v))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  NONE! (unit type - fits in header bits, may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unlike UNSET!, none values are inactive.  They do not cause errors
// when they are used in situations like the condition of an IF statement.
// Instead they are considered to be false--like the LOGIC! #[false] value.
// So none is considered to be the other "conditionally false" value.
//
// Only those two values are conditionally false in Rebol, and testing for
// conditional truth and falsehood is frequent.  Hence in addition to its
// type, NONE! also carries a header bit that can be checked for conditional
// falsehood.
//
// Though having tracking information for a none is less frequently useful
// than for TRASH! or UNSET!, it's there in debug builds just in case it
// ever serves a purpose, as the REBVAL payload is not being used.
//

#ifdef NDEBUG
    #define SET_NONE(v) \
        ((v)->header.bits = VALUE_FLAG_FALSE | NOT_END_MASK | REB_NONE)
#else
    #define SET_NONE(v) \
        (Assert_REBVAL_Writable((v), __FILE__, __LINE__), \
            (v)->header.bits = VALUE_FLAG_FALSE | \
            NOT_END_MASK | WRITABLE_MASK_DEBUG | REB_NONE, \
        SET_TRACK_PAYLOAD(v))
#endif

#define NONE_VALUE (&PG_None_Value[0])


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOGIC! (type and value fits in header bits, may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A logic can be either true or false.  For purposes of optimization, logical
// falsehood is indicated by one of the value option bits in the header--as
// opposed to in the value payload.  This means it can be tested quickly and
// that a single check can test for both NONE! and logic false.
//
// Conditional truth and falsehood allows an interpretation where a NONE!
// is a "falsey" value as well as logic false.  Unsets are neither
// conditionally true nor conditionally false, and so debug builds will
// complain if you try to determine which it is.  (It likely means a mistake
// was made in skipping a formal decision-point regarding whether an unset
// should represent an "opt out" or an error.)
//
// Due to no need for payload, it goes ahead and includes a TRACK payload
// in debug builds.
//

#ifdef NDEBUG
    #define SET_TRUE(v) \
        ((v)->header.bits = REB_LOGIC | NOT_END_MASK)

    #define SET_FALSE(v) \
        ((v)->header.bits = REB_LOGIC | NOT_END_MASK \
            | VALUE_FLAG_FALSE)
#else
    #define SET_TRUE(v) \
        (Assert_REBVAL_Writable((v), __FILE__, __LINE__), \
            (v)->header.bits = REB_LOGIC | NOT_END_MASK \
                | WRITABLE_MASK_DEBUG, \
         SET_TRACK_PAYLOAD(v)) // compound

    #define SET_FALSE(v) \
        (Assert_REBVAL_Writable((v), __FILE__, __LINE__), \
            (v)->header.bits = REB_LOGIC | NOT_END_MASK \
            | WRITABLE_MASK_DEBUG | VALUE_FLAG_FALSE, \
         SET_TRACK_PAYLOAD(v))  // compound
#endif

#define SET_LOGIC(v,n)  ((n) ? SET_TRUE(v) : SET_FALSE(v))
#define VAL_LOGIC(v)    NOT(GET_VAL_FLAG((v), VALUE_FLAG_FALSE))

#ifdef NDEBUG
    #define IS_CONDITIONAL_FALSE(v) \
        GET_VAL_FLAG((v), VALUE_FLAG_FALSE)
#else
    // In a debug build, we want to make sure that UNSET! is never asked
    // about its conditional truth or falsehood; it's neither.
    //
    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_Debug(v)
#endif

#define IS_CONDITIONAL_TRUE(v) NOT(IS_CONDITIONAL_FALSE(v))

#define FALSE_VALUE (&PG_False_Value[0])
#define TRUE_VALUE (&PG_True_Value[0])


//
// Rebol Symbol
//
// !!! Historically Rebol used an unsigned 32-bit integer as a "symbol ID".
// These symbols did not participate in garbage collection and had to be
// looked up in a table to get their values.  Ren-C is moving toward adapting
// REBSERs to be able to store words and canon words, as well as GC them.
// This starts moving the types to be the size of a platform pointer.
//

typedef REBUPT REBSYM;


/***********************************************************************
**
**  DATATYPE - Datatype or pseudo-datatype
**
**  !!! Consider rename to TYPE! once legacy TYPE? calls have been
**  converted to TYPE-OF.  Also consider a model where there are
**  user types, and hence TYPE? may be able to return more than just
**  one out of a set of 64 things.
**
***********************************************************************/

struct Reb_Datatype {
    enum Reb_Kind kind;
    REBARR  *spec;
//  REBINT  min_type;
//  REBINT  max_type;
};

#define VAL_TYPE_KIND(v)    ((v)->payload.datatype.kind)
#define VAL_TYPE_KIND_0(v)  TO_0_FROM_KIND(VAL_TYPE_KIND(v))

#define VAL_TYPE_SPEC(v)    ((v)->payload.datatype.spec)

// %words.r is arranged so that symbols for types are at the start
// Although REB_TRASH is 0, the 0 REBCNT used for symbol IDs is reserved
// for "no symbol".  So there is no symbol for the "fake" type TRASH!
//
#define IS_KIND_SYM(s)      ((s) < REB_MAX_0 + 1)
#define KIND_FROM_SYM(s)    cast(enum Reb_Kind, KIND_FROM_0((s) - 1))
#define SYM_FROM_KIND(k) \
    (assert((k) < REB_MAX), cast(REBSYM, TO_0_FROM_KIND(k) + 1))
#define VAL_TYPE_SYM(v)     SYM_FROM_KIND((v)->payload.datatype.kind)

//#define   VAL_MIN_TYPE(v) ((v)->payload.datatype.min_type)
//#define   VAL_MAX_TYPE(v) ((v)->payload.datatype.max_type)


/***********************************************************************
**
**  NUMBERS - Integer and other simple scalars
**
***********************************************************************/

#define VAL_INT32(v)    (REBINT)((v)->payload.integer)
#define VAL_INT64(v)    ((v)->payload.integer)
#define VAL_UNT64(v)    ((v)->payload.unteger)

#define SET_INTEGER(v,n) \
    (VAL_RESET_HEADER(v, REB_INTEGER), ((v)->payload.integer) = (n))

#define MAX_CHAR        0xffff
#define VAL_CHAR(v)     ((v)->payload.character)
#define SET_CHAR(v,n) \
    (VAL_RESET_HEADER((v), REB_CHAR), VAL_CHAR(v) = (n), NOOP)

#define IS_NUMBER(v) \
    (VAL_TYPE(v) == REB_INTEGER || VAL_TYPE(v) == REB_DECIMAL)


/***********************************************************************
**
**  DECIMAL -- Implementation-wise, a 'double'-precision floating
**  point number in C (typically 64-bit).
**
***********************************************************************/

#define VAL_DECIMAL(v)  ((v)->payload.decimal)
#define SET_DECIMAL(v,n) VAL_RESET_HEADER(v, REB_DECIMAL), VAL_DECIMAL(v) = (n)


/***********************************************************************
**
**  MONEY -- Includes denomination and amount
**
**  !!! The naming of "deci" used by MONEY! as "decimal" is a very
**  bad overlap with DECIMAL! and also not very descriptive of what
**  the properties of a "deci" are.  Also, to be a useful money
**  abstraction it should store the currency type, e.g. the three
**  character ISO 4217 code (~15 bits to store)
**
**      https://en.wikipedia.org/wiki/ISO_4217
**
***********************************************************************/

struct Reb_Money {
    deci amount;
};

#define VAL_MONEY_AMOUNT(v)     ((v)->payload.money.amount)
#define SET_MONEY_AMOUNT(v,n) \
    (VAL_RESET_HEADER((v), REB_MONEY), VAL_MONEY_AMOUNT(v) = (n), NOOP)


/***********************************************************************
**
**  DATE and TIME
**
***********************************************************************/

typedef struct reb_ymdz {
#ifdef ENDIAN_LITTLE
    REBINT zone:7;  // +/-15:00 res: 0:15
    REBCNT day:5;
    REBCNT month:4;
    REBCNT year:16;
#else
    REBCNT year:16;
    REBCNT month:4;
    REBCNT day:5;
    REBINT zone:7;  // +/-15:00 res: 0:15
#endif
} REBYMD;

typedef union reb_date {
    REBYMD date;
    REBCNT bits;
} REBDAT;

struct Reb_Time {
    REBI64 time;    // time in nanoseconds
    REBDAT date;
};

#define VAL_TIME(v) ((v)->payload.time.time)
#define TIME_SEC(n) ((REBI64)(n) * 1000000000L)

#define MAX_SECONDS (((i64)1<<31)-1)
#define MAX_HOUR    (MAX_SECONDS / 3600)
#define MAX_TIME    ((REBI64)MAX_HOUR * HR_SEC)

#define NANO        1.0e-9
#define SEC_SEC     ((REBI64)1000000000L)
#define MIN_SEC     (60 * SEC_SEC)
#define HR_SEC      (60 * 60 * SEC_SEC)

#define SEC_TIME(n)  ((n) * SEC_SEC)
#define MIN_TIME(n)  ((n) * MIN_SEC)
#define HOUR_TIME(n) ((n) * HR_SEC)

#define SECS_IN(n) ((n) / SEC_SEC)
#define VAL_SECS(n) (VAL_TIME(n) / SEC_SEC)

#define DEC_TO_SECS(n) (i64)(((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400
#define TIME_IN_DAY (SEC_TIME((i64)SECS_IN_DAY))

#define NO_TIME     MIN_I64

#define MAX_YEAR        0x3fff

#define VAL_DATE(v)     ((v)->payload.time.date)
#define VAL_YEAR(v)     ((v)->payload.time.date.date.year)
#define VAL_MONTH(v)    ((v)->payload.time.date.date.month)
#define VAL_DAY(v)      ((v)->payload.time.date.date.day)
#define VAL_ZONE(v)     ((v)->payload.time.date.date.zone)

#define ZONE_MINS 15
#define ZONE_SECS (ZONE_MINS*60)
#define MAX_ZONE (15 * (60/ZONE_MINS))


/***********************************************************************
**
**  TUPLE
**
***********************************************************************/

typedef struct Reb_Tuple {
    REBYTE tuple[12];
} REBTUP;

#define VAL_TUPLE(v)        ((v)->payload.tuple.tuple + 1)
#define VAL_TUPLE_LEN(v)    ((v)->payload.tuple.tuple[0])
#define MAX_TUPLE 10


/***********************************************************************
**
**  PAIR
**
***********************************************************************/

#define VAL_PAIR(v)     ((v)->payload.pair)
#define VAL_PAIR_X(v)   ((v)->payload.pair.x)
#define VAL_PAIR_Y(v)   ((v)->payload.pair.y)
#define VAL_PAIR_X_INT(v) ROUND_TO_INT((v)->payload.pair.x)
#define VAL_PAIR_Y_INT(v) ROUND_TO_INT((v)->payload.pair.y)

#define SET_PAIR(v,x,y) \
    (VAL_RESET_HEADER(v, REB_PAIR),VAL_PAIR_X(v)=(x),VAL_PAIR_Y(v)=(y))


/****************************************************************************
**
**  ANY-SERIES!
**
*****************************************************************************/

struct Reb_Any_Series
{
    REBSER *series;
    REBCNT index;
};

#ifdef NDEBUG
    #define VAL_SERIES(v)   ((v)->payload.any_series.series)
#else
    #define VAL_SERIES(v)   (*VAL_SERIES_Ptr_Debug(v))
#endif
#define VAL_INDEX(v)        ((v)->payload.any_series.index)
#define VAL_LEN_HEAD(v)     SER_LEN(VAL_SERIES(v))
#define VAL_LEN_AT(v)       (Val_Series_Len_At(v))

#define IS_EMPTY(v)         (VAL_INDEX(v) >= VAL_LEN_HEAD(v))

#define VAL_RAW_DATA_AT(v) \
    SER_AT_RAW(VAL_SERIES(v), VAL_INDEX(v))


// Note: These macros represent things that used to sometimes be functions,
// and sometimes were not.  They could be done without a function call, but
// that would then make them unsafe to use with side-effects:
//
//     Val_Init_Block(Alloc_Tail_Array(parent), child);
//
// The repetitition of the value parameter would lead to the allocation
// running multiple times.  Hence we Caps_Words_With_Underscore to name
// these macros to indicate they are safe by not duplicating their args.
// If erring on the side of caution and making a function call turns out
// to be a problem in profiling, then on a case-by-case basis those
// bottlenecks can be replaced with something more like:
//
//     VAL_RESET_HEADER(value, REB_XXX);
//     ENSURE_SERIES_MANAGED(series);
//     VAL_SERIES(value) = series;
//     VAL_INDEX(value) = index;
//
// (Or perhaps just use proper inlining and support it in those builds.)

#define Val_Init_Series_Index(v,t,s,i) \
    Val_Init_Series_Index_Core((v), (t), (s), (i))

#define Val_Init_Series(v,t,s) \
    Val_Init_Series_Index((v), (t), (s), 0)


/***********************************************************************
**
**  BINARY! (uses `struct Reb_Any_Series`)
**
***********************************************************************/

#define VAL_BIN(v)              BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_HEAD(v)         BIN_HEAD(VAL_SERIES(v))
#define VAL_BIN_AT(v)           BIN_AT(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_BIN_TAIL(v)         BIN_AT(VAL_SERIES(v), VAL_SERIES(v)->tail)

// !!! RE: VAL_BIN_AT_HEAD() see remarks on VAL_ARRAY_AT_HEAD()
//
#define VAL_BIN_AT_HEAD(v,n)    BIN_AT(VAL_SERIES(v), (n))

#define VAL_BYTE_SIZE(v) (BYTE_SIZE(VAL_SERIES(v)))

#define Val_Init_Binary(v,s) \
    Val_Init_Series((v), REB_BINARY, (s))


/***********************************************************************
**
**  ANY-STRING! (uses `struct Reb_Any_Series`)
**
***********************************************************************/

#define VAL_STR_IS_ASCII(v) \
    (VAL_BYTE_SIZE(v) && All_Bytes_ASCII(VAL_BIN_AT(v), VAL_LEN_AT(v)))

#define Val_Init_String(v,s) \
    Val_Init_Series((v), REB_STRING, (s))

#define Val_Init_File(v,s) \
    Val_Init_Series((v), REB_FILE, (s))

#define Val_Init_Tag(v,s) \
    Val_Init_Series((v), REB_TAG, (s))

// Arg is a unicode value:
#define VAL_UNI(v)      UNI_HEAD(VAL_SERIES(v))
#define VAL_UNI_HEAD(v) UNI_HEAD(VAL_SERIES(v))
#define VAL_UNI_AT(v)   UNI_AT(VAL_SERIES(v), VAL_INDEX(v))

#define VAL_ANY_CHAR(v) GET_ANY_CHAR(VAL_SERIES(v), VAL_INDEX(v))


/***********************************************************************
**
**  ANY-ARRAY! (uses `struct Reb_Any_Series`)
**
***********************************************************************/

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
// Must use `(old_style)cast_here` because we may-or-may-not be casting away
// constness in the process, e.g. a series extracted from a const REBVAL.
//
#define VAL_ARRAY(v)            (*(REBARR**)(&VAL_SERIES(v)))
#define VAL_ARRAY_HEAD(v)       ARR_HEAD(VAL_ARRAY(v))
#define VAL_ARRAY_TAIL(v)       ARR_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v))

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v)         ARR_AT(VAL_ARRAY(v), VAL_INDEX(v))
#define VAL_ARRAY_LEN_AT(v)     VAL_LEN_AT(v)

// !!! VAL_ARRAY_AT_HEAD() is a leftover from the old definition of
// VAL_ARRAY_AT().  Unlike SKIP in Rebol, this definition did *not* take
// the current index position of the value into account.  It rather extracted
// the array, counted rom the head, and disregarded the index entirely.
//
// The best thing to do with it is probably to rewrite the use cases to
// not need it.  But at least "AT HEAD" helps communicate what the equivalent
// operation in Rebol would be...and you know it's not just giving back the
// head because it's taking an index.  So  it looks weird enough to suggest
// looking here for what the story is.
//
#define VAL_ARRAY_AT_HEAD(v,n) \
    ARR_AT(VAL_ARRAY(v), (n))

#define VAL_TERM_ARRAY(v)       TERM_ARRAY(VAL_ARRAY(v))

#define Val_Init_Array_Index(v,t,a,i) \
    Val_Init_Series_Index((v), (t), ARR_SERIES(a), (i))

#define Val_Init_Array(v,t,a) \
    Val_Init_Array_Index((v), (t), (a), 0)

#define Val_Init_Block_Index(v,a,i) \
    Val_Init_Array_Index((v), REB_BLOCK, (a), (i))

#define Val_Init_Block(v,s) \
    Val_Init_Block_Index((v), (s), 0)

#define EMPTY_BLOCK     ROOT_EMPTY_BLOCK
#define EMPTY_ARRAY     VAL_ARRAY(ROOT_EMPTY_BLOCK)


/***********************************************************************
**
**  IMAGES, QUADS - RGBA
**
***********************************************************************/

#define QUAD_LEN(s)         SER_LEN(s)

#define QUAD_HEAD(s)        SER_DATA_RAW(s)
#define QUAD_SKIP(s,n)      (QUAD_HEAD(s) + ((n) * 4))
#define QUAD_TAIL(s)        (QUAD_HEAD(s) + (QUAD_LEN(s) * 4))

#define IMG_SIZE(s)         ((s)->misc.size)
#define IMG_WIDE(s)         ((s)->misc.area.wide)
#define IMG_HIGH(s)         ((s)->misc.area.high)
#define IMG_DATA(s)         SER_DATA_RAW(s)

#define VAL_IMAGE_HEAD(v)   QUAD_HEAD(VAL_SERIES(v))
#define VAL_IMAGE_TAIL(v)   QUAD_SKIP(VAL_SERIES(v), VAL_LEN_HEAD(v))
#define VAL_IMAGE_DATA(v)   QUAD_SKIP(VAL_SERIES(v), VAL_INDEX(v))
#define VAL_IMAGE_BITS(v)   cast(REBCNT*, VAL_IMAGE_HEAD(v))
#define VAL_IMAGE_WIDE(v)   (IMG_WIDE(VAL_SERIES(v)))
#define VAL_IMAGE_HIGH(v)   (IMG_HIGH(VAL_SERIES(v)))
#define VAL_IMAGE_LEN(v)    VAL_LEN_AT(v)

#define Val_Init_Image(v,s) \
    Val_Init_Series((v), REB_IMAGE, (s));

//tuple to image! pixel order bytes
#define TO_PIXEL_TUPLE(t) \
    TO_PIXEL_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
        VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)

//tuple to RGBA bytes
#define TO_COLOR_TUPLE(t) \
    TO_RGBA_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
        VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)


/***********************************************************************
**
**  BIT_SET -- Bit sets
**
***********************************************************************/

#define VAL_BITSET(v)   VAL_SERIES(v)

#define VAL_BIT_DATA(v) VAL_BIN(v)

#define SET_BIT(d,n)    ((d)[(n) >> 3] |= (1 << ((n) & 7)))
#define CLR_BIT(d,n)    ((d)[(n) >> 3] &= ~(1 << ((n) & 7)))
#define IS_BIT(d,n)     LOGICAL((d)[(n) >> 3] & (1 << ((n) & 7)))

#define Val_Init_Bitset(v,s) \
    Val_Init_Series((v), REB_BITSET, (s))


/***********************************************************************
**
**  SYMBOLS -- Used only for symbol tables
**
***********************************************************************/

struct Reb_Symbol {
    REBCNT  canon;  // Index of the canonical (first) word
    REBCNT  alias;  // Index to next alias form
    REBCNT  name;   // Index into PG_Word_Names string
};

// Arg is value:
#define VAL_SYM_NINDEX(v)   ((v)->payload.symbol.name)
#define VAL_SYM_NAME(v)     (BIN_HEAD(PG_Word_Names) + VAL_SYM_NINDEX(v))
#define VAL_SYM_CANON(v)    ((v)->payload.symbol.canon)
#define VAL_SYM_ALIAS(v)    ((v)->payload.symbol.alias)

// Return the CANON value for a symbol number:
#define SYMBOL_TO_CANON(sym) \
    VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, sym))

// Return the CANON value for a word value:
#define WORD_TO_CANON(w) \
    VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, VAL_WORD_SYM(w)))

// Is it the same symbol? Quick check, then canon check:
#define SAME_SYM(s1,s2) \
    ((s1) == (s2) \
    || ( \
        VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, (s1))) \
        == VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, (s2))) \
    ))


/***********************************************************************
**
**  WORDS -- All word related types
**
***********************************************************************/

#ifdef NDEBUG
    #define WORD_FLAG 0
#else
    #define WORD_FLAG REB_WORD // interpreted to mean ANY-WORD!
#endif

enum {
    // is bound to a normally GC'd context
    //
    WORD_FLAG_BOUND_SPECIFIC = (1 << (TYPE_SPECIFIC_BIT + 0)) | WORD_FLAG,

    // var is found by combining w/extra info
    //
    WORD_FLAG_BOUND_RELATIVE = (1 << (TYPE_SPECIFIC_BIT + 1)) | WORD_FLAG
};

struct Reb_Any_Word {
    //
    // The context to look in to find the word's value.  It is valid if the
    // word has been bound, and null otherwise.
    //
    // Note that if the ANY-CONTEXT! to which this word is bound is a FRAME!,
    // then that frame may no longer be on the stack.  Hence the space for
    // the variable is no longer allocated.  Tracking mechanisms must be used
    // to make sure the word can keep track of this fact.
    //
    // !!! Tracking mechanism is currently under development.
    //
    // Also note that the expense involved in doing a lookup to a context
    // that is a FRAME! is greater than one that is not (such as an OBJECT!)
    // This is because in the current implementation, the stack must be
    // walked to find the required frame.
    //
    union {
        REBCTX *specific; // for WORD_FLAG_BOUND_SPECIFIC
        REBFUN *relative; // for WORD_FLAG_BOUND_RELATIVE
    } binding;

    // Index of word in context (if word is bound, e.g. `context` is not NULL)
    //
    // !!! Intended logic is that if the index is positive, then the word
    // is looked for in the context's pooled memory data pointer.  If the
    // index is negative or 0, then it's assumed to be a stack variable,
    // and looked up in the CTX_STACKVARS data.
    //
    // But or now there are no examples of contexts which have both pooled
    // and stack memory, and the general issue of mapping the numbers has
    // not been solved.  However, both pointers are available to a context
    // so it's awaiting some solution for a reasonably-performing way to
    // do the mapping from [1 2 3 4 5 6] to [-3 -2 -1 0 1 2] (or whatever)
    //
    REBINT index;

    // Index of the word's symbol
    //
    // Note: Future expansion plans are to have symbol entries tracked by
    // pointer and garbage collected, likely as series nodes.  A full pointer
    // sized value is required here.
    //
    REBSYM sym;
};

#define IS_WORD_BOUND(v) \
    (assert(ANY_WORD(v)), \
        GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC) \
        || GET_VAL_FLAG((v), WORD_FLAG_BOUND_RELATIVE)) // !!! => test both

#define IS_WORD_UNBOUND(v) \
    NOT(IS_WORD_BOUND(v))

#define VAL_WORD_SYM(v) \
    (assert(ANY_WORD(v)), (v)->payload.any_word.sym)
#define INIT_WORD_SYM(v,s) \
    (assert(ANY_WORD(v)), (v)->payload.any_word.sym = (s))

// !!! Today indices for both stackvars and durable vars in the varlist are
// done with positive numbers.  But when "hybrids" exist, the indexing scheme
// will need to differentiate...with positive numbers for the durable vars
// and negative numbers for the stack vars, to distingiush where to get data.
//
#define VAL_WORD_INDEX(v) \
    (assert(ANY_WORD(v)), (v)->payload.any_word.index)
#define INIT_WORD_INDEX(v,i) \
    (assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC) \
        ? (i) >= 1 && SAME_SYM( \
            VAL_WORD_SYM(v), CTX_KEY_SYM(VAL_WORD_CONTEXT(v), (i)) \
        ) : (i) >= 1), \
        (v)->payload.any_word.index = (i))

#define VAL_WORD_CONTEXT(v) \
    (assert(ANY_WORD(v) && GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC)), \
        (v)->payload.any_word.binding.specific)

#define VAL_WORD_CONTEXT_MAY_REIFY(v) \
    (assert(ANY_WORD(v)), GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC)) \
        ? (v)->payload.any_word.binding.specific \
        : Frame_For_Call_May_Reify( \
            Call_For_Relative_Word((v), FALSE), NULL, TRUE)

#define INIT_WORD_SPECIFIC(v,context) \
    (assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC) \
        && !GET_VAL_FLAG((v), WORD_FLAG_BOUND_RELATIVE)), \
        ENSURE_SERIES_MANAGED(CTX_SERIES(context)), \
        assert(GET_ARR_FLAG(CTX_KEYLIST(context), SERIES_FLAG_MANAGED)), \
        (v)->payload.any_word.binding.specific = (context))

#define INIT_WORD_RELATIVE(v,func) \
    (assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND_RELATIVE) \
        && !GET_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC)), \
        (v)->payload.any_word.binding.relative = (func))

#define IS_SAME_WORD(v, n) \
    (IS_WORD(v) && VAL_WORD_CANON(v) == n)

#ifdef NDEBUG
    #define UNBIND_WORD(v) \
        (CLEAR_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC), \
            CLEAR_VAL_FLAG((v), WORD_FLAG_BOUND_RELATIVE))
#else
    #define UNBIND_WORD(v) \
        (CLEAR_VAL_FLAG((v), WORD_FLAG_BOUND_SPECIFIC), \
            CLEAR_VAL_FLAG((v), WORD_FLAG_BOUND_RELATIVE), \
            (v)->payload.any_word.index = 0)
#endif

#define VAL_WORD_CANON(v) \
    VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, VAL_WORD_SYM(v)))

#define VAL_WORD_NAME(v) \
    VAL_SYM_NAME(ARR_AT(PG_Word_Table.array, VAL_WORD_SYM(v)))

#define VAL_WORD_NAME_STR(v)    BIN_HEAD(VAL_WORD_NAME(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  TYPESET! (`struct Reb_Typeset`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A typeset is a collection of up to 63 types, implemented as a bitset.
// The 0th type corresponds to REB_TRASH and can be used to indicate another
// property of the typeset (though no such uses exist yet).
//
// !!! The limit of only being able to hold a set of 63 types is a temporary
// one, as user-defined types will require a different approach.  Hence the
// best way to look at the bitset for built-in types is as an optimization
// for type-checking the common parameter cases.
//
// Though available to the user to manipulate directly as a TYPESET!, REBVALs
// of this category have another use in describing the fields of objects
// ("KEYS") or parameters of function frames ("PARAMS").  When used for that
// purpose, they not only list the legal types...but also hold a symbol for
// naming the field or parameter.
//
// !!! At present, a TYPESET! created with MAKE TYPESET! cannot set the
// internal symbol.  Nor can it set the extended flags, though that might
// someday be allowed with a syntax like:
//
//      make typeset! [<hide> <quote> <protect> string! integer!]
//

#ifdef NDEBUG
    #define TYPESET_FLAG 0
#else
    #define TYPESET_FLAG REB_TYPESET // interpreted to mean ANY-TYPESET!
#endif

// Option flags used with GET_VAL_FLAG().  These describe properties of
// a value slot when it's constrained to the types in the typeset
//
enum {
    // Quoted (REDUCE group/get-word|path if EVALUATE)
    //
    TYPESET_FLAG_QUOTE = (1 << (TYPE_SPECIFIC_BIT + 0)) | TYPESET_FLAG,

    // DO/NEXT performed at callsite when setting
    //
    TYPESET_FLAG_EVALUATE = (1 << (TYPE_SPECIFIC_BIT + 1)) | TYPESET_FLAG,

    // Value indicates an optional switch
    //
    TYPESET_FLAG_REFINEMENT = (1 << (TYPE_SPECIFIC_BIT + 2)) | TYPESET_FLAG,

    // Can't be changed (set with PROTECT)
    //
    TYPESET_FLAG_LOCKED = (1 << (TYPE_SPECIFIC_BIT + 3)) | TYPESET_FLAG,

    // Can't be reflected (set with PROTECT/HIDE) or local in spec as `foo:`
    //
    TYPESET_FLAG_HIDDEN = (1 << (TYPE_SPECIFIC_BIT + 4)) | TYPESET_FLAG,

    // Can't be bound to beyond the current bindings.
    //
    // !!! This flag was implied in R3-Alpha by TYPESET_FLAG_HIDDEN.  However,
    // the movement of SELF out of being a hardcoded keyword in the binding
    // machinery made it start to be considered as being a by-product of the
    // generator, and hence a "userspace" word (like definitional return).
    // To avoid disrupting all object instances with a visible SELF, it was
    // made hidden...which worked until a bugfix restored the functionality
    // of checking to not bind to hidden things.  UNBINDABLE is an interim
    // solution to separate the property of bindability from visibility, as
    // the SELF solution shakes out--so that SELF may be hidden but bind.
    //
    TYPESET_FLAG_UNBINDABLE = (1 << (TYPE_SPECIFIC_BIT + 5)) | TYPESET_FLAG,

    // !!! <durable> is the working name for the property of a function
    // argument or local to have its data survive after the call is over.
    // Much of the groundwork has been laid to allow this to be specified
    // individually for each argument, but the feature currently is "all
    // or nothing"--and implementation-wise corresponds to what R3-Alpha
    // called CLOSURE!, with the deep-copy-per-call that entails.
    //
    // Hence if this property is applied, it will be applied to *all* of
    // a function's arguments.
    //
    TYPESET_FLAG_DURABLE = (1 << (TYPE_SPECIFIC_BIT + 6)) | TYPESET_FLAG
};

struct Reb_Typeset {
    REBSYM sym;         // Symbol (if a key of object or function param)

    // Note: `sym` is first so that the value's 32-bit Reb_Flags header plus
    // the 32-bit REBCNT will pad `bits` to a REBU64 alignment boundary

    REBU64 bits;        // One bit for each DATATYPE! (use with FLAGIT_64)
};

// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->payload.typeset.bits)

#define TYPE_CHECK(v,n) \
    LOGICAL(VAL_TYPESET_BITS(v) & FLAGIT_KIND(n))

#define TYPE_SET(v,n) \
    ((VAL_TYPESET_BITS(v) |= FLAGIT_KIND(n)), NOOP)

#define EQUAL_TYPESET(v,w) \
    (VAL_TYPESET_BITS(v) == VAL_TYPESET_BITS(w))


// Symbol is SYM_0 unless typeset in object keylist or func paramlist

#ifdef NDEBUG
    #define VAL_TYPESET_SYM(v) ((v)->payload.typeset.sym)
#else
    // !!! Due to large reorganizations, it may be that VAL_WORD_SYM and
    // VAL_TYPESET_SYM calls were swapped.  In the aftermath of reorganization
    // this check is prudent (until further notice...)
    #define VAL_TYPESET_SYM(v) (*VAL_TYPESET_SYM_Ptr_Debug(v))
#endif

#define VAL_TYPESET_CANON(v) \
    VAL_SYM_CANON(ARR_AT(PG_Word_Table.array, VAL_TYPESET_SYM(v)))

// Word number array (used by Bind_Table):
#define WORDS_HEAD(w) \
    SER_HEAD(REBINT, (w))

#define WORDS_LAST(w) \
    (WORDS_HEAD(w) + SER_LEN(w) - 1) // (tail never zero)

// Useful variation of FLAGIT_64 for marking a type, as they now involve
// shifting and possible abstraction vs. simply being 0..63
//
#define FLAGIT_KIND(t)          (cast(REBU64, 1) << TO_0_FROM_KIND(t))


//=////////////////////////////////////////////////////////////////////////=//
//
// ANY-CONTEXT! (`struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The Reb_Any_Context is the basic struct used currently for OBJECT!,
// MODULE!, ERROR!, and PORT!.  It builds upon the context datatype REBCTX,
// which permits the storage of associated KEYS and VARS.  (See the comments
// on `struct Reb_Context` that are in %sys-series.h).
//
// Contexts coordinate with words, which can have their VAL_WORD_CONTEXT()
// set to a context's series pointer.  Then they cache the index of that
// word's symbol in the context's keylist, for a fast lookup to get to the
// corresponding var.  The key is a typeset which has several EXT flags
// controlling behaviors like whether the var is protected or hidden.
//
// !!! This "caching" mechanism is not actually "just a cache".  Once bound
// the index is treated as permanent.  This is why objects are "append only"
// because disruption of the index numbers would break the extant words
// with index numbers to that position.  Ren-C intends to undo this by
// paying for the check of the symbol number at the time of lookup, and if
// it does not match consider it a cache miss and re-lookup...adjusting the
// index inside of the word.  For efficiency, some objects could be marked
// as not having this property, but it may be just as efficient to check
// the symbol match as that bit.
//
// Frame key/var indices start at one, and they leave two REBVAL slots open
// in the 0 spot for other uses.  With an ANY-CONTEXT!, the use for the
// "ROOTVAR" is to store a canon value image of the ANY-CONTEXT!'s REBVAL
// itself.  This trick allows a single REBSER* to be passed around rather
// than the REBVAL struct which is 4x larger, yet still reconstitute the
// entire REBVAL if it is needed.
//

struct Reb_Any_Context {
    REBCTX *context;
    REBVAL *stackvars;

    union {
        REBCTX *spec; // used by REB_MODULE
        REBFUN *func; // used by REB_FRAME
    } more;
};

#define VAL_CONTEXT(v) \
    (assert(ANY_CONTEXT(v)), (v)->payload.any_context.context)

#define INIT_VAL_CONTEXT(v,c) \
    ((v)->payload.any_context.context = (c))

#define VAL_CONTEXT_SPEC(v)         ((v)->payload.any_context.more.spec)

#define VAL_CONTEXT_FUNC(v)         ((v)->payload.any_context.more.func)

#define VAL_CONTEXT_STACKVARS(v)    ((v)->payload.any_context.stackvars)

#define VAL_CONTEXT_STACKVARS_LEN(v) \
    (assert(ANY_CONTEXT(v)), \
        CHUNK_LEN_FROM_VALUES((v)->payload.any_context.stackvars))

// Convenience macros to speak in terms of object values instead of the context
//
#define VAL_CONTEXT_VAR(v,n)        CTX_VAR(VAL_CONTEXT(v), (n))
#define VAL_CONTEXT_KEY(v,n)        CTX_KEY(VAL_CONTEXT(v), (n))
#define VAL_CONTEXT_KEY_SYM(v,n)    CTX_KEY_SYM(VAL_CONTEXT(v), (n))

// The movement of the SELF word into the domain of the object generators
// means that an object may wind up having a hidden SELF key (and it may not).
// Ultimately this key may well occur at any position.  While user code is
// discouraged from accessing object members by integer index (`pick obj 1`
// is an error), system code has historically relied upon this.
//
// During a transitional period where all MAKE OBJECT! constructs have a
// "real" SELF key/var in the first position, there needs to be an adjustment
// to the indexing of some of this system code.  Some of these will be
// temporary, because not all objects will need a definitional SELF (just as
// not all functions need a definitional RETURN).  Exactly which require it
// and which do not remains to be seen, so this macro helps review the + 1
// more easily than if it were left as just + 1.
//
#define SELFISH(n) (n + 1)

#define Val_Init_Object(v,c) \
    Val_Init_Context((v), REB_OBJECT, (c))


/***********************************************************************
**
**  MODULES - Code isolation units
**
**  http://www.rebol.com/r3/docs/concepts/modules-defining.html
**
***********************************************************************/

#define VAL_MOD_SPEC(v)     VAL_CONTEXT_SPEC(v)
#define VAL_MOD_BODY(v)     VAL_CONTEXT_BODY(v)

#define Val_Init_Module(v,c) \
    Val_Init_Context((v), REB_MODULE, (c))


/***********************************************************************
**
**  PORTS - External series interface
**
***********************************************************************/

#define Val_Init_Port(v,c) \
    Val_Init_Context((v), REB_PORT, (c))


//=////////////////////////////////////////////////////////////////////////=//
//
// ERROR! (uses `struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Errors are a subtype of ANY-CONTEXT! which follow a standard layout.
// That layout is in %boot/sysobj.r as standard/error.
//
// Historically errors could have a maximum of 3 arguments, with the fixed
// names of `arg1`, `arg2`, and `arg3`.  They would also have a numeric code
// which would be used to look up a a formatting block, which would contain
// a block for a message with spots showing where the args were to be inserted
// into a message.  These message templates can be found in %boot/errors.r
//
// Ren-C is exploring the customization of user errors to be able to provide
// arbitrary named arguments and message templates to use them.  It is
// a work in progress, but refer to the FAIL native, the corresponding
// `fail()` C macro inside the source, and the various routines in %c-error.c
//

#define ERR_VALUES(e)   cast(ERROR_OBJ*, ARR_HEAD(CTX_VARLIST(e)))
#define ERR_NUM(e)      cast(REBCNT, VAL_INT32(&ERR_VALUES(e)->code))

#define VAL_ERR_VALUES(v)   ERR_VALUES(VAL_CONTEXT(v))
#define VAL_ERR_NUM(v)      ERR_NUM(VAL_CONTEXT(v))

#define Val_Init_Error(v,c) \
    Val_Init_Context((v), REB_ERROR, (c))


/***********************************************************************
**
**  FUNCTIONS - Natives, actions, operators, and user functions
**
**  NOTE: make-headers.r will skip specs with the "REBNATIVE(" in them
**  REBTYPE macros are used and expanded in tmp-funcs.h
**
***********************************************************************/

#ifdef NDEBUG
    #define FUNC_FLAG 0
#else
    #define FUNC_FLAG REB_FUNCTION // interpreted to mean ANY-FUNCTION!
#endif

enum {
    // called with "infix" protocol
    //
    FUNC_FLAG_INFIX = (1 << (TYPE_SPECIFIC_BIT + 0)) | FUNC_FLAG,

    // function "fakes" a definitionally scoped return (or a "LEAVE"...which
    // word is determined by the symbol of the *last* parameter)
    //
    FUNC_FLAG_LEAVE_OR_RETURN = (1 << (TYPE_SPECIFIC_BIT + 1)) | FUNC_FLAG,

    // native hooks into DO state and does own arg eval
    //
    FUNC_FLAG_FRAMELESS = (1 << (TYPE_SPECIFIC_BIT + 2)) | FUNC_FLAG,

#if !defined(NDEBUG)
    //
    // TRUE-valued refinements, NONE! for unused args
    //
    FUNC_FLAG_LEGACY = (1 << (TYPE_SPECIFIC_BIT + 3)) | FUNC_FLAG,
#endif

    FUNC_FLAG_NO_COMMA // needed for proper comma termination of this list
};

struct Reb_Call;

// enums in C have no guaranteed size, yet Rebol wants to use known size
// types in its interfaces.  Hence REB_R is a REBCNT from reb-c.h (and not
// this enumerated type containing its legal values).
enum {
    R_OUT = 0,

    // See comments on OPT_VALUE_THROWN about the migration of "thrownness"
    // from being a property signaled to the evaluator.
    //
    // R_OUT_IS_THROWN is a test of that signaling mechanism.  It is currently
    // being kept in parallel with the THROWN() bit and ensured as matching.
    // Being in the state of doing a stack unwind will likely be knowable
    // through other mechanisms even once the thrown bit on the value is
    // gone...so it may not be the case that natives are asked to do their
    // own separate indication, so this may wind up replaced with R_OUT.  For
    // the moment it is good as a double-check.
    //
    R_OUT_IS_THROWN,

    // !!! These R_ values are somewhat superfluous...and actually inefficient
    // because they have to be checked by the caller in a switch statement
    // to take the equivalent action.  They have a slight advantage in
    // hand-written C code for making it more clear that if you have used
    // the D_OUT return slot for temporary work that you explicitly want
    // to specify another result...this cannot be caught by the REB_TRASH
    // trick for detecting an unwritten D_OUT.
    //
    R_UNSET, // => SET_UNSET(D_OUT); return R_OUT;
    R_NONE, // => SET_NONE(D_OUT); return R_OUT;
    R_TRUE, // => SET_TRUE(D_OUT); return R_OUT;
    R_FALSE // => SET_FALSE(D_OUT); return R_OUT;
};
typedef REBCNT REB_R;

// NATIVE! function
typedef REB_R (*REBNAT)(struct Reb_Call *call_);
#define REBNATIVE(n) \
    REB_R N_##n(struct Reb_Call *call_)

// ACTION! function (one per each DATATYPE!)
typedef REB_R (*REBACT)(struct Reb_Call *call_, REBCNT a);
#define REBTYPE(n) \
    REB_R T_##n(struct Reb_Call *call_, REBCNT action)

// PORT!-action function
typedef REB_R (*REBPAF)(struct Reb_Call *call_, REBCTX *p, REBCNT a);

// COMMAND! function
typedef REB_R (*CMD_FUNC)(REBCNT n, REBSER *args);

typedef struct Reb_Routine_Info REBRIN;

struct Reb_Any_Function {
    //
    // Array of spec values for function
    //
    REBARR *spec;

    // `func` is a Rebol Array that contains an ANY-FUNCTION! value that
    // represents the function as its [0]th element, and hence is a single
    // pointer which can be used to access all the function's properties
    // via the FUNC_VALUE macro.  The rest of the array is the parameter
    // definitions (TYPESET! values plus name symbol)
    //
    REBFUN *func;

    union Reb_Any_Function_Impl {
        REBNAT code;
        REBARR *body;
        REBCNT act;
        REBRIN *info;
    } impl;
};

/* argument is of type REBVAL* */
#ifdef NDEBUG
    #define VAL_FUNC(v)             ((v)->payload.any_function.func + 0)
#else
    #define VAL_FUNC(v)             VAL_FUNC_Debug(v)
#endif
#define VAL_FUNC_SPEC(v)            ((v)->payload.any_function.spec)
#define VAL_FUNC_PARAMLIST(v)       FUNC_PARAMLIST(VAL_FUNC(v))

#define VAL_FUNC_NUM_PARAMS(v)      FUNC_NUM_PARAMS(VAL_FUNC(v))
#define VAL_FUNC_PARAMS_HEAD(v)     FUNC_PARAMS_HEAD(VAL_FUNC(v))
#define VAL_FUNC_PARAM(v,p)         FUNC_PARAM(VAL_FUNC(v), (p))

#define VAL_FUNC_CODE(v)      ((v)->payload.any_function.impl.code)
#define VAL_FUNC_BODY(v)      ((v)->payload.any_function.impl.body)
#define VAL_FUNC_ACT(v)       ((v)->payload.any_function.impl.act)
#define VAL_FUNC_INFO(v)      ((v)->payload.any_function.impl.info)

// FUNC_FLAG_LEAVE_OR_RETURN functions use RETURN or LEAVE native's function
// value to give the definitional return its prototype, but overwrite its
// code pointer to hold the paramlist of the target.
//
// Do_Native_Throws() sees when someone tries to execute one of these "native
// returns"...and instead interprets it as a THROW whose /NAME is the function
// value.  The paramlist has that value (it's the REBVAL in slot #0)  In this
// way the illusion of a "new function being created on each call" is given.
//
// This is a special case: the body value of the hacked REBVAL of the return
// is allowed to be inconsistent with the content of the ROOT_RETURN_NATIVE's
// actual FUNC.  (In the general case, the [0] element of the FUNC must be
// consistent with the fields of the value holding it.)
//
#define VAL_FUNC_EXIT_FROM(v) VAL_FUNC_BODY(v)

// !!! At the moment functions are "all durable" or "none durable" w.r.t. the
// survival of their arguments and locals after the call.  This corresponds
// to the CLOSURE! and FUNCTION! distinction for the moment, and brings
// about two things: specific binding *and* durability.
//
#define IS_FUNC_DURABLE(f) \
    LOGICAL(VAL_FUNC_NUM_PARAMS(f) != 0 \
        && GET_VAL_FLAG(VAL_FUNC_PARAM((f), 1), TYPESET_FLAG_DURABLE))


/***********************************************************************
**
**  HANDLE
**
**  Type for holding an arbitrary code or data pointer inside
**  of a Rebol data value.  What kind of function or data is not
**  known to the garbage collector, so it ignores it.
**
**  !!! Review usages of this type where they occur
**
***********************************************************************/

struct Reb_Handle {
    union {
        CFUNC *code;
        void *data;
    } thing;
};

#define VAL_HANDLE_CODE(v) \
    ((v)->payload.handle.thing.code)

#define VAL_HANDLE_DATA(v) \
    ((v)->payload.handle.thing.data)

#define SET_HANDLE_CODE(v,c) \
    (VAL_RESET_HEADER((v), REB_HANDLE), VAL_HANDLE_CODE(v) = (c))

#define SET_HANDLE_DATA(v,d) \
    (VAL_RESET_HEADER((v), REB_HANDLE), VAL_HANDLE_DATA(v) = (d))

// !!! The logic used to be an I32 but now it's folded in as a value flag
// Usages of this should be reviewed.
//
#define VAL_I32(v) ((v)->payload.rebcnt)


/***********************************************************************
**
**  LIBRARY -- External library management structures
**
***********************************************************************/

typedef struct Reb_Library_Handle {
    void * fd;
    REBFLGS flags;
} REBLHL;

struct Reb_Library {
    REBLHL *handle;
    REBARR *spec;
};

#define LIB_FD(v)           ((v)->fd)
#define LIB_FLAGS(v)        ((v)->flags)

#define VAL_LIB(v)          ((v)->payload.library)
#define VAL_LIB_SPEC(v)     ((v)->payload.library.spec)
#define VAL_LIB_HANDLE(v)   ((v)->payload.library.handle)
#define VAL_LIB_FD(v)       ((v)->payload.library.handle->fd)
#define VAL_LIB_FLAGS(v)    ((v)->payload.library.handle->flags)

enum {
    LIB_MARK = 1,       // library was found during GC mark scan.
    LIB_USED = 1 << 1,
    LIB_CLOSED = 1 << 2
};

#define LIB_SET_FLAG(s, f)  (LIB_FLAGS(s) |= (f))
#define LIB_CLR_FLAG(s, f)  (LIB_FLAGS(s) &= ~(f))
#define LIB_GET_FLAG(s, f) (LIB_FLAGS(s) &  (f))

#define MARK_LIB(s)    LIB_SET_FLAG(s, LIB_MARK)
#define UNMARK_LIB(s)  LIB_CLR_FLAG(s, LIB_MARK)
#define IS_MARK_LIB(s) LIB_GET_FLAG(s, LIB_MARK)

#define USE_LIB(s)     LIB_SET_FLAG(s, LIB_USED)
#define UNUSE_LIB(s)   LIB_CLR_FLAG(s, LIB_USED)
#define IS_USED_LIB(s) LIB_GET_FLAG(s, LIB_USED)

#define IS_CLOSED_LIB(s)    LIB_GET_FLAG(s, LIB_CLOSED)
#define CLOSE_LIB(s)        LIB_SET_FLAG(s, LIB_CLOSED)
#define OPEN_LIB(s)         LIB_CLR_FLAG(s, LIB_CLOSED)

/***********************************************************************
**
**  STRUCT -- C Structures
**
***********************************************************************/

typedef struct Reb_Struct {
    REBARR  *spec;
    REBSER  *fields;    // fields definition
    REBSER  *data;
} REBSTU;

#define VAL_STRUCT(v)           ((v)->payload.structure)
#define VAL_STRUCT_SPEC(v)      ((v)->payload.structure.spec)
#define VAL_STRUCT_FIELDS(v)    ((v)->payload.structure.fields)
#define VAL_STRUCT_DATA(v)      ((v)->payload.structure.data)
#define VAL_STRUCT_DP(v)        (BIN_HEAD(VAL_STRUCT_DATA(v)))


/***********************************************************************
**
**  ROUTINE -- External library routine structures
**
***********************************************************************/
struct Reb_Routine_Info {
    union {
        struct {
            REBLHL  *lib;
            CFUNC *funcptr;
        } rot;
        struct {
            void *closure;
            REBFUN *func;
            void *dispatcher;
        } cb;
    } info;
    void    *cif;
    REBSER  *arg_types; /* index 0 is the return type, */
    REBARR  *fixed_args;
    REBARR  *all_args;
    REBARR  *arg_structs; /* for struct arguments */
    REBSER  *extra_mem; /* extra memory that needs to be free'ed */
    REBCNT  flags; // !!! 32-bit...should it use REBFLGS for 64-bit on 64-bit?
    REBINT  abi;
};

typedef REBFUN REBROT;

enum {
    ROUTINE_MARK = 1,       // routine was found during GC mark scan.
    ROUTINE_USED = 1 << 1,
    ROUTINE_CALLBACK = 1 << 2, //this is a callback
    ROUTINE_VARARGS = 1 << 3 //this is a function with varargs
};

/* argument is REBFCN */

#define ROUTINE_SPEC(v)             FUNC_SPEC(v)
#define ROUTINE_INFO(v)             FUNC_INFO(v)
#define ROUTINE_PARAMLIST(v)        FUNC_PARAMLIST(v)
#define ROUTINE_FUNCPTR(v)          (ROUTINE_INFO(v)->info.rot.funcptr)
#define ROUTINE_LIB(v)              (ROUTINE_INFO(v)->info.rot.lib)
#define ROUTINE_ABI(v)              (ROUTINE_INFO(v)->abi)
#define ROUTINE_FFI_ARG_TYPES(v)    (ROUTINE_INFO(v)->arg_types)
#define ROUTINE_FIXED_ARGS(v)       (ROUTINE_INFO(v)->fixed_args)
#define ROUTINE_ALL_ARGS(v)         (ROUTINE_INFO(v)->all_args)
#define ROUTINE_FFI_ARG_STRUCTS(v)  (ROUTINE_INFO(v)->arg_structs)
#define ROUTINE_EXTRA_MEM(v)        (ROUTINE_INFO(v)->extra_mem)
#define ROUTINE_CIF(v)              (ROUTINE_INFO(v)->cif)
#define ROUTINE_RVALUE(v)           VAL_STRUCT(ARR_HEAD(ROUTINE_FFI_ARG_STRUCTS(v)))
#define ROUTINE_CLOSURE(v)          (ROUTINE_INFO(v)->info.cb.closure)
#define ROUTINE_DISPATCHER(v)       (ROUTINE_INFO(v)->info.cb.dispatcher)
#define CALLBACK_FUNC(v)            (ROUTINE_INFO(v)->info.cb.func)

/* argument is REBRIN */

#define RIN_FUNCPTR(v)              ((v)->info.rot.funcptr)
#define RIN_LIB(v)                  ((v)->info.rot.lib)
#define RIN_CLOSURE(v)              ((v)->info.cb.closure)
#define RIN_FUNC(v)                 ((v)->info.cb.func)
#define RIN_ARGS_STRUCTS(v)         ((v)->arg_structs)
#define RIN_RVALUE(v)               VAL_STRUCT(ARR_HEAD(RIN_ARGS_STRUCTS(v)))

#define ROUTINE_FLAGS(s)       ((s)->flags)
#define ROUTINE_SET_FLAG(s, f) (ROUTINE_FLAGS(s) |= (f))
#define ROUTINE_CLR_FLAG(s, f) (ROUTINE_FLAGS(s) &= ~(f))
#define ROUTINE_GET_FLAG(s, f) LOGICAL(ROUTINE_FLAGS(s) & (f))

#define IS_CALLBACK_ROUTINE(s) ROUTINE_GET_FLAG(s, ROUTINE_CALLBACK)

/* argument is REBVAL */
#define VAL_ROUTINE(v)              VAL_FUNC(v)
#define VAL_ROUTINE_SPEC(v)         VAL_FUNC_SPEC(v)
#define VAL_ROUTINE_INFO(v)         VAL_FUNC_INFO(v)
#define VAL_ROUTINE_PARAMLIST(v)    VAL_FUNC_PARAMLIST(v)
#define VAL_ROUTINE_FUNCPTR(v)      (VAL_ROUTINE_INFO(v)->info.rot.funcptr)
#define VAL_ROUTINE_LIB(v)          (VAL_ROUTINE_INFO(v)->info.rot.lib)
#define VAL_ROUTINE_ABI(v)          (VAL_ROUTINE_INFO(v)->abi)
#define VAL_ROUTINE_FFI_ARG_TYPES(v)    (VAL_ROUTINE_INFO(v)->arg_types)
#define VAL_ROUTINE_FIXED_ARGS(v)   (VAL_ROUTINE_INFO(v)->fixed_args)
#define VAL_ROUTINE_ALL_ARGS(v)     (VAL_ROUTINE_INFO(v)->all_args)
#define VAL_ROUTINE_FFI_ARG_STRUCTS(v)  (VAL_ROUTINE_INFO(v)->arg_structs)
#define VAL_ROUTINE_EXTRA_MEM(v)    (VAL_ROUTINE_INFO(v)->extra_mem)
#define VAL_ROUTINE_CIF(v)          (VAL_ROUTINE_INFO(v)->cif)

#define VAL_ROUTINE_RVALUE(v) \
    VAL_STRUCT(ARR_HEAD(VAL_ROUTINE_INFO(v)->arg_structs))

#define VAL_ROUTINE_CLOSURE(v)      (VAL_ROUTINE_INFO(v)->info.cb.closure)
#define VAL_ROUTINE_DISPATCHER(v)   (VAL_ROUTINE_INFO(v)->info.cb.dispatcher)
#define VAL_CALLBACK_FUNC(v)        (VAL_ROUTINE_INFO(v)->info.cb.func)


/***********************************************************************
**
**  EVENT
**
***********************************************************************/

#define VAL_EVENT_TYPE(v)   ((v)->payload.event.type)  //(VAL_EVENT_INFO(v) & 0xff)
#define VAL_EVENT_FLAGS(v)  ((v)->payload.event.flags) //((VAL_EVENT_INFO(v) >> 16) & 0xff)
#define VAL_EVENT_WIN(v)    ((v)->payload.event.win)   //((VAL_EVENT_INFO(v) >> 24) & 0xff)
#define VAL_EVENT_MODEL(v)  ((v)->payload.event.model)
#define VAL_EVENT_DATA(v)   ((v)->payload.event.data)
#define VAL_EVENT_TIME(v)   ((v)->payload.event.time)
#define VAL_EVENT_REQ(v)    ((v)->payload.event.eventee.req)

// !!! Because 'eventee.ser' is exported to clients who may not have the full
// definitions of Rebol's internal types like REBSER available, it is defined
// as a 'void*'.  This "dereference a cast of an address as a double-pointer"
// trick allows us to use VAL_EVENT_SER on the left hand of an assignment,
// but means that 'v' cannot be const to use this on the right hand side.
// An m_cast will have to be used in those cases (or split up this macro)
#define VAL_EVENT_SER(v) \
    (*cast(REBSER **, &(v)->payload.event.eventee.ser))

#define IS_EVENT_MODEL(v,f) (VAL_EVENT_MODEL(v) == (f))

#define SET_EVENT_INFO(val, type, flags, win) \
    VAL_EVENT_TYPE(val)=type, VAL_EVENT_FLAGS(val)=flags, VAL_EVENT_WIN(val)=win
    //VAL_EVENT_INFO(val) = (type | (flags << 16) | (win << 24))

#define VAL_EVENT_X(v)      ((REBINT) (short) (VAL_EVENT_DATA(v) & 0xffff))
#define VAL_EVENT_Y(v)      ((REBINT) (short) ((VAL_EVENT_DATA(v) >> 16) & 0xffff))
#define VAL_EVENT_XY(v)     (VAL_EVENT_DATA(v))
#define SET_EVENT_XY(v,x,y) VAL_EVENT_DATA(v) = ((y << 16) | (x & 0xffff))

#define VAL_EVENT_KEY(v)    (VAL_EVENT_DATA(v) & 0xffff)
#define VAL_EVENT_KCODE(v)  ((VAL_EVENT_DATA(v) >> 16) & 0xffff)
#define SET_EVENT_KEY(v,k,c) VAL_EVENT_DATA(v) = ((c << 16) + k)

#define IS_KEY_EVENT(type)  0


/***********************************************************************
**
**  GOBS - Graphic Objects
**
***********************************************************************/

#pragma pack(pop)
    #include "reb-gob.h"
#pragma pack(push,4)

struct Reb_Gob {
    REBGOB *gob;
    REBCNT index;
};

#define VAL_GOB(v)          ((v)->payload.gob.gob)
#define VAL_GOB_INDEX(v)    ((v)->payload.gob.index)
#define SET_GOB(v,g) \
    (VAL_RESET_HEADER(v, REB_GOB), VAL_GOB(v) = g, VAL_GOB_INDEX(v) = 0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBOL VALUE DEFINITION (`struct Reb_Value`)
//
//=////////////////////////////////////////////////////////////////////////=//

// Reb_All is a structure type designed specifically for getting at
// the underlying bits of whichever union member is in effect inside
// the Reb_Value_Data.  This is not actually legal, although if types
// line up in unions it could be possibly be made "more legal":
//
//     http://stackoverflow.com/questions/11639947/
//
struct Reb_All {
#if defined(__LP64__) || defined(__LLP64__)
    REBCNT bits[6];
#else
    REBCNT bits[3];
#endif
};

#define VAL_ALL_BITS(v) ((v)->payload.all.bits)

union Reb_Value_Payload {
    struct Reb_All all;

    REBCNT rebcnt;
    REBI64 integer;
    REBU64 unteger;
    REBDEC decimal; // actually a C 'double', typically 64-bit
    REBUNI character; // It's CHAR! (for now), but 'char' is a C keyword

    struct Reb_Pair pair;
    struct Reb_Money money;
    struct Reb_Handle handle;
    struct Reb_Time time;
    struct Reb_Tuple tuple;
    struct Reb_Datatype datatype;
    struct Reb_Typeset typeset;

    struct Reb_Any_Word any_word;
    struct Reb_Any_Series any_series;
    struct Reb_Any_Context any_context;

    struct Reb_Any_Function any_function;

    struct Reb_Library library;
    struct Reb_Struct structure; // It's STRUCT!, but 'struct' is a C keyword

    struct Reb_Event event;
    struct Reb_Gob gob;

    struct Reb_Symbol symbol; // internal

#if !defined(NDEBUG) && defined(TRACK_EMPTY_PAYLOADS)
    struct Reb_Track track; // debug only (for TRASH!, UNSET!, NONE!, LOGIC!)
#endif
};

struct Reb_Value
{
    struct Reb_Value_Header header;
    union Reb_Value_Payload payload;
};

#pragma pack(pop)


//=////////////////////////////////////////////////////////////////////////=//
//
//  DEBUG PROBING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This small macro can be inserted into code to probe a value in debug
// builds.  It takes a REBVAL* and an optional message:
//
//     REBVAL *v = Some_Value_Pointer();
//
//     PROBE(v);
//     PROBE_MSG(v, "some value");
//
// In order to make it easier to find out where a piece of debug spew is
// coming from, the file and line number are included.
//

#if !defined(NDEBUG)
    #define PROBE(v) \
        Probe_Core_Debug(NULL, __FILE__, __LINE__, (v))

    #define PROBE_MSG(v, m) \
        Probe_Core_Debug((m), __FILE__, __LINE__, (v))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some REBVALs contain one or more series that need to be guarded.  With
// PUSH_GUARD_VALUE() it is possible to not worry about what series are in
// a value, as it will take care of it if there are any.  As with series
// guarding, the last value guarded must be the first one you DROP_GUARD on.
//

#define PUSH_GUARD_VALUE(v) \
    Guard_Value_Core(v)

#define DROP_GUARD_VALUE(v) \
    do { \
        GC_Value_Guard->content.dynamic.len--; \
        assert((v) == cast(REBVAL **, GC_Value_Guard->content.dynamic.data)[ \
            GC_Value_Guard->content.dynamic.len \
        ]); \
    } while (0)


#endif // %sys-value.h
