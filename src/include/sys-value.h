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

#define Panic_Value(v) \
    PANIC_VALUE(v)

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


#define FLAGIT_KIND(t) \
    (cast(REBU64, 1) << (t)) // makes a 64-bit bitflag

// While inline vs. macro doesn't usually matter much, debug builds won't
// inline this, and it's called *ALL* the time.  Since it doesn't repeat its
// argument, it's not worth it to make it a function for slowdown caused.
// Also, don't bother checking using the `cast()` template in C++.
//
#define VAL_TYPE_RAW(v) \
    ((enum Reb_Kind)((v)->header.bits >> HEADER_TYPE_SHIFT))

#ifdef NDEBUG
    #define VAL_TYPE(v) \
        VAL_TYPE_RAW(v)
#else
    enum {
        VOID_FLAG_NOT_TRASH = (1 << TYPE_SPECIFIC_BIT),
        VOID_FLAG_SAFE_TRASH = (2 << TYPE_SPECIFIC_BIT)
    };

    inline static REBOOL IS_TRASH_DEBUG(const RELVAL *v) {
        // note this is directly inlined into VAL_TYPE_Debug() below
        return LOGICAL(
            VAL_TYPE_RAW(v) == REB_MAX_VOID
            && NOT(v->header.bits & VOID_FLAG_NOT_TRASH)
        );
    }

    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        if (
            (v->header.bits & CELL_MASK)
            && NOT(IS_END_MACRO(v)) // IS_END redundantly checks trash
            && NOT(
                kind == REB_MAX_VOID
                && NOT(v->header.bits & VOID_FLAG_NOT_TRASH)
            )
            // ^-- *so* frequent, debug builds hand-inline IS_TRASH_DEBUG()
        ){
            return kind;
        }

        assert(v->header.bits & CELL_MASK);
        printf("END marker or garbage/trash in VAL_TYPE()\n");
        fflush(stdout);
        Panic_Value_Debug(v, file, line);
    }

    #define VAL_TYPE(v) \
        VAL_TYPE_Debug((v), __FILE__, __LINE__)
#endif

inline static void VAL_SET_TYPE_BITS(RELVAL *v, enum Reb_Kind kind) {
    //
    // Note: Only use if you are sure the new type payload is in sync with
    // the type and bits (e.g. changing ANY-WORD! to another ANY-WORD!).
    // Otherwise the value-specific flags might be misinterpreted.
    //
    // Use VAL_RESET_HEADER() to set the type AND initialize the flags to 0.
    //
    assert(!IS_TRASH_DEBUG(v));
    (v)->header.bits &= ~HEADER_TYPE_MASK;
    (v)->header.bits |= TYPE_SHIFT_LEFT_FOR_HEADER(kind);
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
    // For safety in the debug build, all the type-specific flags include a
    // type (or type representing a category) as part of the flag.  This type
    // is checked first, and then masked out to use the single-bit-flag value
    // which is intended.
    //
    // But flag testing routines are called *a lot*, and debug builds do not
    // inline functions.  So it's worth doing a sketchy macro so this somewhat
    // borderline assert doesn't wind up taking up 20% of the debug's runtime.
    //
    #define CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG \
        REBUPT category = f >> HEADER_TYPE_SHIFT; \
        if (category != REB_0) { \
            enum Reb_Kind kind = VAL_TYPE(v); \
            if (kind != category) { \
                if (category == REB_WORD) \
                    assert(ANY_WORD_KIND(kind)); \
                else if (category == REB_OBJECT) \
                    assert(ANY_CONTEXT_KIND(kind)); \
                else \
                    assert(FALSE); \
            } \
            f &= ~HEADER_TYPE_MASK; \
        } \


    inline static void SET_VAL_FLAGS(RELVAL *v, REBUPT f) {
        CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG;
        v->header.bits |= f;
    }

    inline static void SET_VAL_FLAG(RELVAL *v, REBUPT f) {
        CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG;
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        v->header.bits |= f;
    }

    inline static REBOOL GET_VAL_FLAG(const RELVAL *v, REBUPT f) {
        CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG;
        return LOGICAL(v->header.bits & f);
    }

    inline static void CLEAR_VAL_FLAGS(RELVAL *v, REBUPT f) {
        CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG;
        v->header.bits &= ~f;
    }

    inline static void CLEAR_VAL_FLAG(RELVAL *v, REBUPT f) {
        CHECK_VALUE_FLAG_EVIL_MACRO_DEBUG;
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        v->header.bits &= ~f;
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
// INIT_CELL_IF_DEBUG provides additional "pre-formatting" of a cell.  In C
// debug builds, this helps by making reads of VAL_TYPE assert to say that
// it hasn't been set to an actual value yet.  In C++ builds, it goes
// further--because no writes may be done to cells that haven't been INITed.
//
// (The write protection is not checked in the C build, due to the clutter
// that would be necessary on every `REBVAL value;` declaration to call
// INIT_CELL_IF_DEBUG().  A constructor does this in C++.  It's considered
// good enough at the present time, though this may change if the GC needs
// to explicitly know where values on the stack are...which would mean this
// would not be a debug-build-only concept.)
//

#define VAL_RESET_HEADER_COMMON(v,kind) \
    ((v)->header.bits = TYPE_SHIFT_LEFT_FOR_HEADER(kind) \
        | NOT_END_MASK | CELL_MASK)

#ifdef NDEBUG
    #define ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v) \
        NOOP

    #define MARK_CELL_WRITABLE_IF_CPP_DEBUG(v) \
        NOOP

    #define MARK_CELL_UNWRITABLE_IF_CPP_DEBUG(v) \
        NOOP

    #define VAL_RESET_HEADER(v,t) \
        VAL_RESET_HEADER_COMMON((v), (t))

    #define INIT_CELL_IF_DEBUG(v) \
        NOOP
#else
    #ifdef __cplusplus
        #define ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v,file,line) \
            Assert_Cell_Writable((v), (file), (line))

        // just adds bit
        #define MARK_CELL_WRITABLE_IF_CPP_DEBUG(v) \
            ((v)->header.bits |= VALUE_FLAG_WRITABLE_CPP_DEBUG)

        #define MARK_CELL_UNWRITABLE_IF_CPP_DEBUG(v) \
            ((v)->header.bits &= ~cast(REBUPT, VALUE_FLAG_WRITABLE_CPP_DEBUG))
    #else
        #define ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v,file,line) \
            NOOP

        #define MARK_CELL_WRITABLE_IF_CPP_DEBUG(v) \
            NOOP

        #define MARK_CELL_UNWRITABLE_IF_CPP_DEBUG(v) \
            NOOP
    #endif

    inline static void VAL_RESET_HEADER_Debug(
        RELVAL *v, enum Reb_Kind kind, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v, file, line);
        VAL_RESET_HEADER_COMMON(v, kind);
        MARK_CELL_WRITABLE_IF_CPP_DEBUG(v);
    }

    #define VAL_RESET_HEADER(v,k) \
        VAL_RESET_HEADER_Debug((v), (k), __FILE__, __LINE__)

    inline static void INIT_CELL_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER_COMMON(v, REB_MAX_VOID); // no VOID_FLAG_NOT_TRASH
        Set_Track_Payload_Debug(v, file, line);
        MARK_CELL_WRITABLE_IF_CPP_DEBUG(v);
    }

    #define INIT_CELL_IF_DEBUG(v) \
        INIT_CELL_Debug((v), __FILE__, __LINE__)
#endif


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
    { return LOGICAL(VAL_TYPE(v) == REB_MAX_VOID); }

#ifdef NDEBUG
    inline static void SET_VOID(RELVAL *v)
        { VAL_RESET_HEADER(v, REB_MAX_VOID); }

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
        VAL_RESET_HEADER(v, REB_MAX_VOID);
        SET_VAL_FLAG(v, VOID_FLAG_NOT_TRASH);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void Set_Trash_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_MAX_VOID); // we don't set VOID_FLAG_NOT_TRASH
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void Set_Trash_Safe_Debug(
        RELVAL *v, const char *file, int line
    ){
        VAL_RESET_HEADER(v, REB_MAX_VOID);
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
    v->header.bits = TYPE_SHIFT_LEFT_FOR_HEADER(REB_BLANK) \
        | VALUE_FLAG_FALSE | NOT_END_MASK | CELL_MASK;
}

#ifdef NDEBUG
    #define SET_BLANK(v) \
        SET_BLANK_COMMON(v)
#else
    inline static void SET_BLANK_Debug(
        RELVAL *v, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v, file, line);
        SET_BLANK_COMMON(v);
        MARK_CELL_WRITABLE_IF_CPP_DEBUG(v);
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
    v->header.bits = TYPE_SHIFT_LEFT_FOR_HEADER(REB_LOGIC) \
        | NOT_END_MASK | CELL_MASK;
}

inline static void SET_FALSE_COMMON(RELVAL *v) {
    v->header.bits = TYPE_SHIFT_LEFT_FOR_HEADER(REB_LOGIC) \
        | NOT_END_MASK | CELL_MASK | VALUE_FLAG_FALSE;
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
        ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v, file, line);
        SET_TRUE_COMMON(v);
        MARK_CELL_WRITABLE_IF_CPP_DEBUG(v);
        Set_Track_Payload_Debug(v, file, line);
    }

    inline static void SET_FALSE_Debug(
        RELVAL *v, const char *file, int line
    ){
        ASSERT_CELL_WRITABLE_IF_CPP_DEBUG(v, file, line);
        SET_FALSE_COMMON(v);
        MARK_CELL_WRITABLE_IF_CPP_DEBUG(v);
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

#define VAL_TYPE_SPEC(v) \
    ((v)->payload.datatype.spec)

#define IS_KIND_SYM(s) \
    ((s) < REB_MAX)

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
    VAL_RESET_HEADER(v, REB_TUPLE);
    memcpy(VAL_TUPLE_DATA(v), data, sizeof(VAL_TUPLE_DATA(v)));
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
