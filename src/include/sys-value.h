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
// %sys-series.h for an explanation of REBSER, REBARR, REBFRM, and REBMAP.)
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

struct Reb_Frame;
typedef struct Reb_Frame REBFRM; // parallel REBARR key/var arrays, +2 values

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
    REBUPT all;
};

// `NOT_END_MASK`
//
// If set, it means this is *not* an end marker.  The bit is picked
// strategically to be in the negative and in the lowest order position.
// This means that any even pointer-sized value (such as...all pointers)
// will have its zero bit set, and thus implicitly signal an end.
//
// If this bit is 0, it means that *no other header bits are valid*,
// as it may contain arbitrary data used for non-REBVAL purposes.
//
#define NOT_END_MASK 0x01

// `SETTABLE_MASK_DEBUG`
//
// This is for the debug build, to make it safer to use the implementation
// trick of NOT_END_MASK.  It indicates the slot is "REBVAL sized", and can
// be written into--including to be written with SET_END().
//
// It's again a strategic choice--the 2nd lowest bit and in the negative.
// On *most* known platforms, testing an arbitrary pointer value for
// this bit will give 0 and suggest it is *not* a REBVAL (while still
// indicating an END because of the 0 in the lowest bit).  By checking the
// bit before writing a header, a pointer within a container doing
// double-duty as an implicit terminator for the contained values can
// trigger an alert if the values try to overwrite it.
//
// !!! This checking feature is not fully implemented, but will be soon.
//
#ifdef NDEBUG
    //
    // The assumption that (pointer % 2 = 0) is a very safe one on all known
    // platforms Rebol might run on.  But although (pointer % 4 = 0) is almost
    // always true, it has created porting problems for other languages:
    //
    // http://stackoverflow.com/questions/19991506
    //
    // Hence this check is debug-only, and should be easy to switch off.
    // The release build should not make assumptions about using this
    // second bit for any other purpose.
    //
#else
    // We want to be assured that we are not trying to take the type of a
    // value that is actually an END marker, because end markers chew out only
    // one bit--the rest is allowed to be anything (a pointer value, etc.)
    //
    #define SETTABLE_MASK_DEBUG 0x02
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
// header slot with the lowest bit set to 0.  (See OPTS_VALUE_NOT_END for
// an explanation of this choice.)  The upshot is that a data structure
// designed to hold Rebol arrays is able to terminate an array at full
// capacity with a pointer-sized value with the lowest 2 bits clear, and
// use the rest of the bits for other purposes.  (See OPTS_VALUE_REBVAL_DEBUG
// for why it's the low 2 bits and not just the lowest bit.)
//
// This means not only is a full REBVAL not needed to terminate, the sunk cost
// of an existing pointer can be used to avoid needing even 1/4 of a REBVAL
// for a header to terminate.  (See the `prev` pointer in `struct Reb_Chunk`
// from %sys-stack.h for a simple example of the technique.)
//
// !!! Because Rebol Arrays (REBARR) have both a length and a terminator, it
// is important to keep these in sync.  R3-Alpha sought to give code the
// freedom to work with unterminated arrays if the cost of writing terminators
// was not necessary.  Ren-C pushed back against this to try and be more
// uniform to get the invariants under control.  A formal balance is still
// being sought of when terminators will be required and when they will not.
//

#define IS_END(v)           ((v)->header.all % 2 == 0)
#define NOT_END(v)          ((v)->header.all % 2 == 1)

#ifdef NDEBUG
    #define SET_END(v)      ((v)->header.all = 0)
#else
    #define SET_END(v)      SET_END_Debug(v)
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
    // `OPT_VALUE_FALSE`
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
    OPT_VALUE_FALSE,

    // `OPT_VALUE_LINE`
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
    OPT_VALUE_LINE,

    // `OPT_VALUE_THROWN`
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
    OPT_VALUE_THROWN,

    OPT_VALUE_MAX
};

// Reading/writing routines for the 8 "OPTS" flags, which are in the lowest
// 8 bits.  (They need to be lowest for the OPT_NOT_END trick to work.)
//
#define VAL_SET_OPT(v,n)    ((v)->header.all |= ((1 << (n)) << 8))
#define VAL_GET_OPT(v,n)    (((v)->header.all & ((1 << (n)) << 8)) != 0)
#define VAL_CLR_OPT(v,n) \
    ((v)->header.all &= ~cast(REBUPT, (1 << (n)) << 8))


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
        cast(enum Reb_Kind, ((v)->header.all & HEADER_TYPE_MASK) >> 2)
#else
    #define VAL_TYPE(v) \
        VAL_TYPE_Debug(v)
#endif

// SET_TYPE only sets the type bits, with other header bits intact.
// More frequently one wants to clear the bits, use VAL_RESET_HEADER for that.
//
#define VAL_SET_TYPE(v,t) \
    ((v)->header.all &= ~cast(REBUPT, HEADER_TYPE_MASK), \
        (v)->header.all |= ((t) << 2))

// VAL_RESET_HEADER clears out the header and sets it to a new type (and also
// sets the option bits indicating the value is *not* an END marker, and
// that the value is a full cell which can be written to).  It is legal
// to use this to set REB_TRASH in the debug build.
//
#ifdef NDEBUG
    #define VAL_RESET_HEADER(v,t) \
        ((v)->header.all = NOT_END_MASK | ((t) << 2))
#else
    // The debug build includes an extra check that the value we are about
    // to write the header of is actually a full REBVAL-sized slot...and not
    // just an implicit END marker that's really doing double-duty as some
    // internal pointer of a container structure.
    //
    #define VAL_RESET_HEADER(v,t) \
        VAL_RESET_HEADER_Debug((v),(t)) // not a `do {}` for macro inlining
#endif

// !!! SET_ZEROED is a macro-capture of a dodgy behavior of R3-Alpha,
// which was to assume that clearing the payload of a value and then setting
// the header made it the `zero?` of that type.  Review uses.
//
#define SET_ZEROED(v,t) \
    (CLEAR((v), sizeof(REBVAL)), VAL_RESET_HEADER((v),(t)))

// Reading/writing routines for the 8 "EXTS" flags that are interpreted
// differently depending on the VAL_TYPE() of the value.
//
#define VAL_SET_EXT(v,n)    ((v)->header.all |= (1 << ((n) + 16)))
#define VAL_GET_EXT(v,n)    (((v)->header.all & (1 << ((n) + 16))) != 0)
#define VAL_CLR_EXT(v,n) \
    ((v)->header.all &= ~cast(REBUPT, 1 << ((n) + 16)))

// The ability to read and write all the EXTS at once as an 8-bit value.
// Review uses to see if they could be done all as part of the initialization.
//
#define VAL_EXTS_DATA(v) \
    (((v)->header.all & (cast(REBUPT, 0xFF) << 16)) >> 16)

#define VAL_SET_EXTS_DATA(v,e) \
    (((v)->header.all &= ~(cast(REBUPT, 0xFF) << 16)), \
        (v)->header.all |= ((e) << 16))


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

#if !defined(NDEBUG)
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
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRASH! (uses `struct Reb_Track`)
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

#ifdef NDEBUG
    #define SET_TRASH_IF_DEBUG(v) NOOP

    #define SET_TRASH_SAFE(v) SET_UNSET(v)
#else
    enum {
        EXT_TRASH_SAFE = 0,     // GC safe trash (UNSET! in release build)
        EXT_TRASH_MAX
    };

    // Special type check...we don't want to use a VAL_TYPE() == REB_TRASH
    // because VAL_TYPE is supposed to assert on trash
    //
    #define IS_TRASH_DEBUG(v) \
        (((v)->header.all & HEADER_TYPE_MASK) == 0)

    #define SET_TRASH_IF_DEBUG(v) \
        ( \
            (v)->header.all = NOT_END_MASK | SETTABLE_MASK_DEBUG, \
            SET_TRACK_PAYLOAD(v) \
        )

    #define SET_TRASH_SAFE(v) \
        ( \
            (v)->header.all = NOT_END_MASK | SETTABLE_MASK_DEBUG, \
            VAL_SET_EXT((v), EXT_TRASH_SAFE), \
            SET_TRACK_PAYLOAD(v) \
        )
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  UNSET! (unit type - fits in header bits, `struct Reb_Track` if DEBUG)
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
#define UNSET_VALUE ROOT_UNSET_VAL

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
//  NONE! (unit type - fits in header bits, `struct Reb_Track` if DEBUG)
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
        ((v)->header.all = (REB_NONE << 2) | ((1 << OPT_VALUE_FALSE) << 8) | \
            NOT_END_MASK)
#else
    #define SET_NONE(v) \
        ((v)->header.all = (REB_NONE << 2) | ((1 << OPT_VALUE_FALSE) << 8) | \
            NOT_END_MASK | SETTABLE_MASK_DEBUG, \
         SET_TRACK_PAYLOAD(v))
#endif

#define NONE_VALUE ROOT_NONE_VAL


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOGIC! (fits in header bits, `struct Reb_Track` if DEBUG)
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
        ((v)->header.all = (REB_LOGIC << 2) | NOT_END_MASK)

    #define SET_FALSE(v) \
        ((v)->header.all = (REB_LOGIC << 2) | NOT_END_MASK \
            | ((1 << OPT_VALUE_FALSE) << 8))
#else
    #define SET_TRUE(v) \
        ((v)->header.all = (REB_LOGIC << 2) | NOT_END_MASK \
            | SETTABLE_MASK_DEBUG, \
         SET_TRACK_PAYLOAD(v))  // compound

    #define SET_FALSE(v) \
        ((v)->header.all = (REB_LOGIC << 2) | NOT_END_MASK \
            | SETTABLE_MASK_DEBUG | ((1 << OPT_VALUE_FALSE) << 8), \
         SET_TRACK_PAYLOAD(v))  // compound
#endif

#define SET_LOGIC(v,n)  ((n) ? SET_TRUE(v) : SET_FALSE(v))
#define VAL_LOGIC(v)    !VAL_GET_OPT((v), OPT_VALUE_FALSE)

#ifdef NDEBUG
    #define IS_CONDITIONAL_FALSE(v) \
        VAL_GET_OPT((v), OPT_VALUE_FALSE)
#else
    // In a debug build, we want to make sure that UNSET! is never asked
    // about its conditional truth or falsehood; it's neither.
    //
    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_Debug(v)
#endif

#define IS_CONDITIONAL_TRUE(v) !IS_CONDITIONAL_FALSE(v)


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
#define VAL_TYPE_SPEC(v)    ((v)->payload.datatype.spec)

// %words.r is arranged so that symbols for types are at the start
// Although REB_TRASH is 0, the 0 REBCNT used for symbol IDs is reserved
// for "no symbol".  So there is no symbol for the "fake" type TRASH!
//
#define IS_KIND_SYM(s)      ((s) < REB_MAX + 1)
#define KIND_FROM_SYM(s)    cast(enum Reb_Kind, (s) - 1)
#define SYM_FROM_KIND(k)    cast(REBCNT, (k) + 1)
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
#define VAL_LEN_HEAD(v)     SERIES_LEN(VAL_SERIES(v))
#define VAL_LEN_AT(v)       (Val_Series_Len_At(v))

#define IS_EMPTY(v)         (VAL_INDEX(v) >= VAL_LEN_HEAD(v))

#define VAL_DATA_AT(p) \
    (VAL_BIN_HEAD(p) + (VAL_INDEX(p) * VAL_SERIES_WIDTH(p)))

#define VAL_SERIES_WIDTH(v) \
    SERIES_WIDE(VAL_SERIES(v))


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
#define VAL_ARRAY(v)            AS_ARRAY(VAL_SERIES(v))
#define VAL_ARRAY_HEAD(v)       ARRAY_HEAD(VAL_ARRAY(v))
#define VAL_ARRAY_TAIL(v)       ARRAY_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v))

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v)         ARRAY_AT(VAL_ARRAY(v), VAL_INDEX(v))
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
    ARRAY_AT(VAL_ARRAY(v), (n))

#define VAL_TERM_ARRAY(v)       TERM_ARRAY(VAL_ARRAY(v))

#define Val_Init_Array_Index(v,t,a,i) \
    Val_Init_Series_Index((v), (t), ARRAY_SERIES(a), (i))

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

#define QUAD_LEN(s)         SERIES_LEN(s)

#define QUAD_HEAD(s)        cast(REBYTE*, SERIES_DATA(s))
#define QUAD_SKIP(s,n)      (QUAD_HEAD(s) + ((n) * 4))
#define QUAD_TAIL(s)        (QUAD_HEAD(s) + (QUAD_LEN(s) * 4))

#define IMG_SIZE(s)         ((s)->misc.size)
#define IMG_WIDE(s)         ((s)->misc.area.wide)
#define IMG_HIGH(s)         ((s)->misc.area.high)
#define IMG_DATA(s)         cast(REBYTE*, SERIES_DATA(s))

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
#define IS_BIT(d,n)     ((d)[(n) >> 3] & (1 << ((n) & 7)))

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
    VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, sym))

// Return the CANON value for a word value:
#define WORD_TO_CANON(w) \
    VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, VAL_WORD_SYM(w)))


/***********************************************************************
**
**  WORDS -- All word related types
**
***********************************************************************/

struct Reb_Any_Word {
    //
    // The "target" of a word is a specification of where to look for its
    // value.  If this is a FRAME then it will be the VAL_FRAME_VARLIST
    // series of that frame.  If the word targets a stack-relative lookup,
    // such as with FUNCTION!, then the word must bind to something more
    // persistent than the stack.  Hence it indicates the VAL_FUNC_PARAMLIST
    // and must pay to walk the stack looking to see if that function is
    // currently being called to find the stack "var" for that param "key"
    //
    REBARR *target;

    // Index of word in frame (if it's not NULL)
    //
    REBINT index;

    // Index of the word's symbol
    //
    REBCNT sym;
};

#define IS_SAME_WORD(v, n)      (IS_WORD(v) && VAL_WORD_CANON(v) == n)

#ifdef NDEBUG
    #define VAL_WORD_SYM(v) ((v)->payload.any_word.sym)
#else
    // !!! Due to large reorganizations, it may be that VAL_WORD_SYM and
    // VAL_TYPESET_SYM calls were swapped.  In the aftermath of reorganization
    // this check is prudent (until further notice...)
    //
    #define VAL_WORD_SYM(v) \
        (*VAL_WORD_SYM_Ptr_Debug(v))
#endif

#define VAL_WORD_INDEX(v)       ((v)->payload.any_word.index)
#define VAL_WORD_TARGET(v)      ((v)->payload.any_word.target)
#define HAS_TARGET(v)            (VAL_WORD_TARGET(v) != NULL)

#ifdef NDEBUG
    #define UNBIND_WORD(v) \
        (VAL_WORD_TARGET(v)=NULL)
#else
    #define WORD_INDEX_UNBOUND MIN_I32
    #define UNBIND_WORD(v) \
        (VAL_WORD_TARGET(v)=NULL, VAL_WORD_INDEX(v)=WORD_INDEX_UNBOUND)
#endif

#define VAL_WORD_CANON(v) \
    VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, VAL_WORD_SYM(v)))

#define VAL_WORD_NAME(v) \
    VAL_SYM_NAME(ARRAY_AT(PG_Word_Table.array, VAL_WORD_SYM(v)))

#define VAL_WORD_NAME_STR(v)    BIN_HEAD(VAL_WORD_NAME(v))

#define VAL_WORD_TARGET_WORDS(v) VAL_WORD_TARGET(v)->words
#define VAL_WORD_TARGET_VALUES(v) VAL_WORD_TARGET(v)->values

// Is it the same symbol? Quick check, then canon check:
#define SAME_SYM(s1,s2) \
    ((s1) == (s2) \
    || ( \
        VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, (s1))) \
        == VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, (s2))) \
    ))


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

// Option flags used with VAL_GET_EXT().  These describe properties of
// a value slot when it's constrained to the types in the typeset
//
enum {
    EXT_TYPESET_QUOTE = 0,  // Quoted (REDUCE paren/get-word|path if EVALUATE)
    EXT_TYPESET_EVALUATE,   // DO/NEXT performed at callsite when setting
    EXT_TYPESET_REFINEMENT, // Value indicates an optional switch
    EXT_WORD_LOCK,  // Can't be changed (set with PROTECT)
    EXT_WORD_HIDE,      // Can't be reflected (set with PROTECT/HIDE)
    EXT_TYPESET_MAX
};

struct Reb_Typeset {
    REBCNT sym;         // Symbol (if a key of object or function param)

    // Note: `sym` is first so that the value's 32-bit Reb_Flags header plus
    // the 32-bit REBCNT will pad `bits` to a REBU64 alignment boundary

    REBU64 bits;        // One bit for each DATATYPE! (use with FLAGIT_64)
};

// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->payload.typeset.bits)

#define TYPE_CHECK(v,n) \
    ((VAL_TYPESET_BITS(v) & FLAGIT_64(n)) != 0)

#define TYPE_SET(v,n) \
    ((VAL_TYPESET_BITS(v) |= FLAGIT_64(n)), NOOP)

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
    VAL_SYM_CANON(ARRAY_AT(PG_Word_Table.array, VAL_TYPESET_SYM(v)))

// Word number array (used by Bind_Table):
#define WORDS_HEAD(w) \
    cast(REBINT *, SERIES_DATA(w))

#define WORDS_LAST(w) \
    (WORDS_HEAD(w) + SERIES_LEN(w) - 1) // (tail never zero)


//=////////////////////////////////////////////////////////////////////////=//
//
// ANY-CONTEXT! (`struct Reb_Any_Context`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The Reb_Any_Context is the basic struct used currently for OBJECT!,
// MODULE!, ERROR!, and PORT!.  It builds upon the "frame" datatype REBFRM,
// which permits the storage of associated KEYS and VARS.  (See the comments
// on `struct Reb_Frame` that are in %sys-series.h).
//
// Contexts coordinate with words, which can have their VAL_WORD_TARGET()
// set to a context's frame pointer.  Then they cache the index of that
// word's symbol in the frame's keylist, for a fast lookup to get to the
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
    REBFRM *frame;
    REBFRM *spec; // optional (currently only used by modules)
    REBARR *body; // optional (currently not used at all)
};

#ifdef NDEBUG
    #define VAL_FRAME(v)            ((v)->payload.any_context.frame)
#else
    #define VAL_FRAME(v)            (*VAL_FRAME_Ptr_Debug(v))
#endif

#define VAL_CONTEXT_SPEC(v)         ((v)->payload.any_context.spec)
#define VAL_CONTEXT_BODY(v)         ((v)->payload.any_context.body)

// A fully constructed frame can reconstitute the context REBVAL that it is
// a frame for from a single pointer...the REBVAL sitting in the 0 slot
// of the frame's varlist.  In a debug build we check to make sure the
// type of the embedded value matches the type of what is intended (so
// someone who thinks they are initializing a REB_OBJECT from a FRAME does
// not accidentally get a REB_ERROR, for instance.)
//
#if FALSE && defined(NDEBUG)
    //
    // !!! Currently Val_Init_Context_Core does not require the passed in
    // frame to already be managed.  If it did, then it could be this
    // simple and not be a "bad macro".  Review if it's worthwhile to change
    // the prerequisite that this is only called on managed frames.
    //
    #define Val_Init_Context(o,t,f,s,b) \
        (*(o) = *FRAME_CONTEXT(f))
#else
    #define Val_Init_Context(o,t,f,s,b) \
        Val_Init_Context_Core((o), (t), (f), (s), (b))
#endif

// Convenience macros to speak in terms of object values instead of the frame
//
#define VAL_CONTEXT_VAR(v,n)        FRAME_VAR(VAL_FRAME(v), (n))
#define VAL_CONTEXT_KEY(v,n)        FRAME_KEY(VAL_FRAME(v), (n))
#define VAL_CONTEXT_KEY_SYM(v,n)    FRAME_KEY_SYM(VAL_FRAME(v), (n))

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

#define Val_Init_Object(v,f) \
    Val_Init_Context((v), REB_OBJECT, (f), NULL, NULL)


/***********************************************************************
**
**  MODULES - Code isolation units
**
**  http://www.rebol.com/r3/docs/concepts/modules-defining.html
**
***********************************************************************/

#define VAL_MOD_SPEC(v)     VAL_CONTEXT_SPEC(v)
#define VAL_MOD_BODY(v)     VAL_CONTEXT_BODY(v)

#define Val_Init_Module(v,f,s,b) \
    Val_Init_Context((v), REB_MODULE, (f), (s), (b))


/***********************************************************************
**
**  PORTS - External series interface
**
***********************************************************************/

#define Val_Init_Port(v,f) \
    Val_Init_Context((v), REB_PORT, (f), NULL, NULL)


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

#define ERR_VALUES(frame)   cast(ERROR_OBJ*, ARRAY_HEAD(FRAME_VARLIST(frame)))
#define ERR_NUM(frame)      cast(REBCNT, VAL_INT32(&ERR_VALUES(frame)->code))

#define VAL_ERR_VALUES(v)   ERR_VALUES(VAL_FRAME(v))
#define VAL_ERR_NUM(v)      ERR_NUM(VAL_FRAME(v))

#define Val_Init_Error(o,f) \
    Val_Init_Context((o), REB_ERROR, (f), NULL, NULL)


/***********************************************************************
**
**  FUNCTIONS - Natives, actions, operators, and user functions
**
**  NOTE: make-headers.r will skip specs with the "REBNATIVE(" in them
**  REBTYPE macros are used and expanded in tmp-funcs.h
**
***********************************************************************/

enum {
    EXT_FUNC_INFIX = 0,     // called with "infix" protocol
    EXT_FUNC_HAS_RETURN,    // function "fakes" a definitionally scoped return
    EXT_FUNC_FRAMELESS,     // native hooks into DO state and does own arg eval
    EXT_FUNC_MAX
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
    R_FALSE, // => SET_FALSE(D_OUT); return R_OUT;
    R_ARG1, // => *D_OUT = *D_ARG(1); return R_OUT;
    R_ARG2, // => *D_OUT = *D_ARG(2); return R_OUT;
    R_ARG3 // => *D_OUT = *D_ARG(3); return R_OUT;
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
typedef REB_R (*REBPAF)(struct Reb_Call *call_, REBFRM *p, REBCNT a);

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

// EXT_FUNC_HAS_RETURN functions use the RETURN native's function value to give
// the definitional return its prototype, but overwrite its code pointer to
// hold the paramlist of the target.
//
// Do_Native_Throws() sees when someone tries to execute one of these "native
// returns"...and instead interprets it as a THROW whose /NAME is the function
// value.  The paramlist has that value (it's the REBVAL in slot #0)
//
// This is a special case: the body value of the hacked REBVAL of the return
// is allowed to be inconsistent with the content of the ROOT_RETURN_NATIVE's
// actual FUNC.  (In the general case, the [0] element of the FUNC must be
// consistent with the fields of the value holding it.)
//
#define VAL_FUNC_RETURN_TO(v) VAL_FUNC_BODY(v)


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
    REBFLG flags;
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
    REBINT  abi;
    REBFLG  flags;
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
#define ROUTINE_RVALUE(v)           VAL_STRUCT(ARRAY_HEAD(ROUTINE_FFI_ARG_STRUCTS(v)))
#define ROUTINE_CLOSURE(v)          (ROUTINE_INFO(v)->info.cb.closure)
#define ROUTINE_DISPATCHER(v)       (ROUTINE_INFO(v)->info.cb.dispatcher)
#define CALLBACK_FUNC(v)            (ROUTINE_INFO(v)->info.cb.func)

/* argument is REBRIN */

#define RIN_FUNCPTR(v)              ((v)->info.rot.funcptr)
#define RIN_LIB(v)                  ((v)->info.rot.lib)
#define RIN_CLOSURE(v)              ((v)->info.cb.closure)
#define RIN_FUNC(v)                 ((v)->info.cb.func)
#define RIN_ARGS_STRUCTS(v)         ((v)->arg_structs)
#define RIN_RVALUE(v)               VAL_STRUCT(ARRAY_HEAD(RIN_ARGS_STRUCTS(v)))

#define ROUTINE_FLAGS(s)       ((s)->flags)
#define ROUTINE_SET_FLAG(s, f) (ROUTINE_FLAGS(s) |= (f))
#define ROUTINE_CLR_FLAG(s, f) (ROUTINE_FLAGS(s) &= ~(f))
#define ROUTINE_GET_FLAG(s, f) (ROUTINE_FLAGS(s) &  (f))

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
    VAL_STRUCT(ARRAY_HEAD(VAL_ROUTINE_INFO(v)->arg_structs))

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

#ifndef NDEBUG
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
