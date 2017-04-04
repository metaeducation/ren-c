//
//  File: %sys-value.h
//  Summary: {Accessor Functions for properties of a Rebol Value}
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
// This file provides basic accessors for value types.  Because these
// accessors operate on REBVAL (or RELVAL) pointers, the inline functions need
// the complete struct definition available from all the payload types.
//
// See notes in %sys-rebval.h for the definition of the REBVAL structure.
//
// An attempt is made to group the accessors in sections.  Some functions are
// defined in %c-value.c for the sake of the grouping.
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
// stack variable's payload, and then DROP_GUARD_VALUE() when the protection
// is not needed.  (You must always drop the last guard pushed.)
//
// For a means of creating a temporary array of GC-protected REBVALs, see
// the "chunk stack" in %sys-stack.h.  This is used when building function
// argument frames, which means that the REBVAL* arguments to a function
// accessed via ARG() will be stable as long as the function is running.
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DEBUG PROBE
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The PROBE macro can be used in debug builds to mold a REBVAL much like the
// Rebol `probe` operation.  It's actually polymorphic, and if you have
// a REBSER*, REBCTX*, or REBARR* it can be used with those as well.
//
// In order to make it easier to find out where a piece of debug spew is
// coming from, the file and line number will be output as well.
//
// Note: As a convenience, PROBE also flushes the `stdout` and `stderr` in
// case the debug build was using printf() to output contextual information.
//

#if !defined(NDEBUG)
    #define PROBE(v) \
        Probe_Core_Debug((v), __FILE__, __LINE__)
#endif


#define VAL_ALL_BITS(v) ((v)->payload.all.bits)


//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE "KIND" (1 out of 64 different foundational types)
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


#define FLAGIT_KIND(t) \
    (cast(REBU64, 1) << (t)) // makes a 64-bit bitflag

// While inline vs. macro doesn't usually matter much, debug builds won't
// inline this, and it's called *ALL* the time.  Since it doesn't repeat its
// argument, it's not worth it to make it a function for slowdown caused.
// Also, don't bother checking using the `cast()` template in C++.
//
// !!! Technically this is wasting two bits in the header, because there are
// only 64 types that fit in a type bitset.  Yet the sheer commonness of
// this operation makes bit masking expensive...and choosing the number of
// types based on what fits in a 64-bit mask is not necessarily the most
// future-proof concept in the first place.  Use a full byte for speed.
//
#define VAL_TYPE_RAW(v) \
    ((enum Reb_Kind)(RIGHT_8_BITS((v)->header.bits)))

#ifdef NDEBUG
    #define VAL_TYPE(v) \
        VAL_TYPE_RAW(v)
#else
    #define BLANK_FLAG_UNREADABLE_DEBUG \
        FLAGIT_LEFT(TYPE_SPECIFIC_BIT + 0)

    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        if (IS_END_MACRO(v)) {
            printf("VAL_TYPE() called on END marker\n");
            panic_at (v, file, line);
        }

        if (NOT(v->header.bits & NODE_FLAG_CELL)) {
            printf("VAL_TYPE() called on non-cell\n");
            panic_at (v, file, line);
        }

        if (NOT(v->header.bits & NODE_FLAG_VALID)) {
            printf("VAL_TYPE() called on trash cell\n");
            panic_at (v, file, line);
        }

        enum Reb_Kind kind = VAL_TYPE_RAW(v);

        if (
            kind == REB_BLANK
            && (v->header.bits & BLANK_FLAG_UNREADABLE_DEBUG)
        ) {
            printf("VAL_TYPE() called on unreadable BLANK!\n");
            panic_at (v, file, line);
        }

        return kind;
    }

    #define VAL_TYPE(v) \
        VAL_TYPE_Debug((v), __FILE__, __LINE__)
#endif

inline static void Reset_Val_Kind(RELVAL *v, enum Reb_Kind kind) {
    //
    // Note: Only use if you are sure the new type payload is in sync with
    // the type and bits (e.g. changing ANY-WORD! to another ANY-WORD!).
    // Otherwise the value-specific flags might be misinterpreted.
    //
    // Use Reset_Val_Header() to set the type AND initialize the flags to 0.
    //
    assert(
        (v->header.bits & NODE_FLAG_CELL)
        && (v->header.bits & NODE_FLAG_VALID)
    );
    CLEAR_8_RIGHT_BITS((v)->header.bits);
    (v)->header.bits |= HEADERIZE_KIND(kind);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// VALUE_FLAG_XXX flags are applicable to all types.  Type-specific flags are
// named things like TYPESET_FLAG_XXX or WORD_FLAG_XXX and only apply to the
// type that they reference.  Both use these XXX_VAL_FLAG accessors.
//

#ifdef NDEBUG
    inline static void Set_Val_Flags(RELVAL *v, REBUPT f) {
        v->header.bits |= f;
    }

    #define Set_Val_Flag(v,f) \
        Set_Val_Flags((v), (f))

    inline static REBOOL Get_Val_Flag(const RELVAL *v, REBUPT f) {
        return LOGICAL(v->header.bits & f);
    }

    inline static REBOOL Any_Val_Flags(const RELVAL *v, REBUPT f) {
        return LOGICAL((v->header.bits & f) != 0);
    }

    inline static REBOOL All_Val_Flags(const RELVAL *v, REBUPT f) {
        return LOGICAL((v->header.bits & f) == f);
    }

    inline static void Clear_Val_Flags(RELVAL *v, REBUPT f) {
        v->header.bits &= ~f;
    }

    #define Clear_Val_Flag(v,f) \
        Clear_Val_Flags((v), (f))
#else
    // For safety in the debug build, all the type-specific flags include a
    // type (or type representing a category) as part of the flag.  This type
    // is checked first, and then masked out to use the single-bit-flag value
    // which is intended.
    //
    // But flag testing routines are called *a lot*, and debug builds do not
    // inline functions.  So it's worth doing a sketchy macro so this somewhat
    // borderline assert doesn't wind up taking up 20% of the debug's runtime.
    //
    #define CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(flags) \
        REBUPT category = RIGHT_8_BITS(flags); \
        if (category != REB_0) { \
            if (kind != category) { \
                if (category == REB_WORD) \
                    assert(ANY_WORD_KIND(kind)); \
                else if (category == REB_OBJECT) \
                    assert(ANY_CONTEXT_KIND(kind)); \
                else \
                    assert(FALSE); \
            } \
            CLEAR_8_RIGHT_BITS(flags); \
        } \

    inline static void Set_Val_Flags(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        v->header.bits |= f;
    }

    inline static void Set_Val_Flag(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        v->header.bits |= f;
    }

    inline static REBOOL Get_Val_Flag(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        return LOGICAL(v->header.bits & f);
    }

    inline static REBOOL Any_Val_Flags(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        return LOGICAL((v->header.bits & f) != 0);
    }

    inline static REBOOL All_Val_Flags(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        return LOGICAL((v->header.bits & f) == f);
    }

    inline static void Clear_Val_Flags(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        v->header.bits &= ~f;
    }

    inline static void Clear_Val_Flag(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        v->header.bits &= ~f;
    }
#endif

#define Not_Val_Flag(v,f) \
    NOT(Get_Val_Flag((v), (f)))


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACKING PAYLOAD (for types that don't use their payloads)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some datatypes like BLANK! or LOGIC!, or a void cell, can be communicated
// entirely by their header bits.  Though this "wastes" 3/4 of the space of
// the value cell, it also means the cell can be written and tested quickly.
//
// The release build does not canonize the remaining bits of the payload so
// they are left as random data.  But the debug build can take advantage of
// it to store some tracking information about the point and moment of
// initialization.  This data can be viewed in the debugging watchlist
// under the `track` component of payload, and is also used by panic().
//

#if !defined NDEBUG
    inline static void Set_Track_Payload_Debug(
        RELVAL *v, const char *file, int line
    ){
        v->payload.track.filename = file;
        v->payload.track.line = line;
        v->extra.do_count = TG_Do_Count;
    }

    inline static const char* VAL_TRACK_FILE(const RELVAL *v)
        { return v->payload.track.filename; }

    inline static int VAL_TRACK_LINE(const REBVAL *v)
        { return v->payload.track.line; }

    inline static REBUPT VAL_TRACK_COUNT(const REBVAL *v)
        { return v->extra.do_count; }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL WRITABILITY AND SETUP
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Reset_Val_Header clears out the header of *most* bits, setting it to a
// new type.
//
// The value is expected to already be "pre-formatted" with the NODE_FLAG_CELL
// bit, so that is left as-is.  It is also expected that VALUE_FLAG_STACK has
// been set if the value is stack-based (e.g. on the C stack or in a frame),
// so that is left as-is also.
//

inline static void Reset_Val_Header_common( // don't call directly
    RELVAL *v,
    enum Reb_Kind kind,
    REBUPT extra_flags
) {
    v->header.bits &= CELL_MASK_RESET;

    // !!! Should NODE_FLAG_CELL be forced on the OR='ing side?  May cover up
    // bugs somehow, which may be arguably good in a release build, but do it
    // without for now and assume it was set and the AND= above kept it.
    //
    v->header.bits |= NODE_FLAG_VALID | HEADERIZE_KIND(kind) | extra_flags;
}

#ifdef NDEBUG
    #define Reset_Val_Header_Core(v,kind,extra) \
        Reset_Val_Header_common((v), (kind), (extra))

    #define ASSERT_CELL_WRITABLE(v,file,line) \
        NOOP

    #define INIT_CELL(v) \
        (v)->header.bits = NODE_FLAG_CELL; // no VALUE_FLAG_STACK
#else
    #define ASSERT_CELL_WRITABLE(v,file,line) \
        Assert_Cell_Writable((v), (file), (line))

    inline static void Reset_Val_Header_Core_Debug(
        RELVAL *v,
        enum Reb_Kind kind,
        REBUPT extra,
        const char *file,
        int line
    ){
        ASSERT_CELL_WRITABLE(v, file, line);

        // The debug build puts some extra type information onto flags
        // which needs to be cleared out.  (e.g. WORD_FLAG_BOUND has the bit
        // pattern for REB_WORD inside of it, to help make sure that flag
        // doesn't get used with things that aren't words).
        //
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(extra);
        
        Reset_Val_Header_common(v, kind, extra);
    }

    #define Reset_Val_Header_Core(v,kind,extra) \
        Reset_Val_Header_Core_Debug((v), (kind), (extra), __FILE__, __LINE__)

    inline static void INIT_CELL_Debug(
        RELVAL *v, const char *file, int line
    ){
        v->header.bits = NODE_FLAG_CELL; // no VALUE_FLAG_STACK
        Set_Track_Payload_Debug(v, file, line);
    }

    #define INIT_CELL(v) \
        INIT_CELL_Debug((v), __FILE__, __LINE__)
#endif

#define Reset_Val_Header(v,t) \
    Reset_Val_Header_Core((v), (t), 0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRASH CELLS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Trash is a cell (marked by NODE_FLAG_CELL) without NODE_FLAG_VALID set.
// To prevent it from being inspected while it's in an invalid state, VAL_TYPE
// used on a trash cell will assert in the debug build.
//
// The garbage collector is not tolerant of trash.
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by panic())
//

#ifdef NDEBUG
    #define SET_TRASH_IF_DEBUG(v) \
        NOOP
#else
    inline static void Set_Trash_Debug(
        RELVAL *v,
        const char *file,
        int line
    ) {
        ASSERT_CELL_WRITABLE(v, file, line);

        v->header.bits &= CELL_MASK_RESET;

        Set_Track_Payload_Debug(v, file, line);
    }

    #define SET_TRASH_IF_DEBUG(v) \
        Set_Trash_Debug((v), __FILE__, __LINE__)

    inline static REBOOL IS_TRASH_DEBUG(const RELVAL *v) {
        assert(v->header.bits & NODE_FLAG_CELL);
        if (v->header.bits & NODE_FLAG_VALID)
            return FALSE;
        assert(VAL_TYPE_RAW(v) == REB_0);
        return TRUE;
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  VOID CELLS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Voids are a transient product of evaluation (e.g. the result of `do []`).
// They cannot be stored in BLOCK!s that are seen by the user, and if a
// variable is assigned a void cell then that variable is "unset".
//
// Void is thus not considered to be a "value type", but a bit pattern used to
// mark cells as not containing any value at all.  It uses REB_MAX, because
// that is one past the range of valid REB_XXX values in the enumeration
// created for the actual types.
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by panic())
//

#define VOID_CELL \
    c_cast(const REBVAL*, &PG_Void_Cell[0])

#define IS_VOID(v) \
    LOGICAL(VAL_TYPE(v) == REB_MAX_VOID)

#define Init_Void(v) \
    Reset_Val_Header(v, REB_MAX_VOID)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BAR! and LIT-BAR!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The "expression barrier" is denoted by a lone vertical bar `|`.  It
// has the special property that literals used directly will be rejected
// as a source for argument fulfillment.  BAR! that comes from evaluations
// can be passed as a parameter, however:
//
//     append [a b c] | [d e f] print "Hello"   ;-- will cause an error
//     append [a b c] [d e f] | print "Hello"   ;-- is legal
//     append [a b c] first [|]                 ;-- is legal
//     append [a b c] '|                        ;-- is legal
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by panic())
//

#define BAR_VALUE \
    c_cast(const REBVAL*, &PG_Bar_Value[0])

#define SET_BAR(v) \
    Reset_Val_Header((v), REB_BAR)

#define SET_LIT_BAR(v) \
    Reset_Val_Header((v), REB_LIT_BAR)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BLANK! (unit type - fits in header bits, may use `struct Reb_Track`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Unlike a void cell, blank values are inactive.  They do not cause errors
// when they are used in situations like the condition of an IF statement.
// Instead they are considered to be false--like the LOGIC! #[false] value.
// So blank is considered to be the other "conditionally false" value.
//
// Only those two values are conditionally false in Rebol, and testing for
// conditional truth and falsehood is frequent.  Hence in addition to its
// type, BLANK! also carries a header bit that can be checked for conditional
// falsehood, to save on needing to separately test the type.
//
// In the debug build, it is possible to make an "unreadable" blank.  This
// will behave neutrally as far as the garbage collector is concerned, so
// it can be used as a placeholder for a value that will be filled in at
// some later time--spanning an evaluation.  But if the special IS_UNREADABLE
// checks are not used, it will not respond to IS_BLANK() and will also
// refuse VAL_TYPE() checks.  This is useful anytime a placeholder is needed
// in a slot temporarily where the code knows it's supposed to come back and
// fill in the correct thing later...where the asserts serve as a reminder
// if that fill in never happens.
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by panic()).
// This is particularly useful when getting an assertion on UNREADABLE blanks.
//

#define BLANK_VALUE \
    c_cast(const REBVAL*, &PG_Blank_Value[0])

#define Init_Blank(v) \
    Reset_Val_Header_Core((v), REB_BLANK, VALUE_FLAG_CONDITIONAL_FALSE)

#ifdef NDEBUG
    #define Init_Unreadable_Blank(v) \
        Init_Blank(v)

    #define IS_BLANK_RAW(v) \
        IS_BLANK(v)

     #define IS_UNREADABLE_IF_DEBUG(v) \
        FALSE

    #define SINK(v) \
        cast(REBVAL*, (v))
#else
    #define Init_Unreadable_Blank(v) \
        Reset_Val_Header_Core((v), REB_BLANK, \
            VALUE_FLAG_CONDITIONAL_FALSE | BLANK_FLAG_UNREADABLE_DEBUG)

    inline static REBOOL IS_BLANK_RAW(const RELVAL *v) {
        return LOGICAL(VAL_TYPE_RAW(v) == REB_BLANK);
    }

    inline static REBOOL IS_UNREADABLE_IF_DEBUG(const RELVAL *v) {
        if (NOT(VAL_TYPE_RAW(v) == REB_BLANK))
            return FALSE;
        return LOGICAL(v->header.bits & BLANK_FLAG_UNREADABLE_DEBUG);
    }

    // "Sinking" a value is like trashing it in the debug build at the moment
    // of knowing that it will ultimately be overwritten.  This avoids
    // any accidental usage of the target cell's contents before the overwrite
    // winds up happening.
    //
    // It's slightly different than "trashing", because if the node was valid
    // before, then it would have been safe for the GC to visit.  So this
    // doesn't break that invariant...if the node was invalid it stays
    // invalid, but if it was valid it is turned into an unreadable blank,
    // which overwrites all the cell fields (with tracking info) and will
    // trigger errors through VAL_TYPE() if it's used.
    //
    inline static REBVAL *Sink_Debug(
        RELVAL *v,
        const char *file,
        int line
    ) {
        ASSERT_CELL_WRITABLE(v, file, line);

        if (v->header.bits & NODE_FLAG_VALID) {
            Reset_Val_Header_Core_Debug(
                v,
                REB_BLANK,
                VALUE_FLAG_CONDITIONAL_FALSE | BLANK_FLAG_UNREADABLE_DEBUG,
                file,
                line
            );
        }
        else {
            // already trash, don't need to mess with the header
        }

        Set_Track_Payload_Debug(v, file, line);

        return cast(REBVAL*, v); // used by SINK, but not SET_TRASH_IF_DEBUG
    }

    #define SINK(v) \
        Sink_Debug((v), __FILE__, __LINE__)

#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOGIC! AND "CONDITIONAL TRUTH"
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A logic can be either true or false.  For purposes of optimization, logical
// falsehood is indicated by one of the value option bits in the header--as
// opposed to in the value payload.  This means it can be tested quickly and
// that a single check can test for both BLANK! and logic false.
//
// Conditional truth and falsehood allows an interpretation where a BLANK!
// is a "falsey" value as well as logic false.  Voids are neither
// conditionally true nor conditionally false, and so debug builds will
// complain if you try to determine which it is.  (It likely means a mistake
// was made in skipping a formal decision-point regarding whether an unset
// should represent an "opt out" or an error.)
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by panic())
//

#define FALSE_VALUE \
    c_cast(const REBVAL*, &PG_False_Value[0])

#define TRUE_VALUE \
    c_cast(const REBVAL*, &PG_True_Value[0])

#define Init_True(v) \
    Reset_Val_Header_Core((v), REB_LOGIC, 0)

#define Init_False(v) \
    Reset_Val_Header_Core((v), REB_LOGIC, VALUE_FLAG_CONDITIONAL_FALSE)

#define Init_Logic(v,b) \
    Reset_Val_Header_Core((v), REB_LOGIC, \
        (b) ? 0 : VALUE_FLAG_CONDITIONAL_FALSE)

#ifdef NDEBUG
    #define IS_CONDITIONAL_FALSE(v) \
        Get_Val_Flag((v), VALUE_FLAG_CONDITIONAL_FALSE)
#else
    inline static REBOOL IS_CONDITIONAL_FALSE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        if (IS_VOID(v)) {
            printf("Conditional true/false test on void\n");
            panic_at (v, file, line);
        }
        return Get_Val_Flag(v, VALUE_FLAG_CONDITIONAL_FALSE);
    }

    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_Debug((v), __FILE__, __LINE__)
#endif

#define IS_CONDITIONAL_TRUE(v) \
    NOT(IS_CONDITIONAL_FALSE(v)) // macro gets file + line # in debug build

// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`.
//
inline static REBOOL IS_CONDITIONAL_TRUE_SAFE(const REBVAL *v) {
    if (IS_BLOCK(v)) {
        if (Get_Val_Flag(v, VALUE_FLAG_UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));
            
        return TRUE;
    }
    return IS_CONDITIONAL_TRUE(v);
}

inline static REBOOL VAL_LOGIC(const RELVAL *v) {
    assert(IS_LOGIC(v));
    return Not_Val_Flag((v), VALUE_FLAG_CONDITIONAL_FALSE);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATATYPE!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: R3-Alpha's notion of a datatype has not been revisited very much in
// Ren-C.  The unimplemented UTYPE! user-defined type concept was removed
// for simplification, pending a broader review of what was needed.
//
// %words.r is arranged so that symbols for types are at the start
// Although REB_0 is 0 and the 0 REBCNT used for symbol IDs is reserved
// for "no symbol"...this is okay, because void is not a value type and
// should not have a symbol.
//
// !!! Consider the naming once all legacy TYPE? calls have been converted
// to TYPE-OF.  TYPE! may be a better name, though possibly KIND! would be
// better if user types suggest that TYPE-OF can potentially return some
// kind of context (might TYPE! be an ANY-CONTEXT!, with properties like
// MIN-VALUE and MAX-VALUE, for instance).
//

#define VAL_TYPE_KIND(v) \
    ((v)->payload.datatype.kind)

#define VAL_TYPE_SPEC(v) \
    ((v)->payload.datatype.spec)

#define IS_KIND_SYM(s) \
    ((s) < cast(REBSYM, REB_MAX))

inline static enum Reb_Kind KIND_FROM_SYM(REBSYM s) {
    assert(IS_KIND_SYM(s));
    return cast(enum Reb_Kind, cast(int, (s)));
}

#define SYM_FROM_KIND(k) \
    cast(REBSYM, cast(enum Reb_Kind, (k)))

#define VAL_TYPE_SYM(v) \
    SYM_FROM_KIND((v)->payload.datatype.kind)


//=////////////////////////////////////////////////////////////////////////=//
//
//  CHAR!
//
//=////////////////////////////////////////////////////////////////////////=//

#define MAX_CHAR 0xffff

#define VAL_CHAR(v) \
    ((v)->payload.character)

inline static void Init_Char(RELVAL *v, REBUNI uni) {
    Reset_Val_Header(v, REB_CHAR);
    VAL_CHAR(v) = uni;
}

#define SPACE_VALUE \
    (ROOT_SPACE_CHAR)


//=////////////////////////////////////////////////////////////////////////=//
//
//  INTEGER!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Integers in Rebol were standardized to use a compiler-provided 64-bit
// value.  This was formally added to the spec in C99, but many compilers
// supported it before that.
//
// !!! 64-bit extensions were added by the "rebolsource" fork, with much of
// the code still written to operate on 32-bit values.  Since the standard
// unit of indexing and block length counts remains 32-bit in that 64-bit
// build at the moment, many lingering references were left that operated
// on 32-bit values.  To make this clearer, the macros have been renamed
// to indicate which kind of integer they retrieve.  However, there should
// be a general review for reasoning, and error handling + overflow logic
// for these cases.
//

#if defined(NDEBUG) || !defined(__cplusplus) 
    #define VAL_INT64(v) \
        ((v)->payload.integer)
#else
    // allows an assert, but also lvalue: `VAL_INT64(v) = xxx`
    //
    inline static REBI64 & VAL_INT64(RELVAL *v) { // C++ reference type
        assert(IS_INTEGER(v));
        return v->payload.integer;
    }
    inline static REBI64 VAL_INT64(const RELVAL *v) {
        assert(IS_INTEGER(v));
        return v->payload.integer;
    }
#endif

inline static void Init_Integer(RELVAL *v, REBI64 i64) {
    Reset_Val_Header(v, REB_INTEGER);
    v->payload.integer = i64;
}

#define VAL_INT32(v) \
    cast(REBINT, VAL_INT64(v))

#define VAL_UNT32(v) \
    cast(REBCNT, VAL_INT64(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  DECIMAL! and PERCENT!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Implementation-wise, the decimal type is a `double`-precision floating
// point number in C (typically 64-bit).  The percent type uses the same
// payload, and is currently extracted with VAL_DECIMAL() as well.
//
// !!! Calling a floating point type "decimal" appears based on Rebol's
// original desire to use familiar words and avoid jargon.  It has however
// drawn criticism from those who don't think it correctly conveys floating
// point behavior, expecting something else.  Red has renamed the type
// FLOAT! which may be a good idea.
//

#if defined(NDEBUG) || !defined(__cplusplus)
    #define VAL_DECIMAL(v) \
        ((v)->payload.decimal)
#else
    // allows an assert, but also lvalue: `VAL_DECIMAL(v) = xxx`
    //
    inline static REBDEC & VAL_DECIMAL(RELVAL *v) { // C++ reference type
        assert(IS_DECIMAL(v) || IS_PERCENT(v));
        return v->payload.decimal;
    }
    inline static REBDEC VAL_DECIMAL(const RELVAL *v) {
        assert(IS_DECIMAL(v) || IS_PERCENT(v));
        return v->payload.decimal;
    }
#endif

inline static void Init_Decimal(RELVAL *v, REBDEC d) {
    Reset_Val_Header(v, REB_DECIMAL);
    v->payload.decimal = d;
}

inline static void SET_PERCENT(RELVAL *v, REBDEC d) {
    Reset_Val_Header(v, REB_PERCENT);
    v->payload.decimal = d;
}


// !!! There was an IS_NUMBER() macro defined in R3-Alpha which only covered
// REB_INTEGER and REB_DECIMAL.  But ANY-NUMBER! the typeset included PERCENT!
// so this adds that and gets rid of IS_NUMBER()
//
inline static REBOOL ANY_NUMBER(const RELVAL *v) {
    return LOGICAL(
        VAL_TYPE(v) == REB_INTEGER
        || VAL_TYPE(v) == REB_DECIMAL
        || VAL_TYPE(v) == REB_PERCENT
    );
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  MONEY!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha's MONEY! type is "unitless" currency, such that $10/$10 = $1
// (and not 1).  This is because the feature in Rebol2 of being able to
// store the ISO 4217 code (~15 bits) was not included:
//
// https://en.wikipedia.org/wiki/ISO_4217
//
// According to @Ladislav:
//
// "The money datatype is neither a bignum, nor a fixpoint arithmetic.
//  It actually is unnormalized decimal floating point."
//
// !!! The naming of "deci" used by MONEY! as "decimal" is a confusing overlap
// with DECIMAL!, although that name may be changing also.
//

inline static deci VAL_MONEY_AMOUNT(const RELVAL *v) {
    deci amount;
    amount.m0 = v->extra.m0;
    amount.m1 = v->payload.money.m1;
    amount.m2 = v->payload.money.m2;
    amount.s = v->payload.money.s;
    amount.e = v->payload.money.e;
    return amount;
}

inline static void Init_Money(RELVAL *v, deci amount) {
    Reset_Val_Header(v, REB_MONEY);
    v->extra.m0 = amount.m0;
    v->payload.money.m1 = amount.m1;
    v->payload.money.m2 = amount.m2;
    v->payload.money.s = amount.s;
    v->payload.money.e = amount.e;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  TIME!
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_TIME(v) \
    ((v)->payload.time.nanoseconds)

#define TIME_SEC(n) \
    (cast(REBI64, n) * 1000000000L)

#define MAX_SECONDS \
    ((cast(REBI64, 1) << 31) - 1)

#define MAX_HOUR \
    (MAX_SECONDS / 3600)

#define MAX_TIME \
    (cast(REBI64, MAX_HOUR) * HR_SEC)

#define NANO 1.0e-9

#define SEC_SEC \
    cast(REBI64, 1000000000L)

#define MIN_SEC \
    (60 * SEC_SEC)

#define HR_SEC \
    (60 * 60 * SEC_SEC)

#define SEC_TIME(n) \
    ((n) * SEC_SEC)

#define MIN_TIME(n) \
    ((n) * MIN_SEC)

#define HOUR_TIME(n) \
    ((n) * HR_SEC)

#define SECS_IN(n) \
    ((n) / SEC_SEC)

#define VAL_SECS(n) \
    (VAL_TIME(n) / SEC_SEC)

#define DEC_TO_SECS(n) \
    cast(REBI64, ((n) + 5.0e-10) * SEC_SEC)

#define SECS_IN_DAY 86400

#define TIME_IN_DAY \
    SEC_TIME(cast(REBI64, SECS_IN_DAY))

#define NO_TIME MIN_I64

inline static void Init_Time(RELVAL *v, REBI64 nanoseconds) {
    Reset_Val_Header(v, REB_TIME);
    VAL_TIME(v) = nanoseconds;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  DATE!
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_DATE(v) \
    ((v)->extra.date)

#define MAX_YEAR 0x3fff

#define VAL_YEAR(v) \
    ((v)->extra.date.date.year)

#define VAL_MONTH(v) \
    ((v)->extra.date.date.month)

#define VAL_DAY(v) \
    ((v)->extra.date.date.day)

#define VAL_ZONE(v) \
    ((v)->extra.date.date.zone)

#define ZONE_MINS 15

#define ZONE_SECS \
    (ZONE_MINS * 60)

#define MAX_ZONE \
    (15 * (60 / ZONE_MINS))


//=////////////////////////////////////////////////////////////////////////=//
//
//  TUPLE!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// TUPLE! is a Rebol2/R3-Alpha concept to fit up to 7 byte-sized integers
// directly into a value payload without needing to make a series allocation.
// At source level they would be numbers separated by dots, like `1.2.3.4.5`.
// This was mainly applied for IP addresses and RGB/RGBA constants, and
// considered to be a "lightweight"...it would allow PICK and POKE like a
// series, but did not behave like one due to not having a position.
//
// !!! Ren-C challenges the value of the TUPLE! type as defined.  Color
// literals are often hexadecimal (where BINARY! would do) and IPv6 addresses
// have a different notation.  It may be that `.` could be used for a more
// generalized partner to PATH!, where `a.b.1` would be like a/b/1
//

#define MAX_TUPLE \
    ((sizeof(REBCNT) * 2) - 1) // for same properties on 64-bit and 32-bit

#define VAL_TUPLE(v) \
    ((v)->payload.tuple.tuple + 1)

#define VAL_TUPLE_LEN(v) \
    ((v)->payload.tuple.tuple[0])

#define VAL_TUPLE_DATA(v) \
    ((v)->payload.tuple.tuple)

inline static void SET_TUPLE(RELVAL *v, const void *data) {
    Reset_Val_Header(v, REB_TUPLE);
    memcpy(VAL_TUPLE_DATA(v), data, sizeof(VAL_TUPLE_DATA(v)));
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  EVENT!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's events are used for the GUI and for network and I/O.  They are
// essentially just a union of some structures which are packed so they can
// fit into a REBVAL's payload size.
//
// The available event models are:
//
// * EVM_PORT
// * EVM_OBJECT
// * EVM_DEVICE
// * EVM_CALLBACK
// * EVM_GUI
//

#define VAL_EVENT_TYPE(v) \
    ((v)->payload.event.type)

#define VAL_EVENT_FLAGS(v) \
    ((v)->payload.event.flags)

#define VAL_EVENT_WIN(v) \
    ((v)->payload.event.win)

#define VAL_EVENT_MODEL(v) \
    ((v)->payload.event.model)

#define VAL_EVENT_DATA(v) \
    ((v)->payload.event.data)

#define VAL_EVENT_TIME(v) \
    ((v)->payload.event.time)

#define VAL_EVENT_REQ(v) \
    ((v)->extra.eventee.req)

#define VAL_EVENT_SER(v) \
    ((v)->extra.eventee.ser)

#define IS_EVENT_MODEL(v,f) \
    (VAL_EVENT_MODEL(v) == (f))

inline static void SET_EVENT_INFO(RELVAL *val, u8 type, u8 flags, u8 win) {
    VAL_EVENT_TYPE(val) = type;
    VAL_EVENT_FLAGS(val) = flags;
    VAL_EVENT_WIN(val) = win;
}

// Position event data

#define VAL_EVENT_X(v) \
    cast(REBINT, cast(short, VAL_EVENT_DATA(v) & 0xffff))

#define VAL_EVENT_Y(v) \
    cast(REBINT, cast(short, (VAL_EVENT_DATA(v) >> 16) & 0xffff))

#define VAL_EVENT_XY(v) \
    (VAL_EVENT_DATA(v))

inline static void SET_EVENT_XY(RELVAL *v, REBINT x, REBINT y) {
    //
    // !!! "conversion to u32 from REBINT may change the sign of the result"
    // Hence cast.  Not clear what the intent is.
    //
    VAL_EVENT_DATA(v) = cast(u32, ((y << 16) | (x & 0xffff)));
}

// Key event data

#define VAL_EVENT_KEY(v) \
    (VAL_EVENT_DATA(v) & 0xffff)

#define VAL_EVENT_KCODE(v) \
    ((VAL_EVENT_DATA(v) >> 16) & 0xffff)

inline static void SET_EVENT_KEY(RELVAL *v, REBCNT k, REBCNT c) {
    VAL_EVENT_DATA(v) = ((c << 16) + k);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  IMAGE!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! Ren-C's primary goals are to research and pin down fundamentals, where
// things like IMAGE! would be an extension through a user-defined type
// vs. being in the core.  The R3-Alpha code has been kept compiling here
// due to its usage in R3-GUI.
//

// QUAD=(Red, Green, Blue, Alpha)

#define QUAD_LEN(s) \
    SER_LEN(s)

#define QUAD_HEAD(s) \
    SER_DATA_RAW(s)

#define QUAD_SKIP(s,n) \
    (QUAD_HEAD(s) + ((n) * 4))

#define QUAD_TAIL(s) \
    (QUAD_HEAD(s) + (QUAD_LEN(s) * 4))

#define IMG_WIDE(s) \
    ((s)->misc.area.wide)

#define IMG_HIGH(s) \
    ((s)->misc.area.high)

#define IMG_DATA(s) \
    SER_DATA_RAW(s)

#define VAL_IMAGE_HEAD(v) \
    QUAD_HEAD(VAL_SERIES(v))

#define VAL_IMAGE_TAIL(v) \
    QUAD_SKIP(VAL_SERIES(v), VAL_LEN_HEAD(v))

#define VAL_IMAGE_DATA(v) \
    QUAD_SKIP(VAL_SERIES(v), VAL_INDEX(v))

#define VAL_IMAGE_BITS(v) \
    cast(REBCNT*, VAL_IMAGE_HEAD(v))

#define VAL_IMAGE_WIDE(v) \
    (IMG_WIDE(VAL_SERIES(v)))

#define VAL_IMAGE_HIGH(v) \
    (IMG_HIGH(VAL_SERIES(v)))

#define VAL_IMAGE_LEN(v) \
    VAL_LEN_AT(v)

#define Init_Image(v,s) \
    Init_Any_Series((v), REB_IMAGE, (s));

//tuple to image! pixel order bytes
#define TO_PIXEL_TUPLE(t) \
    TO_PIXEL_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
        VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)

//tuple to RGBA bytes
#define TO_COLOR_TUPLE(t) \
    TO_RGBA_COLOR(VAL_TUPLE(t)[0], VAL_TUPLE(t)[1], VAL_TUPLE(t)[2], \
        VAL_TUPLE_LEN(t) > 3 ? VAL_TUPLE(t)[3] : 0xff)


//=////////////////////////////////////////////////////////////////////////=//
//
//  GOB! Graphic Object
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! The GOB! is a datatype specific to R3-View.  Its data is a small
// fixed-size object.  It is linked together by series containing more
// GOBs and values, and participates in the garbage collection process.
//
// The monolithic structure of Rebol had made it desirable to take advantage
// of the memory pooling to quickly allocate, free, and garbage collect
// these.  If the GOB! is to become part of an extension mechanism for
// user-defined types (and hence not present in minimalist configurations)
// then this would require either exporting the memory pools as part of
// the service or expecting clients to use malloc or do their own pooling.
//

#define VAL_GOB(v) \
    ((v)->payload.gob.gob)

#define VAL_GOB_INDEX(v) \
    ((v)->payload.gob.index)

inline static void SET_GOB(RELVAL *v, REBGOB *g) {
    Reset_Val_Header(v, REB_GOB);
    VAL_GOB(v) = g;
    VAL_GOB_INDEX(v) = 0;
}


// !!! Because you cannot assign REBVALs to one another (e.g. `*dest = *src`)
// a function is used.  The reason that a function is used is because this
// gives more flexibility in decisions based on the destination cell regarding
// whether it is necessary to reify information in the source cell.
//
// That advanced purpose has not yet been implemented, because it requires
// being able to "sniff" a cell for its lifetime.  For now it only preserves
// the VALUE_FLAG_STACK bit, without actually doing anything with it.
//
// Interface designed to line up with Derelativize()
//
inline static REBVAL *Move_Value(RELVAL *out, const REBVAL *v)
{
    assert(
        Get_Val_Flag(v, NODE_FLAG_CELL)
        && Get_Val_Flag(v, NODE_FLAG_VALID)
    );
    assert(NOT_END(v));
    ASSERT_CELL_WRITABLE(out, __FILE__, __LINE__);

    out->header.bits &= CELL_MASK_RESET;
    out->header.bits |= v->header.bits & CELL_MASK_COPY;

    // Note: In theory it would be possible to make payloads that had stack
    // lifetimes by default, which would be promoted to GC lifetimes using
    // the same kind of logic that the on-demand reification of FRAME!s
    // uses.  In practice, this would be very difficult to take advantage of
    // in C, because it really applies best with things that can live on
    // the C stack--and Rebol arrays don't have that form of invocation.
    //
    out->payload = v->payload;

    if (
        // NOT(v->header.bits & (VALUE_FLAG_BINDABLE | VALUE_FLAG_STACK))
        // || v->extra.binding->header.bits & NODE_FLAG_MANAGED
        //
        NOT(v->header.bits & VALUE_FLAG_STACK)
    ) {
        // If the source value isn't the kind of value that can have a
        // non-reified binding (e.g. an INTEGER! or STRING!), then it is
        // fully specified by definition.
        //
        // Also, if it is the kind of value that can have a non-reified
        // binding but isn't resident on the stack, we know that it must have
        // already been reified.
        //
        // Finally, if it's the kind of thing that can have a non-reified
        // binding but it's managed, then that's also fine.
        //
        out->extra = v->extra;
        return KNOWN(out);
    }

    // If we get here, the source value is on the stack and has a non-reified
    // binding of some kind.  Check to see if the target stack level will
    // outlive the stack level of the non-reified material in the binding. 

    REBCNT bind_depth = 1; // !!! need to determine v's binding stack level
    REBCNT out_depth;
    if (NOT(out->header.bits & VALUE_FLAG_STACK))
        out_depth = 0;
    else
        out_depth = 1; // !!! need to determine out's stack level

    if (out_depth >= bind_depth) {
        //
        // The non-reified binding will outlive the output slot, so there is
        // no reason to reify it.
        //
        out->extra = v->extra;
        return KNOWN(out);
    }

    // This is the expensive case, we know the binding as-is will not outlive
    // the target slot.  A reification is necessary.

    // !!! Code is not written yet, but neither are there any actual non
    // reified bindings in the wild.

    out->extra = v->extra;
    return KNOWN(out);
}


// The way globals are currently declared, one cannot use the DECLARE_LOCAL
// macro...because they run through a strange PVAR and TVAR process.
// There would also be no FS_TOP in effect to capture when they are being
// initialized.  This is similar to INIT_CELL, but being tracked separately
// because the strategy needs more review.
//
// (In particular, the frame's miscellaneous `f->cell` needs review)
//
#define Prep_Global_Cell(cell) \
    INIT_CELL(cell)


//
// Rather than allow a REBVAL to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
// See VALUE_FLAG_STACK for the experimental motivation.  However, even if
// this were merely a synonym for a plain REBVAL declaration in the release
// build, it provides a useful generic hook into the point of declaration
// of a stack value.
//
// Note: because this will run instructions, a routine should avoid doing a
// DECLARE_LOCAL inside of a loop.  It should be at the outermost scope of
// the function.
//
// Note: when setting the header bits, it doesn't set NODE_FLAG_VALID,
// so this is a "trash" cell by default.
//
#define DECLARE_LOCAL(name) \
    REBSER name##_pair; \
    *cast(RELVAL*, &name##_pair) = *BLANK_VALUE; /* => tbd: FS_TOP FRAME! */ \
    REBVAL * const name = cast(REBVAL*, &name##_pair) + 1; \
    name->header.bits = NODE_FLAG_CELL | VALUE_FLAG_STACK
