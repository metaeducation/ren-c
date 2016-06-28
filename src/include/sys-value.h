//
//  File: %sys-value.h
//  Summary: {Accessor Functions for properties of a Rebol Value}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
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
// This file provides accessors for the various value types.  Because these
// accessors operate on REBVAL (or RELVAL) pointers, the inline functions need
// the complete struct definition available from all the payload types.
//
// See notes in %sys-rebval.h for the definition of the REBVAL structure.
//
// An attempt is made to group the accessors in sections.  Some functions are
// defined in %c-value.c for the sake of the grouping.
//
// !!! R3-Alpha only used macros.  This had the advantage of only needing
// the definitions available at the *callsite* vs at the point of the accessor
// definition.  Yet macros may have undesirable properties (especially if
// macro arguments are used more than once).
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
//  DEBUG PROBE AND PANIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The PROBE macro can be used in debug builds to mold a REBVAL much like the
// Rebol `probe` operation.  PROBE_MSG can add a message:
//
//     REBVAL *v = Some_Value_Pointer();
//
//     PROBE(v);
//     PROBE_MSG(v, "the v value debug dump label");
//
// In order to make it easier to find out where a piece of debug spew is
// coming from, the file and line number will be output as well.
//
// PANIC_VALUE causes a crash on a value, while trying to provide information
// that could identify where that value was assigned.  If it is resident
// in a series and you are using Address Sanitizer or Valgrind, then it should
// cause a crash that pinpoints the stack where that array was allocated.
//

#if !defined(NDEBUG)
    #define PROBE(v) \
        Probe_Core_Debug(NULL, __FILE__, __LINE__, (v))

    #define PROBE_MSG(v, m) \
        Probe_Core_Debug((m), __FILE__, __LINE__, (v))

    #define PANIC_VALUE(v) \
        Panic_Value_Debug((v), __FILE__, __LINE__)
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
// The 64 basic Rebol types ("kinds" of values) are shifted left 2 bits.
// This makes their range go from 0..251 instead of from 0..63.  Reasoning is
// that most of the time the types are just used in comparisons, so it's
// usually cheaper to not shift out the 2 low bits used for END and WRITABLE.
//
// But to index into a zero based array with 64 elements, these XXX_0 forms
// are available to do shifting.  See also REB_MAX_0
//
// VAL_TYPE() should obviously not be called on uninitialized memory.  But
// it should also not be called on an END marker, as those markers only
// guarantee the low bit as having Rebol-readable-meaning.  In debug builds,
// this is asserted by VAL_TYPE_Debug.
//

#define KIND_FROM_0(z) \
    cast(enum Reb_Kind, (z) << 2)

#define TO_0_FROM_KIND(k) \
    (cast(REBCNT, (k)) >> 2)

#define FLAGIT_KIND(t) \
    (cast(REBU64, 1) << TO_0_FROM_KIND(t)) // makes a 64-bit bitflag

// While inline vs. macro doesn't usually matter much, debug builds won't
// inline this, and it's called *ALL* the time.  Since it doesn't repeat its
// argument, it's not worth it to make it a function for slowdown caused.
// Also, don't bother checking using the `cast()` template in C++.
//
#define VAL_TYPE_COMMON(v) \
    ((enum Reb_Kind)((v)->header.bits & HEADER_TYPE_MASK))

#ifdef NDEBUG
    #define VAL_TYPE(v) \
        VAL_TYPE_COMMON(v)
#else
    enum {
        VOID_FLAG_NOT_TRASH = (1 << TYPE_SPECIFIC_BIT),
        VOID_FLAG_SAFE_TRASH = (2 << TYPE_SPECIFIC_BIT)
    };

    inline static REBOOL IS_TRASH_DEBUG(const RELVAL *v) {
        // note this is directly inlined into VAL_TYPE_Debug() below
        return LOGICAL(
            (v->header.bits & HEADER_TYPE_MASK) == REB_0
            && !((v->header.bits & VOID_FLAG_NOT_TRASH))
        );
    }

    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        if (
            (v->header.bits & CELL_MASK)
            && NOT(IS_END_MACRO(v)) // IS_END redundantly checks trash
            && NOT(
                (v->header.bits & HEADER_TYPE_MASK) == REB_0
                && !((v->header.bits & VOID_FLAG_NOT_TRASH))
            ) // ^-- *so* frequent, debug builds hand-inline IS_TRASH_DEBUG()
        ){
            return VAL_TYPE_COMMON(v);
        }

        assert(v->header.bits & CELL_MASK);
        printf("END marker or garbage/trash in VAL_TYPE()\n");
        fflush(stdout);
        Panic_Value_Debug(v, file, line);
    }

    #define VAL_TYPE(v) \
        VAL_TYPE_Debug((v), __FILE__, __LINE__)
#endif

#define VAL_TYPE_0(v) \
    TO_0_FROM_KIND(VAL_TYPE(v))

inline static void VAL_SET_TYPE_BITS(RELVAL *v, enum Reb_Kind kind) {
    //
    // Note: Only use if you are sure the new type payload is in sync with
    // the type and bits (e.g. changing ANY-WORD! to another ANY-WORD!).
    // Otherwise the value-specific flags might be misinterpreted.
    //
    // Use VAL_RESET_HEADER() to set the type AND initialize the flags to 0.
    //
    assert(!IS_TRASH_DEBUG(v));
    (v)->header.bits &= ~cast(REBUPT, HEADER_TYPE_MASK);
    (v)->header.bits |= cast(REBUPT, kind);
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
// For safety in the debug build, all the type-specific flags include their
// type as part of the flag.  This type is checked first, and then masked out
// to use the single-bit-flag value which is intended.  The check uses the
// an exemplar to identify the category (e.g. REB_OBJECT for ANY-CONTEXT!)
//

#ifdef NDEBUG
    inline static void SET_VAL_FLAGS(RELVAL *v, REBUPT f) {
        v->header.bits |= f;
    }

    #define SET_VAL_FLAG(v,f) \
        SET_VAL_FLAGS((v), (f))

    inline static REBOOL GET_VAL_FLAG(const RELVAL *v, REBUPT f) {
        return LOGICAL(v->header.bits & f);
    }

    inline static void CLEAR_VAL_FLAGS(RELVAL *v, REBUPT f) {
        v->header.bits &= ~f;
    }

    #define CLEAR_VAL_FLAG(v,f) \
        CLEAR_VAL_FLAGS((v), (f))
#else
    inline static void SET_VAL_FLAGS(RELVAL *v, REBUPT f) {
        Assert_Flags_Are_For_Value(v, f);
        v->header.bits |= f & ~HEADER_TYPE_MASK;
    }

    inline static void SET_VAL_FLAG(RELVAL *v, REBUPT f) {
        SET_VAL_FLAGS(v, f);
        f &= ~HEADER_TYPE_MASK;
        assert(f && f == (f & -f)); // checks that only one bit is set
    }

    inline static REBOOL GET_VAL_FLAG(const RELVAL *v, REBUPT f) {
        Assert_Flags_Are_For_Value(v, f);
        f &= ~HEADER_TYPE_MASK;
        assert(f && f == (f & -f)); // checks that only one bit is set
        return LOGICAL(v->header.bits & f);
    }

    inline static void CLEAR_VAL_FLAGS(RELVAL *v, REBUPT f) {
        Assert_Flags_Are_For_Value(v, f);
        v->header.bits &= ~(f & ~HEADER_TYPE_MASK);
    }

    inline static void CLEAR_VAL_FLAG(RELVAL *v, REBUPT f) {
        CLEAR_VAL_FLAGS(v, f);
        f &= ~HEADER_TYPE_MASK;
        assert(f && f == (f & -f)); // checks that only one bit is set
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACKING PAYLOAD (for types that don't use their payloads)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some datatypes like NONE! or LOGIC!, or a void cell, can be communicated
// entirely by their header bits.  Though this "wastes" 3/4 of the space of
// the value cell, it also means the cell can be written and tested quickly.
//
// The release build does not canonize the remaining bits of the payload so
// they are left as random data.  But the debug build can take advantage of
// it to store some tracking information about the point and moment of
// initialization.  This data can be viewed in the debugging watchlist
// under the `track` component of payload, and is also used by PANIC_VALUE.
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
// VAL_RESET_HEADER clears out the header and sets it to a new type (and also
// sets the option bits indicating the value is *not* an END marker, and
// that the value is a full cell which can be written to).
//
// The debug build includes an extra check that the value we are about
// to write the header of is actually a full REBVAL-sized slot...and not
// just an implicit END marker that's really doing double-duty as some
// internal pointer of a container structure.
//
// The debug build also requires that value slots which are to be written
// to via VAL_RESET_HEADER() be marked writable.  Series and other value
// containers do this automatically, but if you make a REBVAL as a stack
// variable then it will have to be done before any write can happen.
//

inline static void VAL_RESET_HEADER_COMMON(RELVAL *v, enum Reb_Kind kind) {
    (v)->header.bits = NOT_END_MASK | CELL_MASK | cast(REBUPT, kind);
}

#ifdef NDEBUG
    #define ASSERT_CELL_WRITABLE_IF_DEBUG(v) \
        NOOP

    #define INIT_CELL_WRITABLE_IF_DEBUG(v) \
        NOOP

    #define MARK_CELL_WRITABLE_IF_DEBUG(v) \
        NOOP

    #define MARK_CELL_UNWRITABLE_IF_DEBUG(v) \
        NOOP

    #define VAL_RESET_HEADER(v,t) \
        VAL_RESET_HEADER_COMMON((v), (t))
#else
    #ifdef __cplusplus
        #define ASSERT_CELL_WRITABLE_IF_DEBUG(v,file,line) \
            Assert_Cell_Writable((v), (file), (line))

        // trashes completely
        #define INIT_CELL_WRITABLE_IF_DEBUG(v) \
            ((v)->header.bits = CELL_MASK | VALUE_FLAG_WRITABLE_DEBUG)

        // just adds bit
        #define MARK_CELL_WRITABLE_IF_DEBUG(v) \
            ((v)->header.bits |= VALUE_FLAG_WRITABLE_DEBUG)

        #define MARK_CELL_UNWRITABLE_IF_DEBUG(v) \
            ((v)->header.bits &= ~cast(REBUPT, VALUE_FLAG_WRITABLE_DEBUG))
    #else
        #define ASSERT_CELL_WRITABLE_IF_DEBUG(v,file,line) \
            NOOP

        #define INIT_CELL_WRITABLE_IF_DEBUG(v) \
            NOOP

        #define MARK_CELL_WRITABLE_IF_DEBUG(v) \
            NOOP

        #define MARK_CELL_UNWRITABLE_IF_DEBUG(v) \
            NOOP
    #endif

    inline static void VAL_RESET_HEADER_Debug(
        RELVAL *v, enum Reb_Kind kind, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_DEBUG(v, file, line);
        VAL_RESET_HEADER_COMMON(v, kind);
        v->header.bits |= CELL_MASK | VALUE_FLAG_WRITABLE_DEBUG;
    }

    #define VAL_RESET_HEADER(v,k) \
        VAL_RESET_HEADER_Debug((v), (k), __FILE__, __LINE__)
#endif

inline static void SET_ZEROED(RELVAL *v, enum Reb_Kind kind) {
    //
    // !!! SET_ZEROED is a capturing of a dodgy behavior of R3-Alpha,
    // which was to assume that clearing the payload of a value and then
    // setting the header made it the `zero?` of that type.  Review uses.
    //
    VAL_RESET_HEADER(v, kind);
    CLEAR(&v->extra, sizeof(union Reb_Value_Extra));
    CLEAR(&v->payload, sizeof(union Reb_Value_Payload));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VOID or TRASH
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Voids are a transient product of evaluation (e.g. the result of `do []`).
// They cannot be stored in blocks, and if a variable is assigned a void
// cell then that variable is considered to be "unset".  Void is thus not
// considered to be a "value type", but a bit pattern used to mark cells
// as not containing any value at all.
//
// In the debug build, there is a special flag that if it is not set, the
// cell is assumed to be "trash".  This is what's used to fill memory that
// in the release build would be uninitialized data.  To prevent it from being
// inspected while it's in an invalid state, VAL_TYPE used on a trash value
// will assert in the debug build.
//
// IS_TRASH_DEBUG() can be used to test for trash, but in debug builds only.
// The macros for setting trash will compile in both debug and release builds,
// though an unsafe trash will be a NOOP in release builds.  (So the "trash"
// will be uninitialized memory, in that case.)  A safe trash set turns into
// a regular void in release builds.
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by PANIC_VALUE)
//

#define VOID_CELL \
    c_cast(const REBVAL*, &PG_Void_Cell[0])

inline static REBOOL IS_VOID(const RELVAL *v)
    { return LOGICAL(VAL_TYPE(v) == REB_0); } // not a "type", so no IS_0 test

#ifdef NDEBUG
    inline static void SET_VOID(RELVAL *v)
        { VAL_RESET_HEADER(v, REB_0); }

    #define SET_TRASH_IF_DEBUG(v) \
        NOOP

    #define SET_TRASH_SAFE(v) \
        SET_VOID(v)

    #define IS_VOID_OR_SAFE_TRASH(v) \
        IS_VOID(v)

    inline static REBVAL *SINK(RELVAL *v) {
        return cast(REBVAL*, v);
    }
#else
    inline static void Set_Void_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_0);
        SET_VAL_FLAG(v, VOID_FLAG_NOT_TRASH);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void Set_Trash_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_0); // we don't set VOID_FLAG_NOT_TRASH
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void Set_Trash_Safe_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_0);
        SET_VAL_FLAG(v, VOID_FLAG_SAFE_TRASH);
        Set_Track_Payload_Debug(v, file, line);
    }

    #define SET_VOID(v) \
        Set_Void_Debug((v), __FILE__, __LINE__)

    #define SET_TRASH_IF_DEBUG(v) \
        Set_Trash_Debug((v), __FILE__, __LINE__)

    #define SET_TRASH_SAFE(v) \
        Set_Trash_Safe_Debug((v), __FILE__, __LINE__)

    inline static REBOOL IS_VOID_OR_SAFE_TRASH(const RELVAL *v) {
        if (IS_TRASH_DEBUG(v) && GET_VAL_FLAG(v, VOID_FLAG_SAFE_TRASH))
            return TRUE; // only the GC should treat "safe" trash as void
        if (IS_VOID(v))
            return TRUE;
        return FALSE;
    }

    inline static REBVAL *Sink_Debug(RELVAL *v, const char *file, int line) {
        //
        // SINK claims it's okay to cast from RELVAL to REBVAL because the
        // value is just going to be written to.  Verify that claim in the
        // debug build by setting to trash as part of the cast.
        //
        Set_Trash_Debug(v, file, line);
        return cast(REBVAL*, v);
    }

    #define SINK(v) \
        Sink_Debug((v), __FILE__, __LINE__)
#endif


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
//     append [a b c] (|)                       ;-- is legal
//     append [a b c] '|                        ;-- is legal
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by PANIC_VALUE)
//

#ifdef NDEBUG
    inline static void SET_BAR(RELVAL *v)
        { VAL_RESET_HEADER(v, REB_BAR); }

    inline static void SET_LIT_BAR(RELVAL *v)
        { VAL_RESET_HEADER(v, REB_LIT_BAR); }
#else
    inline static void SET_BAR_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_BAR);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void SET_LIT_BAR_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_LIT_BAR);
        Set_Track_Payload_Debug(v, file, line);
    }

    #define SET_BAR(v) \
        SET_BAR_Debug((v), __FILE__, __LINE__)

    #define SET_LIT_BAR(v) \
        SET_LIT_BAR_Debug((v), __FILE__, __LINE__)
#endif

#define BAR_VALUE (&PG_Bar_Value[0])


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
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by PANIC_VALUE)
//

inline static void SET_BLANK_COMMON(RELVAL *v) {
    v->header.bits = VALUE_FLAG_FALSE | NOT_END_MASK | CELL_MASK | REB_BLANK;
}

#ifdef NDEBUG
    #define SET_BLANK(v) \
        SET_BLANK_COMMON(v)
#else
    inline static void SET_BLANK_Debug(
        RELVAL *v, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_DEBUG(v, file, line);
        SET_BLANK_COMMON(v);
        MARK_CELL_WRITABLE_IF_DEBUG(v);
        Set_Track_Payload_Debug(v, file, line);
    }
    #define SET_BLANK(v) \
        SET_BLANK_Debug((v), __FILE__, __LINE__)
#endif

#define BLANK_VALUE \
    (&PG_Blank_Value[0])


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
// Conditional truth and falsehood allows an interpretation where a NONE!
// is a "falsey" value as well as logic false.  Voids are neither
// conditionally true nor conditionally false, and so debug builds will
// complain if you try to determine which it is.  (It likely means a mistake
// was made in skipping a formal decision-point regarding whether an unset
// should represent an "opt out" or an error.)
//
// Doesn't need payload...so the debug build adds information in Reb_Track
// which can be viewed in the debug watchlist (or shown by PANIC_VALUE)
//

#define FALSE_VALUE \
    c_cast(const REBVAL*, &PG_False_Value[0])

#define TRUE_VALUE \
    c_cast(const REBVAL*, &PG_True_Value[0])

inline static void SET_TRUE_COMMON(RELVAL *v) {
    v->header.bits = REB_LOGIC | NOT_END_MASK | CELL_MASK;
}

inline static void SET_FALSE_COMMON(RELVAL *v) {
    v->header.bits = REB_LOGIC | NOT_END_MASK | CELL_MASK | VALUE_FLAG_FALSE;
}

#define IS_CONDITIONAL_FALSE_COMMON(v) \
    GET_VAL_FLAG((v), VALUE_FLAG_FALSE)

#ifdef NDEBUG
    #define SET_TRUE(v) \
        SET_TRUE_COMMON(v)

    #define SET_FALSE(v) \
        SET_FALSE_COMMON(v)

    inline static void SET_LOGIC(RELVAL *v, REBOOL b) {
        if (b)
            SET_TRUE(v);
        else
            SET_FALSE(v);
    }

    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_COMMON(v)
#else
    inline static void SET_TRUE_Debug(
        RELVAL *v, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_DEBUG(v, file, line);
        SET_TRUE_COMMON(v);
        MARK_CELL_WRITABLE_IF_DEBUG(v);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void SET_FALSE_Debug(
        RELVAL *v, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_DEBUG(v, file, line);
        SET_FALSE_COMMON(v);
        MARK_CELL_WRITABLE_IF_DEBUG(v);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void SET_LOGIC_Debug(
        RELVAL *v, REBOOL b, const char *file, int line
    ){
        if (b)
            SET_TRUE_Debug(v, file, line);
        else
            SET_FALSE_Debug(v, file, line);
    }

    inline static REBOOL IS_CONDITIONAL_FALSE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        if (IS_VOID(v)) {
            printf("Conditional true/false test on void\n");
            fflush(stdout);
            Panic_Value_Debug(v, file, line);
        }
        return IS_CONDITIONAL_FALSE_COMMON(v);
    }

    #define SET_TRUE(v) \
        SET_TRUE_Debug((v), __FILE__, __LINE__)

    #define SET_FALSE(v) \
        SET_FALSE_Debug((v), __FILE__, __LINE__)

    #define SET_LOGIC(v,b) \
        SET_LOGIC_Debug((v), (b), __FILE__, __LINE__)

    #define IS_CONDITIONAL_FALSE(v) \
        IS_CONDITIONAL_FALSE_Debug((v), __FILE__, __LINE__)
#endif

#define IS_CONDITIONAL_TRUE(v) \
    NOT(IS_CONDITIONAL_FALSE(v)) // macro gets file + line # in debug build

inline static REBOOL VAL_LOGIC(const RELVAL *v) {
    assert(IS_LOGIC(v));
    return NOT(GET_VAL_FLAG((v), VALUE_FLAG_FALSE));
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

#define VAL_TYPE_KIND_0(v) \
    TO_0_FROM_KIND(VAL_TYPE_KIND(v))

#define VAL_TYPE_SPEC(v) \
    ((v)->payload.datatype.spec)

#define IS_KIND_SYM(s) \
    ((s) < REB_MAX_0)

#define KIND_FROM_SYM(s) \
    cast(enum Reb_Kind, KIND_FROM_0(s))

#define SYM_FROM_KIND(k) \
    cast(REBSYM, TO_0_FROM_KIND(k))

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

inline static void SET_CHAR(RELVAL *v, REBUNI uni) {
    VAL_RESET_HEADER(v, REB_CHAR);
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

#ifdef NDEBUG
    #define VAL_INT64(v) \
        ((v)->payload.integer)
#else
    inline static REBI64 *VAL_INT64_Ptr_Debug(const RELVAL *value) {
        assert(IS_INTEGER(value));
        return &m_cast(REBVAL*, const_KNOWN(value))->payload.integer;
    }

    #define VAL_INT64(v) \
        (*VAL_INT64_Ptr_Debug(v)) // allows lvalue: `VAL_INT64(x) = xxx`
#endif

inline static void SET_INTEGER(RELVAL *v, REBI64 i64) {
    VAL_RESET_HEADER(v, REB_INTEGER);
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

#ifdef NDEBUG
    #define VAL_DECIMAL(v) \
        ((v)->payload.decimal)
#else
    inline static REBDEC *VAL_DECIMAL_Ptr_Debug(const RELVAL *value) {
        assert(IS_DECIMAL(value) || IS_PERCENT(value));
        return &m_cast(REBVAL*, const_KNOWN(value))->payload.decimal;
    }
    #define VAL_DECIMAL(v) \
        (*VAL_DECIMAL_Ptr_Debug(v)) // allows lvalue: `VAL_DECIMAL(v) = xxx`
#endif

inline static void SET_DECIMAL(RELVAL *v, REBDEC d) {
    VAL_RESET_HEADER(v, REB_DECIMAL);
    v->payload.decimal = d;
}

inline static void SET_PERCENT(RELVAL *v, REBDEC d) {
    VAL_RESET_HEADER(v, REB_PERCENT);
    v->payload.decimal = d;
}

// !!! Several parts of the code wanted to access the decimal as "bits" through
// reinterpreting the bits as a 64-bit integer.  In the general case this is
// undefined behavior, and should be changed!  (It's better than it was,
// because it used to use the disengaged integer state of the payload union...
// calling VAL_INT64() on a MONEY! or a DECIMAL!  At least this documents it.

inline static REBI64 VAL_DECIMAL_BITS(const RELVAL *v) {
    assert(IS_DECIMAL(v) || IS_PERCENT(v));
    return *cast(const REBI64*, &v->payload.decimal);
}

inline static void INIT_DECIMAL_BITS(REBVAL *v, REBI64 bits) {
    assert(IS_DECIMAL(v) || IS_PERCENT(v));
    *cast(REBI64*, &v->payload.decimal) = bits;
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

inline static void SET_MONEY(RELVAL *v, deci amount) {
    VAL_RESET_HEADER(v, REB_MONEY);
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

inline static void SET_TIME(RELVAL *v, REBI64 nanoseconds) {
    VAL_RESET_HEADER(v, REB_TIME);
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
// TUPLE! is a Rebol2/R3-Alpha concept to fit up to 10 byte-sized integers
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

#define MAX_TUPLE 10

#define VAL_TUPLE(v) \
    ((v)->payload.tuple.tuple + 1)

#define VAL_TUPLE_LEN(v) \
    ((v)->payload.tuple.tuple[0])

#define VAL_TUPLE_DATA(v) \
    ((v)->payload.tuple.tuple)

inline static void SET_TUPLE(RELVAL *v, const void *data) {
    VAL_RESET_HEADER(v, REB_TUPLE);
    memcpy(VAL_TUPLE_DATA(v), data, sizeof(VAL_TUPLE_DATA(v)));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  PAIR!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! A pair contains two floating point values.  This makes it
// uncomfortably not able to store two arbitrary INTEGER!s, nor two
// arbitrary DECIMAL!s.
//
#define VAL_PAIR(v) \
    ((v)->payload.pair)

#define VAL_PAIR_X(v) \
    ((v)->payload.pair.x)

#define VAL_PAIR_Y(v) \
    ((v)->payload.pair.y)

#define VAL_PAIR_X_INT(v) \
    ROUND_TO_INT((v)->payload.pair.x)

#define VAL_PAIR_Y_INT(v) \
    ROUND_TO_INT((v)->payload.pair.y)

inline static void SET_PAIR(RELVAL *v, float x, float y) {
    VAL_RESET_HEADER(v, REB_PAIR);
    VAL_PAIR_X(v) = x;
    VAL_PAIR_Y(v) = y;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BITSET!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// !!! As written, bitsets use the Any_Series structure in their
// implementation, but are not considered to be an ANY-SERIES! type.
//

#define VAL_BITSET(v) \
    VAL_SERIES(v)

#define Val_Init_Bitset(v,s) \
    Val_Init_Series((v), REB_BITSET, (s))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

inline static REBSER *VAL_SERIES(const RELVAL *v) {
    assert(ANY_SERIES(v) || IS_MAP(v) || IS_VECTOR(v) || IS_IMAGE(v));
    return v->payload.any_series.series;
}

inline static void INIT_VAL_SERIES(RELVAL *v, REBSER *s) {
    assert(!Is_Array_Series(s));
    v->payload.any_series.series = s;
}

#define VAL_INDEX(v) \
    ((v)->payload.any_series.index)

#define VAL_LEN_HEAD(v) \
    SER_LEN(VAL_SERIES(v))

inline static REBCNT VAL_LEN_AT(const RELVAL *v) {
    if (VAL_INDEX(v) >= VAL_LEN_HEAD(v))
        return 0; // avoid negative index
    return VAL_LEN_HEAD(v) - VAL_INDEX(v); // take current index into account
}

inline static REBYTE *VAL_RAW_DATA_AT(const RELVAL *v) {
    return SER_AT_RAW(SER_WIDE(VAL_SERIES(v)), VAL_SERIES(v), VAL_INDEX(v));
}

inline static REBMAP *VAL_MAP(const RELVAL *v) {
    assert(IS_MAP(v));

    // Ren-C introduced const REBVAL* usage, but propagating const vs non
    // const REBSER pointers didn't show enough benefit to be worth the
    // work in supporting them (at this time).  Mutability cast needed.
    //
    return AS_MAP(m_cast(RELVAL*, v)->payload.any_series.series);
}

#define Val_Init_Series_Index(v,t,s,i) \
    Val_Init_Series_Index_Core(SINK(v), (t), (s), (i), SPECIFIED)

#define Val_Init_Series(v,t,s) \
    Val_Init_Series_Index((v), (t), (s), 0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define VAL_BIN(v) \
    BIN_HEAD(VAL_SERIES(v))

#define VAL_BIN_HEAD(v) \
    BIN_HEAD(VAL_SERIES(v))

inline static REBYTE *VAL_BIN_AT(const RELVAL *v) {
    return BIN_AT(VAL_SERIES(v), VAL_INDEX(v));
}

inline static REBYTE *VAL_BIN_TAIL(const RELVAL *v) {
    return SER_TAIL(REBYTE, VAL_SERIES(v));
}

// !!! RE: VAL_BIN_AT_HEAD() see remarks on VAL_ARRAY_AT_HEAD()
//
#define VAL_BIN_AT_HEAD(v,n) \
    BIN_AT(VAL_SERIES(v), (n))

#define VAL_BYTE_SIZE(v) \
    BYTE_SIZE(VAL_SERIES(v))

#define Val_Init_Binary(v,s) \
    Val_Init_Series((v), REB_BINARY, (s))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-STRING! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define Val_Init_String(v,s) \
    Val_Init_Series((v), REB_STRING, (s))

#define Val_Init_File(v,s) \
    Val_Init_Series((v), REB_FILE, (s))

#define Val_Init_Tag(v,s) \
    Val_Init_Series((v), REB_TAG, (s))

#define VAL_UNI(v) \
    UNI_HEAD(VAL_SERIES(v))

#define VAL_UNI_HEAD(v) \
    UNI_HEAD(VAL_SERIES(v))

#define VAL_UNI_AT(v) \
    UNI_AT(VAL_SERIES(v), VAL_INDEX(v))

#define VAL_ANY_CHAR(v) \
    GET_ANY_CHAR(VAL_SERIES(v), VAL_INDEX(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-ARRAY! (uses `struct Reb_Any_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//

#define EMPTY_BLOCK \
    ROOT_EMPTY_BLOCK

#define EMPTY_ARRAY \
    VAL_ARRAY(ROOT_EMPTY_BLOCK)

#define EMPTY_STRING \
    ROOT_EMPTY_STRING

inline static REBCTX *VAL_SPECIFIER(const REBVAL *v) {
    assert(ANY_ARRAY(v));
    return VAL_SPECIFIC(v);
}

inline static void INIT_SPECIFIC(RELVAL *v, REBCTX *context) {
    assert(NOT(GET_VAL_FLAG(v, VALUE_FLAG_RELATIVE)));
    v->extra.binding = CTX_VARLIST(context);
}

inline static void INIT_RELATIVE(RELVAL *v, REBFUN *func) {
    assert(GET_VAL_FLAG(v, VALUE_FLAG_RELATIVE));
    v->extra.binding = FUNC_PARAMLIST(func);
}

inline static void INIT_VAL_ARRAY(RELVAL *v, REBARR *a) {
    SET_VAL_FLAG(v, VALUE_FLAG_ARRAY);
    v->extra.binding = cast(REBARR*, SPECIFIED);
    v->payload.any_series.series = ARR_SERIES(a);
}

// These array operations take the index position into account.  The use
// of the word AT with a missing index is a hint that the index is coming
// from the VAL_INDEX() of the value itself.
//
#define VAL_ARRAY_AT(v) \
    ARR_AT(VAL_ARRAY(v), VAL_INDEX(v))

#define VAL_ARRAY_LEN_AT(v) \
    VAL_LEN_AT(v)

// These operations do not need to take the value's index position into
// account; they strictly operate on the array series
//
inline static REBARR *VAL_ARRAY(const RELVAL *v) {
    assert(ANY_ARRAY(v));
    return AS_ARRAY(v->payload.any_series.series);
}

#define VAL_ARRAY_HEAD(v) \
    ARR_HEAD(VAL_ARRAY(v))

inline static RELVAL *VAL_ARRAY_TAIL(const RELVAL *v) {
    return ARR_AT(VAL_ARRAY(v), VAL_ARRAY_LEN_AT(v));
}


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

#define VAL_TERM_ARRAY(v) \
    TERM_ARRAY(VAL_ARRAY(v))

#define Val_Init_Array_Index(v,t,a,i) \
    Val_Init_Series_Index((v), (t), ARR_SERIES(a), (i))

#define Val_Init_Array(v,t,a) \
    Val_Init_Array_Index((v), (t), (a), 0)

#define Val_Init_Block_Index(v,a,i) \
    Val_Init_Array_Index((v), REB_BLOCK, (a), (i))

#define Val_Init_Block(v,s) \
    Val_Init_Block_Index((v), (s), 0)



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

enum Reb_Param_Class {
    PARAM_CLASS_0 = 0, // reserve to catch uninitialized cases

    // `PARAM_CLASS_NORMAL` is cued by an ordinary WORD! in the function spec
    // to indicate that you would like that argument to be evaluated normally.
    //
    //     >> foo: function [a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 3
    //
    // Special outlier EVAL/ONLY can be used to subvert this:
    //
    //     >> eval/only :foo 1 + 2
    //     a is 1
    //     ** Script error: + operator is missing an argument
    //
    PARAM_CLASS_NORMAL = 0x01,

    // `PARAM_CLASS_HARD_QUOTE` is cued by a GET-WORD! in the function spec
    // dialect.  It indicates that a single value of  content at the callsite
    // should be passed through *literally*, without any evaluation:
    //
    //     >> foo: function [:a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is (1 + 2)
    //
    PARAM_CLASS_HARD_QUOTE = 0x02, // GET-WORD! in spec

    // `PARAM_CLASS_REFINEMENT`
    //
    PARAM_CLASS_REFINEMENT = 0x03,

    // `PARAM_CLASS_LOCAL` is a "pure" local, which will be set to void by
    // argument fulfillment.  It is indicated by a SET-WORD! in the function
    // spec, or by coming after a <local> tag in the function generators.
    //
    // !!! Initially these were indicated with TYPESET_FLAG_HIDDEN.  That
    // would allow the PARAM_CLASS to fit in just two bits (if there were
    // no debug-purpose PARAM_CLASS_0) and free up a scarce typeset flag.
    // But is it the case that hiding and localness should be independent?
    //
    PARAM_CLASS_LOCAL = 0x04,

    // PARAM_CLASS_RETURN acts like a pure local, but is pre-filled with a
    // definitionally-scoped function value that takes 1 arg and returns it.
    //
    PARAM_CLASS_RETURN = 0x05,

    // `PARAM_CLASS_SOFT_QUOTE` is cued by a LIT-WORD! in the function spec
    // dialect.  It quotes with the exception of GROUP!, GET-WORD!, and
    // GET-PATH!...which will be evaluated:
    //
    //     >> foo: function ['a] [print [{a is} a]
    //
    //     >> foo 1 + 2
    //     a is 1
    //
    //     >> foo (1 + 2)
    //     a is 3
    //
    // Although possible to implement soft quoting with hard quoting, it is
    // a convenient way to allow callers to "escape" a quoted context when
    // they need to.
    //
    // Note: Value chosen for PCLASS_ANY_QUOTE_MASK in common with hard quote
    //
    PARAM_CLASS_SOFT_QUOTE = 0x06,

    // `PARAM_CLASS_LEAVE` acts like a pure local, but is pre-filled with a
    // definitionally-scoped function value that takes 0 args and returns void
    //
    PARAM_CLASS_LEAVE = 0x07,

    PARAM_CLASS_MAX
};

#define PCLASS_ANY_QUOTE_MASK 0x02

#define PCLASS_MASK \
    (cast(REBUPT, 0x07) << TYPE_SPECIFIC_BIT)


#ifdef NDEBUG
    #define TYPESET_FLAG_X 0
#else
    #define TYPESET_FLAG_X REB_TYPESET // interpreted to mean ANY-TYPESET!
#endif

// Option flags used with GET_VAL_FLAG().  These describe properties of
// a value slot when it's constrained to the types in the typeset
//
enum {
    // Can't be changed (set with PROTECT)
    //
    TYPESET_FLAG_LOCKED = (1 << (TYPE_SPECIFIC_BIT + 3)) | TYPESET_FLAG_X,

    // Can't be reflected (set with PROTECT/HIDE) or local in spec as `foo:`
    //
    TYPESET_FLAG_HIDDEN = (1 << (TYPE_SPECIFIC_BIT + 4)) | TYPESET_FLAG_X,

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
    TYPESET_FLAG_UNBINDABLE = (1 << (TYPE_SPECIFIC_BIT + 5)) | TYPESET_FLAG_X,

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
    TYPESET_FLAG_DURABLE = (1 << (TYPE_SPECIFIC_BIT + 6)) | TYPESET_FLAG_X,

    // !!! This does not need to be on the typeset necessarily.  See the
    // VARARGS! type for what this is, which is a representation of the
    // capture of an evaluation position. The type will also be checked but
    // the value will not be consumed.
    //
    // Note the important distinction, that a variadic parameter and taking
    // a VARARGS! type are different things.  (A function may accept a
    // variadic number of VARARGS! values, for instance.)
    //
    TYPESET_FLAG_VARIADIC = (1 << (TYPE_SPECIFIC_BIT + 7)) | TYPESET_FLAG_X,

    // !!! In R3-Alpha, there were only 8 type-specific bits...with the
    // remaining bits "reserved for future use".  This goes over the line
    // with a 9th type-specific bit, which may or may not need review.
    // It could just be that more type-specific bits is the future use.

    // Endability is distinct from optional, and it means that a parameter is
    // willing to accept being at the end of the input.  This means either
    // an infix dispatch's left argument is missing (e.g. `do [+ 5]`) or an
    // ordinary argument hit the end (e.g. the trick used for `>> help` when
    // the arity is 1 usually as `>> help foo`)
    //
    TYPESET_FLAG_ENDABLE = (1 << (TYPE_SPECIFIC_BIT + 8)) | TYPESET_FLAG_X,

    // For performance, a cached PROTECTED_OR_LOOKAHEAD or'd flag could make
    // it so that each SET doesn't have to clear out the flag.  See
    // notes on that in variable setting.
    //
    TYPESET_FLAG_LOOKBACK = (1 << (TYPE_SPECIFIC_BIT + 9)) | TYPESET_FLAG_X
};

// Operations when typeset is done with a bitset (currently all typesets)

#define VAL_TYPESET_BITS(v) ((v)->payload.typeset.bits)

#define TYPE_CHECK(v,n) \
    LOGICAL(VAL_TYPESET_BITS(v) & FLAGIT_KIND(n))

#define TYPE_SET(v,n) \
    ((VAL_TYPESET_BITS(v) |= FLAGIT_KIND(n)), NOOP)

#define EQUAL_TYPESET(v,w) \
    (VAL_TYPESET_BITS(v) == VAL_TYPESET_BITS(w))


// Name should be NULL unless typeset in object keylist or func paramlist

inline static void INIT_TYPESET_NAME(RELVAL *typeset, REBSTR *str) {
    assert(IS_TYPESET(typeset));
    typeset->extra.key_spelling = str;
}

inline static REBSTR *VAL_KEY_CASED(const RELVAL *typeset) {
    assert(IS_TYPESET(typeset));
    return typeset->extra.key_spelling;
}

inline static REBSTR *VAL_KEY_CANON(const RELVAL *typeset) {
    return STR_CANON(VAL_KEY_CASED(typeset));
}

inline static OPT_REBSYM VAL_KEY_SYM(const RELVAL *typeset) {
    return STR_SYMBOL(VAL_KEY_CASED(typeset)); // mirrors canon's symbol
}

#define VAL_PARAM_CASED(p) VAL_KEY_CASED(p)
#define VAL_PARAM_CANON(p) VAL_KEY_CANON(p)
#define VAL_PARAM_SYM(p) VAL_KEY_SYM(p)

inline static enum Reb_Param_Class VAL_PARAM_CLASS(const RELVAL *v) {
    assert(IS_TYPESET(v));
    return cast(
        enum Reb_Param_Class,
        (v->header.bits & PCLASS_MASK) >> TYPE_SPECIFIC_BIT
    );
}

inline static void INIT_VAL_PARAM_CLASS(RELVAL *v, enum Reb_Param_Class c) {
    v->header.bits &= ~PCLASS_MASK;
    v->header.bits |= cast(REBUPT, c << TYPE_SPECIFIC_BIT);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-WORD! (`struct Reb_Any_Word`)
//
//=////////////////////////////////////////////////////////////////////////=//

#ifdef NDEBUG
    #define WORD_FLAG_X 0
#else
    #define WORD_FLAG_X REB_WORD // interpreted to mean ANY-WORD!
#endif

enum {
    // `WORD_FLAG_BOUND` answers whether a word is bound, but it may be
    // relatively bound if `VALUE_FLAG_RELATIVE` is set.  In that case, it
    // does not have a context pointer but rather a function pointer, that
    // must be combined with more information to get the FRAME! where the
    // word should actually be looked up.
    //
    // If VALUE_FLAG_RELATIVE is set, then WORD_FLAG_BOUND must also be set.
    //
    WORD_FLAG_BOUND = (1 << (TYPE_SPECIFIC_BIT + 0)) | WORD_FLAG_X,

    // A special kind of word is used during argument fulfillment to hold
    // a refinement's word on the data stack, augmented with its param
    // and argument location.  This helps fulfill "out-of-order" refinement
    // usages more quickly without having to do two full arglist walks.
    //
    WORD_FLAG_PICKUP = (1 << (TYPE_SPECIFIC_BIT + 1)) | WORD_FLAG_X
};

#define IS_WORD_BOUND(v) \
    GET_VAL_FLAG((v), WORD_FLAG_BOUND)

#define IS_WORD_UNBOUND(v) \
    NOT(IS_WORD_BOUND(v))

inline static void INIT_WORD_SPELLING(RELVAL *v, REBSTR *s) {
    assert(ANY_WORD(v));
    v->payload.any_word.spelling = s;
}

inline static REBSTR *VAL_WORD_CASED(const RELVAL *v) {
    assert(ANY_WORD(v));
    return v->payload.any_word.spelling;
}

inline static REBSTR *VAL_WORD_CANON(const RELVAL *v) {
    assert(ANY_WORD(v));
    return STR_CANON(v->payload.any_word.spelling);
}

inline static OPT_REBSYM VAL_WORD_SYM(const RELVAL *v) {
    return STR_SYMBOL(v->payload.any_word.spelling);
}

inline static void INIT_WORD_CONTEXT(RELVAL *v, REBCTX *context) {
    assert(GET_VAL_FLAG(v, WORD_FLAG_BOUND) && context != SPECIFIED);
    ENSURE_SERIES_MANAGED(CTX_SERIES(context));
    ASSERT_ARRAY_MANAGED(CTX_KEYLIST(context));
    v->extra.binding = CTX_VARLIST(context);
}

inline static REBCTX *VAL_WORD_CONTEXT(const REBVAL *v) {
    assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND));
    return VAL_SPECIFIC(v);
}

inline static void INIT_WORD_FUNC(RELVAL *v, REBFUN *func) {
    assert(GET_VAL_FLAG(v, WORD_FLAG_BOUND));
    v->extra.binding = FUNC_PARAMLIST(func);
}

inline static REBFUN *VAL_WORD_FUNC(const RELVAL *v) {
    assert(GET_VAL_FLAG(v, WORD_FLAG_BOUND));
    return VAL_RELATIVE(v);
}

inline static void INIT_WORD_INDEX(RELVAL *v, REBCNT i) {
    assert(ANY_WORD(v));
    assert(GET_VAL_FLAG((v), WORD_FLAG_BOUND));
    assert(SAME_STR(
        VAL_WORD_CASED(v),
        IS_RELATIVE(v)
            ? VAL_KEY_CASED(FUNC_PARAM(VAL_WORD_FUNC(v), i))
            : CTX_KEY_CASED(VAL_WORD_CONTEXT(KNOWN(v)), i)
    ));
    v->payload.any_word.index = cast(REBINT, i);
}

inline static REBCNT VAL_WORD_INDEX(const RELVAL *v) {
    assert(ANY_WORD(v));
    REBINT i = v->payload.any_word.index;
    assert(i > 0);
    return cast(REBCNT, i);
}

inline static void UNBIND_WORD(RELVAL *v) {
    CLEAR_VAL_FLAGS(v, WORD_FLAG_BOUND | VALUE_FLAG_RELATIVE);
#if !defined(NDEBUG)
    v->payload.any_word.index = 0;
#endif
}

inline static void Val_Init_Word(
    RELVAL *out,
    enum Reb_Kind kind,
    REBSTR *spelling
) {
    VAL_RESET_HEADER(out, kind);
    ASSERT_SERIES_MANAGED(spelling); // for now, all words are interned/shared
    INIT_WORD_SPELLING(out, spelling);

#if !defined(NDEBUG)
    out->payload.any_word.index = 0;
#endif

    assert(ANY_WORD(out));
    assert(IS_WORD_UNBOUND(out));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  FUNCTION! (`struct Reb_Function`)
//
//=////////////////////////////////////////////////////////////////////////=//

#ifdef NDEBUG
    #define FUNC_FLAG_X 0
#else
    #define FUNC_FLAG_X REB_FUNCTION
#endif

enum {
    // RETURN will always be in the last paramlist slot (if present)
    //
    FUNC_FLAG_RETURN = (1 << (TYPE_SPECIFIC_BIT + 0)) | FUNC_FLAG_X,

    // LEAVE will always be in the last paramlist slot (if present)
    //
    FUNC_FLAG_LEAVE = (1 << (TYPE_SPECIFIC_BIT + 1)) | FUNC_FLAG_X,

    // A function may act as a barrier on its left (so that it cannot act
    // as an input argument to another function).
    //
    // Given the "greedy" nature of infix, a function with arguments cannot
    // be stopped from infix consumption on its right--because the arguments
    // would consume them.  Only a function with no arguments is able to
    // trigger an error when used as a left argument.  This is the ability
    // given to lookback 0 arity functions, known as "punctuators".
    //
    FUNC_FLAG_PUNCTUATES = (1 << (TYPE_SPECIFIC_BIT + 2)) | FUNC_FLAG_X,

#if !defined(NDEBUG)
    //
    // TRUE-valued refinements, NONE! for unused args
    //
    FUNC_FLAG_LEGACY = (1 << (TYPE_SPECIFIC_BIT + 3)) | FUNC_FLAG_X,
#endif

    FUNC_FLAG_NO_COMMA // needed for proper comma termination of this list
};

#ifdef NDEBUG
    inline static REBFUN *VAL_FUNC(const RELVAL *v) {
        return AS_FUNC(v->payload.function.paramlist);
    }
#else
    #define VAL_FUNC(v) \
        VAL_FUNC_Debug(v)
#endif

inline static REBARR *VAL_FUNC_PARAMLIST(const RELVAL *v)
    { return FUNC_PARAMLIST(VAL_FUNC(v)); }

inline static REBCNT VAL_FUNC_NUM_PARAMS(const RELVAL *v)
    { return FUNC_NUM_PARAMS(VAL_FUNC(v)); }

inline static REBVAL *VAL_FUNC_PARAMS_HEAD(const RELVAL *v)
    { return FUNC_PARAMS_HEAD(VAL_FUNC(v)); }

inline static REBVAL *VAL_FUNC_PARAM(const RELVAL *v, REBCNT n)
    { return FUNC_PARAM(VAL_FUNC(v), n); }

inline static RELVAL *VAL_FUNC_BODY(const RELVAL *v)
    { return ARR_HEAD(v->payload.function.body_holder); }

inline static REBNAT VAL_FUNC_DISPATCHER(const RELVAL *v)
    { return ARR_SERIES(v->payload.function.body_holder)->misc.dispatcher; }

inline static REBCTX *VAL_FUNC_META(const RELVAL *v)
    { return ARR_SERIES(v->payload.function.paramlist)->link.meta; }

inline static REBOOL IS_FUNCTION_PLAIN(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Plain_Dispatcher); }

inline static REBOOL IS_FUNCTION_ACTION(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Action_Dispatcher); }

inline static REBOOL IS_FUNCTION_COMMAND(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Command_Dispatcher); }

inline static REBOOL IS_FUNCTION_SPECIALIZER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Specializer_Dispatcher); }

inline static REBOOL IS_FUNCTION_CHAINER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Chainer_Dispatcher); }

inline static REBOOL IS_FUNCTION_ADAPTER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Adapter_Dispatcher); }

inline static REBOOL IS_FUNCTION_RIN(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Routine_Dispatcher); }

inline static REBOOL IS_FUNCTION_HIJACKER(const RELVAL *v)
    { return LOGICAL(VAL_FUNC_DISPATCHER(v) == &Hijacker_Dispatcher); }

inline static REBRIN *VAL_FUNC_ROUTINE(const RELVAL *v) {
    return cast(REBRIN*, VAL_FUNC_BODY(v)->payload.handle.data);
}

inline static REBARR *VAL_BINDING(const RELVAL *v) {
    assert(
        ANY_ARRAY(v)
        || IS_FUNCTION(v)
        || ANY_CONTEXT(v)
        || IS_VARARGS(v)
        || ANY_WORD(v)
    );
    return v->extra.binding;
}

// !!! At the moment functions are "all durable" or "none durable" w.r.t. the
// survival of their arguments and locals after the call.
//
inline static REBOOL IS_FUNC_DURABLE(REBFUN *f) {
    return LOGICAL(
        FUNC_NUM_PARAMS(f) != 0
        && GET_VAL_FLAG(FUNC_PARAM(f, 1), TYPESET_FLAG_DURABLE)
    );
}

// Native values are stored in an array at boot time.  This is a convenience
// accessor for getting the "FUNC" portion of the native--e.g. the paramlist.
// It should compile to be as efficient as fetching any global pointer.

#define NAT_VALUE(name) \
    (&Natives[N_##name##_ID])

#define NAT_FUNC(name) \
    VAL_FUNC(NAT_VALUE(name))



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

inline static REBCTX *VAL_CONTEXT(const RELVAL *v) {
    assert(ANY_CONTEXT(v));
    return AS_CONTEXT(v->payload.any_context.varlist);
}

inline static void INIT_VAL_CONTEXT(REBVAL *v, REBCTX *c) {
    v->payload.any_context.varlist = CTX_VARLIST(c);
}

#define VAL_CONTEXT_FRAME(v) \
    CTX_FRAME(VAL_CONTEXT(v))

// Convenience macros to speak in terms of object values instead of the context
//
#define VAL_CONTEXT_VAR(v,n) \
    CTX_VAR(VAL_CONTEXT(v), (n))

#define VAL_CONTEXT_KEY(v,n) \
    CTX_KEY(VAL_CONTEXT(v), (n))

inline static REBCTX *VAL_CONTEXT_META(const RELVAL *v) {
    return ARR_SERIES(
        CTX_KEYLIST(AS_CONTEXT(v->payload.any_context.varlist))
    )->link.meta;
}

inline static REBCNT VAL_CONTEXT_STACKVARS_LEN(const RELVAL *v) {
    assert(ANY_CONTEXT(v));
    return CHUNK_LEN_FROM_VALUES(CTX_FRAME(VAL_CONTEXT(v))->stackvars);
}

#define VAL_CONTEXT_KEY_SYM(v,n) \
    CTX_KEY_SYM(VAL_CONTEXT(v), (n))

inline static void INIT_CONTEXT_FRAME(REBCTX *c, struct Reb_Frame *frame) {
    assert(IS_FRAME(CTX_VALUE(c)));
    ARR_SERIES(CTX_VARLIST(c))->misc.f = frame;
}

inline static void INIT_CONTEXT_META(REBCTX *c, REBCTX *m) {
    ARR_SERIES(CTX_KEYLIST(c))->link.meta = m;
}

inline static REBVAL *CTX_FRAME_FUNC_VALUE(REBCTX *c) {
    assert(IS_FUNCTION(CTX_ROOTKEY(c)));
    return CTX_ROOTKEY(c);
}

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
#define SELFISH(n) \
    ((n) + 1)

#define Val_Init_Context(out,kind,context) \
    Val_Init_Context_Core(SINK(out), (kind), (context))

#define Val_Init_Object(v,c) \
    Val_Init_Context((v), REB_OBJECT, (c))

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

#define ERR_VARS(e) \
    cast(ERROR_VARS*, CTX_VARS_HEAD(e))

#define ERR_NUM(e) \
    cast(REBCNT, VAL_INT32(&ERR_VARS(e)->code))

#define VAL_ERR_VARS(v) \
    ERR_VARS(VAL_CONTEXT(v))

#define VAL_ERR_NUM(v) \
    ERR_NUM(VAL_CONTEXT(v))

#define Val_Init_Error(v,c) \
    Val_Init_Context((v), REB_ERROR, (c))


//=////////////////////////////////////////////////////////////////////////=//
//
//  VARARGS! (`struct Reb_Varargs`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A VARARGS! represents a point for parameter gathering inline at the
// callsite of a function.  The point is located *after* that function has
// gathered all of its arguments and started running.  It is implemented by
// holding a reference to a reified FRAME! series, which allows it to find
// the point of a running evaluation (as well as to safely check for when
// that call is no longer on the stack, and can't provide data.)
//
// A second VARARGS! form is implemented as a thin proxy over an ANY-ARRAY!.
// This mimics the interface of feeding forward through those arguments, to
// allow for "parameter packs" that can be passed to variadic functions.
//
// When the bits of a payload of a VARARG! are copied from one item to
// another, they are still maintained in sync.  TAKE-ing a vararg off of one
// is reflected in the others.  This means that the "indexor" position of
// the vararg is located through the frame pointer.  If there is no frame,
// then a single element array (the `array`) holds an ANY-ARRAY! value that
// is shared between the instances, to reflect the state.
//

#ifdef NDEBUG
    #define VARARGS_FLAG_X 0
#else
    #define VARARGS_FLAG_X REB_VARARGS
#endif

enum {
    // Was made with a call to MAKE VARARGS! with data from an ANY-ARRAY!
    // If that is the case, it does not use the varargs payload at all,
    // rather it uses the Reb_Any_Series payload.
    //
    VARARGS_FLAG_NO_FRAME = (1 << (TYPE_SPECIFIC_BIT + 0)) | VARARGS_FLAG_X
};

inline static const REBVAL *VAL_VARARGS_PARAM(const RELVAL *v)
    { return v->payload.varargs.param; }

inline static REBVAL *VAL_VARARGS_ARG(const RELVAL *v)
    { return v->payload.varargs.arg; }

inline static REBCTX *VAL_VARARGS_FRAME_CTX(const RELVAL *v) {
    ASSERT_ARRAY_MANAGED(v->extra.binding);
    assert(GET_ARR_FLAG(v->extra.binding, ARRAY_FLAG_CONTEXT_VARLIST));
    return AS_CONTEXT(v->extra.binding);
}

inline static REBARR *VAL_VARARGS_ARRAY1(const RELVAL *v) {
    assert(!GET_ARR_FLAG(v->extra.binding, ARRAY_FLAG_CONTEXT_VARLIST));
    return v->extra.binding;
}


// The subfeed is either the varlist of the frame of another varargs that is
// being chained at the moment, or the `array1` of another varargs.  To
// be visible for all instances of the same vararg, it can't live in the
// payload bits--so it's in the `eval` slot of a frame or the misc slot
// of the array1.
//
inline static REBARR **SUBFEED_ADDR_OF_FEED(REBARR *a) {
    return GET_ARR_FLAG(a, ARRAY_FLAG_CONTEXT_VARLIST)
        ? &CTX_FRAME(AS_CONTEXT(a))->cell.subfeed
        : &ARR_SERIES(a)->link.subfeed;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  HANDLE! (`struct Reb_Handle`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Type for holding an arbitrary code or data pointer inside of a Rebol data
// value.  What kind of function or data is not known to the garbage collector,
// so it ignores it.
//
// !!! Review usages of this type where they occur.
//

#define VAL_HANDLE_CODE(v) \
    ((v)->payload.handle.code)

#define VAL_HANDLE_DATA(v) \
    ((v)->payload.handle.data)

#define VAL_HANDLE_NUMBER(v) \
    cast(REBUPT, (v)->payload.handle.data)

inline static void SET_HANDLE_CODE(RELVAL *v, CFUNC *code) {
    VAL_RESET_HEADER(v, REB_HANDLE);
    VAL_HANDLE_CODE(v) = code;
}

inline static void SET_HANDLE_DATA(RELVAL *v, void *data) {
    VAL_RESET_HEADER(v, REB_HANDLE);
    VAL_HANDLE_DATA(v) = data;
}

inline static void SET_HANDLE_NUMBER(RELVAL *v, REBUPT number) {
    VAL_RESET_HEADER(v, REB_HANDLE);
    VAL_HANDLE_DATA(v) = cast(void*, number);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  LIBRARY! (`struct Reb_Library`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A library represents a loaded .DLL or .so file.  This contains native
// code, which can be executed either through the "COMMAND" method (Rebol
// extensions) or the FFI interface.
//
// !!! The COMMAND method of extension is being deprecated by Ren-C, instead
// leaning on the idea of writing new natives using the same API that
// the system uses internally.
//

inline static void *LIB_FD(REBLIB *l) {
    return ARR_SERIES(l)->misc.fd; // file descriptor
}

inline static REBOOL IS_LIB_CLOSED(const REBLIB *l) {
    return LOGICAL(ARR_SERIES(l)->misc.fd == NULL);
}

inline static REBCTX *VAL_LIBRARY_META(const RELVAL *v) {
    return ARR_SERIES(v->payload.library.singular)->link.meta;
}

inline static REBLIB *VAL_LIBRARY(const RELVAL *v) {
    return v->payload.library.singular;
}

inline static void *VAL_LIBRARY_FD(const RELVAL *v) {
    return LIB_FD(VAL_LIBRARY(v));
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


// In the C++ build, defining this overload that takes a REBVAL* instead of
// a RELVAL*, and then not defining it...will tell you that you do not need
// to use COPY_VALUE.  Just say `*dest = *src` if your source is a REBVAL!
//
#ifdef __cplusplus
void COPY_VALUE_Debug(REBVAL *dest, const REBVAL *src, REBCTX *specifier);
#endif

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
    VAL_RESET_HEADER(v, REB_GOB);
    VAL_GOB(v) = g;
    VAL_GOB_INDEX(v) = 0;
}

#define Panic_Value(v) \
    PANIC_VALUE(v)


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

inline static void DROP_GUARD_VALUE(RELVAL *v) {
    assert(v == *SER_LAST(RELVAL*, GC_Value_Guard));
    GC_Value_Guard->content.dynamic.len--;
}
