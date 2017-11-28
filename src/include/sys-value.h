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
//  DEBUG PROBE <== **THIS IS VERY USEFUL**
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


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACKING PAYLOAD <== **THIS IS VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In the debug build, "Trash" cells (NODE_FLAG_FREE) can use their payload to
// store where and when they were initialized.  This also applies to some
// datatypes like BLANK!, BAR!, LOGIC!, or void--since they only use their
// header bits, they can also use the payload for this in the debug build.
//
// (Note: The release build does not canonize unused bits of payloads, so
// they are left as random data in that case.)
//
// View this information in the debugging watchlist under the `track` union
// member of a value's payload.  It is also reported by panic().
//

#if !defined NDEBUG
    inline static void Set_Track_Payload_Debug(
        RELVAL *v, const char *file, int line
    ){
        v->payload.track.file = file;
        v->payload.track.line = line;
        v->extra.tick = TG_Tick;
    }
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
    // To help speed up VAL_TYPE_Debug, we push the blank flag into the
    // farthest right value bit...on a 32-bit architecture, this is going
    // to be the 24th flag...pushing up against the rightmost 8-bits used
    // for the value's type.  The odds are on any given value this flag will
    // not be set, but we still don't completely reserve it.
    //
    #define BLANK_FLAG_UNREADABLE_DEBUG \
        FLAGIT_LEFT(23)

    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v, const char *file, int line
    ){
        // VAL_TYPE is called *a lot*, and this makes it a great place to do
        // sanity checks in the debug build.  But a debug build will not
        // inline this function, and makes *no* optimizations.  Using no
        // stack space e.g. no locals) is ideal, and this front-loaded test
        // keeps naive branching implementations from taking > 20% of runtime.
        //
        if (
            (v->header.bits & (
                NODE_FLAG_END
                | NODE_FLAG_CELL
                | NODE_FLAG_FREE
                | BLANK_FLAG_UNREADABLE_DEBUG
            )) == NODE_FLAG_CELL
        ){
            return VAL_TYPE_RAW(v);
        }

        if (v->header.bits & NODE_FLAG_END) {
            printf("VAL_TYPE() called on END marker\n");
            panic_at (v, file, line);
        }

        if (NOT(v->header.bits & NODE_FLAG_CELL)) {
            printf("VAL_TYPE() called on non-cell\n");
            panic_at (v, file, line);
        }

        if (v->header.bits & NODE_FLAG_FREE) {
            printf("VAL_TYPE() called on trash cell\n");
            panic_at (v, file, line);
        }

        assert(v->header.bits & BLANK_FLAG_UNREADABLE_DEBUG);

        if (VAL_TYPE_RAW(v) == REB_BLANK) {
            printf("VAL_TYPE() called on unreadable BLANK!\n");
            panic_at (v, file, line);
        }

        // Hopefully rare case... some other type that is using the same
        // 24th-from-the-left bit as BLANK_FLAG_UNREADABLE_DEBUG, and it's
        // set, but doesn't mean the type is actually unreadable.  Avoid
        // making this a common case, as it slows the debug build.
        // 
        return VAL_TYPE_RAW(v);
    }

    #define VAL_TYPE(v) \
        VAL_TYPE_Debug((v), __FILE__, __LINE__)
#endif


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
    #define SET_VAL_FLAGS(v,f) \
        (v)->header.bits |= (f)

    #if defined(__cplusplus) && __cplusplus >= 201103L
        //
        // In the C++ release build we sanity check that only one bit is set.
        // The assert is done at compile-time, you must use a constant flag.
        // If you need dynamic flag checking, use GET_VAL_FLAGS even for one.
        //
        // Note this is not included as a runtime assert because it is costly,
        // and it's not included in the debug build because the flags are
        // "contaminated" with additional data that's hard to mask out at
        // compile-time due to the weirdness of CLEAR_8_RIGHT_BITS.  This
        // pattern does not catch bad flag checks in asserts.  Review.

        template <REBUPT f>
        inline static void SET_VAL_FLAG_cplusplus(RELVAL *v) {
            static_assert(
                f && (f & (f - 1)) == 0, // only one bit is set
                "use SET_VAL_FLAGS() to set multiple bits"
            );
            v->header.bits |= f;
        }
        #define SET_VAL_FLAG(v,f) \
            SET_VAL_FLAG_cplusplus<f>(v)
        
        template <REBUPT f>
        inline static REBOOL GET_VAL_FLAG_cplusplus(const RELVAL *v) {
            static_assert(
                f && (f & (f - 1)) == 0, // only one bit is set
                "use ANY_VAL_FLAGS() or ALL_VAL_FLAGS() to test multiple bits"
            );
            return LOGICAL(v->header.bits & f);
        }
        #define GET_VAL_FLAG(v,f) \
            GET_VAL_FLAG_cplusplus<f>(v)
    #else
        #define SET_VAL_FLAG(v,f) \
            SET_VAL_FLAGS((v), (f))

        #define GET_VAL_FLAG(v, f) \
            LOGICAL((v)->header.bits & (f))
    #endif

    #define ANY_VAL_FLAGS(v,f) \
        LOGICAL(((v)->header.bits & (f)) != 0)

    #define ALL_VAL_FLAGS(v,f) \
        LOGICAL(((v)->header.bits & (f)) == (f))

    #define CLEAR_VAL_FLAGS(v,f) \
        ((v)->header.bits &= ~(f))

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
    #define CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(flags) \
        enum Reb_Kind category = (enum Reb_Kind)(RIGHT_8_BITS(flags)); \
        assert(kind <= REB_MAX); /* REB_0 is okay here */ \
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

    inline static void SET_VAL_FLAGS(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        v->header.bits |= f;
    }

    inline static void SET_VAL_FLAG(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        v->header.bits |= f;
    }

    inline static REBOOL GET_VAL_FLAG(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        return LOGICAL(v->header.bits & f);
    }

    inline static REBOOL ANY_VAL_FLAGS(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        return LOGICAL((v->header.bits & f) != 0);
    }

    inline static REBOOL ALL_VAL_FLAGS(const RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        return LOGICAL((v->header.bits & f) == f);
    }

    inline static void CLEAR_VAL_FLAGS(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        v->header.bits &= ~f;
    }

    inline static void CLEAR_VAL_FLAG(RELVAL *v, REBUPT f) {
        enum Reb_Kind kind = VAL_TYPE_RAW(v);
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(f);
        assert(f && (f & (f - 1)) == 0); // checks that only one bit is set
        v->header.bits &= ~f;
    }
#endif

#define NOT_VAL_FLAG(v,f) \
    NOT(GET_VAL_FLAG((v), (f)))


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL WRITABILITY AND SETUP
//
//=////////////////////////////////////////////////////////////////////////=//
//
// VAL_RESET_HEADER clears out the header of *most* bits, setting it to a
// new type.  The type takes up the full "rightmost" byte of the header,
// despite the fact it only needs 6 bits.  However, the performance advantage
// of not needing to mask to do VAL_TYPE() is worth it...also there may be a
// use for 256 types (although type bitsets are only 64-bits at the moment)
//
// The value is expected to already be "pre-formatted" with the NODE_FLAG_CELL
// bit, so that is left as-is.  It is also expected that VALUE_FLAG_STACK has
// been set if the value is stack-based (e.g. on the C stack or in a frame),
// so that is left as-is also.
//
// Asserting writiablity helps avoid very bad catastrophies that might ensue
// if "implicit end markers" could be overwritten.  These are the ENDs that
// are actually other bitflags doing double duty inside a data structure, and
// there is no REBVAL storage backing the position.
//
// (A fringe benefit is catching writes to other unanticipated locations.)
//

#if !defined(NDEBUG)
    #define REB_MAX_PLUS_ONE_TRASH \
        (REB_MAX + 1) // used in the debug build to help identify trash nodes
#endif

#define HEADERIZE_KIND(kind) \
    FLAGBYTE_RIGHT(kind)

inline static void VAL_RESET_HEADER_common( // don't call directly
    RELVAL *v,
    enum Reb_Kind kind,
    REBUPT extra_flags
) {
    v->header.bits &= CELL_MASK_RESET;
    v->header.bits |= HEADERIZE_KIND(kind) | extra_flags;
}

#ifdef NDEBUG
    #define ASSERT_CELL_WRITABLE(c,file,line) \
        NOOP

    #define VAL_RESET_HEADER_EXTRA(v,kind,extra) \
        VAL_RESET_HEADER_common((v), (kind), (extra))

    #define VAL_RESET(v,kind,extra) \
        VAL_RESET_HEADER_EXTRA((v), (kind), (extra))

    #define Prep_Non_Stack_Cell(c) \
        (c)->header.bits = NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL

    #define Prep_Stack_Cell(c) \
        (c)->header.bits = (NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL \
            | VALUE_FLAG_STACK)
#else
    inline static void Assert_Cell_Writable(
        const RELVAL *v,
        const char *file,
        int line
    ){
        // REBVALs should not be written at addresses that do not match the
        // alignment of the processor.  Checks modulo the size of an unsigned
        // integer the same size as a platform pointer (REBUPT => uintptr_t)
        //
        // This is pretty important, but usually only triggers on 0xDECAFBAD
        //
        if (cast(REBUPT, v) % sizeof(REBUPT) != 0) {
            printf(
                "Cell address %p not aligned to %d bytes\n",
                cast(const void*, v),
                cast(int, sizeof(REBUPT))
            );
            panic_at (v, file, line);
        }

        if (NOT(v->header.bits & NODE_FLAG_CELL)) {
            printf("Non-cell passed to writing routine\n");
            panic_at (v, file, line);
        }

        if (v->header.bits & CELL_FLAG_PROTECTED) {
            printf("Protected cell passed to writing routine\n");
            panic_at (v, file, line);
        }
    }

    #define ASSERT_CELL_WRITABLE(v,file,line) \
        Assert_Cell_Writable((v), (file), (line))

    inline static void VAL_RESET_HEADER_EXTRA_Debug(
        RELVAL *v,
        enum Reb_Kind kind,
        REBUPT extra,
        const char *file,
        int line
    ){
        ASSERT_CELL_WRITABLE(v, file, line);

        // The debug build puts some extra type information onto flags
        // which needs to be cleared out.  (e.g. FUNC_FLAG_XXX has the bit
        // pattern for REB_FUNCTION inside of it, to help make sure that flag
        // doesn't get used with things that aren't words).
        //
        CHECK_VALUE_FLAGS_EVIL_MACRO_DEBUG(extra);
        
        VAL_RESET_HEADER_common(v, kind, extra);
    }

    #define VAL_RESET_HEADER_EXTRA(v,kind,extra) \
        VAL_RESET_HEADER_EXTRA_Debug((v), (kind), (extra), __FILE__, __LINE__)

    // VAL_RESET is a variant of VAL_RESET_HEADER_EXTRA that actually
    // overwrites the payload with tracking information.  It should not be
    // used if the intent is to preserve the payload and extra, and is
    // wasteful if you're just going to overwrite them immediately afterward.
    //
    inline static void VAL_RESET_Debug(
        RELVAL *v,
        enum Reb_Kind kind,
        REBUPT extra,
        const char *file,
        int line
    ){
        VAL_RESET_HEADER_EXTRA_Debug(v, kind, extra, file, line);
        Set_Track_Payload_Debug(v, file, line);
    }

    #define VAL_RESET(v,kind,extra) \
        VAL_RESET_Debug((v), (kind), (extra), __FILE__, __LINE__)

    inline static void Prep_Non_Stack_Cell_Debug(
        struct Reb_Cell *c, const char *file, int line
    ){
        c->header.bits =
            NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
            | HEADERIZE_KIND(REB_MAX_PLUS_ONE_TRASH);
        Set_Track_Payload_Debug(cast(RELVAL*, c), file, line);
    }

    #define Prep_Non_Stack_Cell(c) \
        Prep_Non_Stack_Cell_Debug((c), __FILE__, __LINE__)

    inline static void Prep_Stack_Cell_Debug(
        struct Reb_Cell *c, const char *file, int line
    ){
        c->header.bits = NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
            | HEADERIZE_KIND(REB_MAX_PLUS_ONE_TRASH) | VALUE_FLAG_STACK;
        Set_Track_Payload_Debug(cast(RELVAL*, c), file, line);
    }

    #define Prep_Stack_Cell(c) \
        Prep_Stack_Cell_Debug((c), __FILE__, __LINE__)
#endif

#define VAL_RESET_HEADER(v,t) \
    VAL_RESET_HEADER_EXTRA((v), (t), 0)

inline static void VAL_SET_TYPE_BITS(RELVAL *v, enum Reb_Kind kind) {
    //
    // Note: Only use if you are sure the new type payload is in sync with
    // the type and bits (e.g. changing ANY-WORD! to another ANY-WORD!).
    // Otherwise the value-specific flags might be misinterpreted.
    //
    ASSERT_CELL_WRITABLE(v, __FILE__, __LINE__);
    CLEAR_8_RIGHT_BITS(v->header.bits);
    v->header.bits |= HEADERIZE_KIND(kind);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRASH CELLS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Trash is a cell (marked by NODE_FLAG_CELL) with NODE_FLAG_FREE set.  To
// prevent it from being inspected while it's in an invalid state, VAL_TYPE
// used on a trash cell will assert in the debug build.
//
// The garbage collector is not tolerant of trash.
//

#ifdef NDEBUG
    #define TRASH_CELL_IF_DEBUG(v) \
        NOOP
#else
    inline static void Set_Trash_Debug(
        RELVAL *v,
        const char *file,
        int line
    ){
        ASSERT_CELL_WRITABLE(v, file, line);

        v->header.bits &= CELL_MASK_RESET;
        v->header.bits |= NODE_FLAG_FREE
          | HEADERIZE_KIND(REB_MAX_PLUS_ONE_TRASH);

        Set_Track_Payload_Debug(v, file, line);
    }

    #define TRASH_CELL_IF_DEBUG(v) \
        Set_Trash_Debug((v), __FILE__, __LINE__)

    inline static REBOOL IS_TRASH_DEBUG(const RELVAL *v) {
        assert(v->header.bits & NODE_FLAG_CELL);
        if (NOT(v->header.bits & NODE_FLAG_FREE))
            return FALSE;
        assert(LEFT_8_BITS(v->header.bits) == TRASH_CELL_BYTE); // bad UTF-8
        assert(VAL_TYPE_RAW(v) == REB_MAX_PLUS_ONE_TRASH);
        return TRUE;
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  END marker (not a value type, only writes `struct Reb_Value_Flags`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Historically Rebol arrays were always one value longer than their maximum
// content, and this final slot was used for a REBVAL type called END!.
// Like a null terminator in a C string, it was possible to start from one
// point in the series and traverse to find the end marker without needing
// to look at the length (though the length in the series header is maintained
// in sync, also).
//
// Ren-C changed this so that end is not a data type, but a header bit.
// See NODE_FLAG_END for an explanation of this choice--and how it means
// a full cell's worth of size is not needed to terminate.
//
// VAL_TYPE() and many other operations will panic if they are used on an END
// cell.  Yet the special unwritable system value END is the size of a REBVAL,
// but does not carry NODE_FLAG_CELL.  Since it is a node, it can be more
// useful to return from routines that return REBVAL* than a NULL, because it
// can have its header dereferenced to check its type in a single test...
// as VAL_TYPE_OR_0() will return REB_0 for the system END marker.  (It's
// actually possible if you're certain you have a NODE_FLAG_CELL to know that
// the type of an end marker is REB_0, but one can rarely exploit that.)
//

#ifdef NDEBUG
    #define IS_END(v) \
        LOGICAL((v)->header.bits & NODE_FLAG_END)

    inline static void SET_END(RELVAL *v) {
        v->header.bits &= CELL_MASK_RESET; // leaves flags _CELL, _NODE, etc.
        v->header.bits |= NODE_FLAG_END | HEADERIZE_KIND(REB_0);
    }

    // Warning: Only use on valid non-END REBVAL -or- on global END value
    //
    #define VAL_TYPE_OR_0(v) \
        VAL_TYPE_RAW(v)
#else
    inline static REBOOL IS_END_Debug(
        const RELVAL *v,
        const char *file,
        int line
    ){
        if (v->header.bits & NODE_FLAG_FREE) {
            printf("IS_END() called on garbage\n");
            panic_at(v, file, line);
        }

        // Do a fast common case.  We check for freeness but not cellness, as
        // not all END markers are full cells.
        //
        if ((v->header.bits & (NODE_FLAG_FREE | NODE_FLAG_END)) == 0)
            return FALSE;

        if (v->header.bits & NODE_FLAG_END) {
            if (v->header.bits & NODE_FLAG_CELL)
                assert(VAL_TYPE_RAW(v) == REB_0);
            else {
                // Can't make any guarantees about what's in the type slot of
                // non-cell ENDs, they only commit a bit or two and use the
                // rest how they wish!  See Init_Endlike_Header()
            }
            return TRUE;
        }

        // Anything that's not an END called by this routine *must* be a cell
        //
        assert(v->header.bits & NODE_FLAG_CELL);
        return FALSE;
    }

#if defined(__cplusplus) && __cplusplus >= 201103L
    //
    // Disable teseting a RELVAL marked that it can't be END for endness-in
    // the C++ build.  (ordinary version takes a RELVAL.)
    //
    // !!! This could probably be tweaked to give a better message with
    // type_traits and SFINAE, these just give a linker error
    //
    REBOOL IS_END_Debug(const_RELVAL_NO_END_PTR v, const char* file, int line);
#endif

    #define IS_END(v) \
        IS_END_Debug((v), __FILE__, __LINE__)

    inline static void SET_END_Debug(RELVAL *v, const char *file, int line) {
        ASSERT_CELL_WRITABLE(v, file, line);
        v->header.bits &= CELL_MASK_RESET; // leaves NODE_FLAG_CELL, etc.
        v->header.bits |= NODE_FLAG_END | HEADERIZE_KIND(REB_0);
        Set_Track_Payload_Debug(v, file, line);
    }

    #define SET_END(v) \
        SET_END_Debug((v), __FILE__, __LINE__)

    inline static enum Reb_Kind VAL_TYPE_OR_0_Debug(
        const RELVAL *v,
        const char *file,
        int line
    ){
        if (v->header.bits & NODE_FLAG_END) {
            if (v != END) {
                printf("VAL_TYPE_OR_0 called on end that isn't -the- END");
                panic_at(v, file, line);
            }
            return VAL_TYPE_RAW(v); // asserted as REB_0 at startup for END
        }

        return VAL_TYPE_Debug(v, file, line);
    }

    // Warning: Only use on valid non-END REBVAL -or- on global END value
    //
    #define VAL_TYPE_OR_0(v) \
        VAL_TYPE_OR_0_Debug((v), __FILE__, __LINE__)
#endif

#define NOT_END(v) \
    NOT(IS_END(v))



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
// Note: For some narrow purposes, it may be useful to quickly test a value
// which is potentially void as "falsey" like BLANK! and LOGIC! false, so
// it is given VALUE_FLAG_FALSEY to facilitate a fast test for that.  But
// usual tests for truth in conditionals specifically prohibit voids.
//

#define REB_MAX_VOID \
    REB_MAX // there is no VOID! datatype, use REB_MAX

#define VOID_CELL \
    c_cast(const REBVAL*, &PG_Void_Cell[0])

#define IS_VOID(v) \
    LOGICAL(VAL_TYPE(v) == REB_MAX_VOID)

#define Init_Void(v) \
    VAL_RESET((v), REB_MAX_VOID, VALUE_FLAG_FALSEY) // see note above!

#define IS_VOID_OR_FALSEY(v) \
    GET_VAL_FLAG((v), VALUE_FLAG_FALSEY)


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

#define BAR_VALUE \
    c_cast(const REBVAL*, &PG_Bar_Value[0])

#define Init_Bar(v) \
    VAL_RESET((v), REB_BAR, 0)

#define Init_Lit_Bar(v) \
    VAL_RESET((v), REB_LIT_BAR, 0)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BLANK!
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

#define BLANK_VALUE \
    c_cast(const REBVAL*, &PG_Blank_Value[0])

#define Init_Blank(v) \
    VAL_RESET((v), REB_BLANK, VALUE_FLAG_FALSEY)

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
        VAL_RESET((v), REB_BLANK, \
            VALUE_FLAG_FALSEY | BLANK_FLAG_UNREADABLE_DEBUG)

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
    ){
        ASSERT_CELL_WRITABLE(v, file, line);

        if (NOT(v->header.bits & NODE_FLAG_FREE)) {
            VAL_RESET_HEADER_EXTRA_Debug(
                v,
                REB_BLANK,
                VALUE_FLAG_FALSEY | BLANK_FLAG_UNREADABLE_DEBUG,
                file,
                line
            );
        }
        else {
            // already trash, don't need to mess with the header
        }

        Set_Track_Payload_Debug(v, file, line);

        return cast(REBVAL*, v); // used by SINK, but not TRASH_CELL_IF_DEBUG
    }

    #define SINK(v) \
        Sink_Debug((v), __FILE__, __LINE__)

#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  LOGIC!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A logic can be either true or false.  For purposes of optimization, logical
// falsehood is indicated by one of the value option bits in the header--as
// opposed to in the value payload.  This means it can be tested quickly, and
// that a single check can test for both BLANK! and logic false.
//
// Conditional truth and falsehood allows an interpretation where a BLANK!
// is a "falsey" value as well.
//

#define FALSE_VALUE \
    c_cast(const REBVAL*, &PG_False_Value[0])

#define TRUE_VALUE \
    c_cast(const REBVAL*, &PG_True_Value[0])

#define Init_Logic(v,b) \
    VAL_RESET((v), REB_LOGIC, (b) ? 0 : VALUE_FLAG_FALSEY)

#ifdef NDEBUG
    #define IS_FALSEY(v) \
        GET_VAL_FLAG((v), VALUE_FLAG_FALSEY)
#else
    inline static REBOOL IS_FALSEY_Debug(
        const RELVAL *v, const char *file, int line
    ){
        if (IS_VOID(v)) {
            printf("Conditional true/false test on void\n");
            panic_at (v, file, line);
        }
        return GET_VAL_FLAG(v, VALUE_FLAG_FALSEY);
    }

    #define IS_FALSEY(v) \
        IS_FALSEY_Debug((v), __FILE__, __LINE__)
#endif

#define IS_TRUTHY(v) \
    NOT(IS_FALSEY(v)) // macro gets file + line # in debug build

// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`.
//
inline static REBOOL IS_CONDITIONAL_TRUE(const REBVAL *v, REBOOL only) {
    if (NOT(only) && IS_BLOCK(v)) {
        if (GET_VAL_FLAG(v, VALUE_FLAG_UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));
            
        return TRUE;
    }
    return IS_TRUTHY(v);
}

inline static REBOOL IS_CONDITIONAL_FALSE(const REBVAL *v, REBOOL only) {
    if (NOT(only) && IS_BLOCK(v)) {
        if (GET_VAL_FLAG(v, VALUE_FLAG_UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));
            
        return FALSE;
    }
    return IS_FALSEY(v);
}

inline static REBOOL VAL_LOGIC(const RELVAL *v) {
    assert(IS_LOGIC(v));
    return NOT_VAL_FLAG((v), VALUE_FLAG_FALSEY);
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
    VAL_RESET_HEADER(v, REB_INTEGER);
    v->payload.integer = i64;
    v->extra.binding = NULL; // !!! avoids compiler warnings, better way?
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
    VAL_RESET_HEADER(v, REB_DECIMAL);
    v->payload.decimal = d;
}

inline static void Init_Percent(RELVAL *v, REBDEC d) {
    VAL_RESET_HEADER(v, REB_PERCENT);
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
    VAL_RESET_HEADER(v, REB_MONEY);
    v->extra.m0 = amount.m0;
    v->payload.money.m1 = amount.m1;
    v->payload.money.m2 = amount.m2;
    v->payload.money.s = amount.s;
    v->payload.money.e = amount.e;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  REFERENCE!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// References are an internal type, used transiently to communicate a cell
// location via a cell.  They are not robust enough for userspace, so they
// use the internal REB_0_REFERENCE type and currently only appear in the
// path dispatch code.
//

#define REB_0_REFERENCE \
    REB_0

inline static void Init_Reference(
    RELVAL *out,
    RELVAL *cell,
    REBSPC *specifier
){
    VAL_RESET_HEADER(out, REB_0_REFERENCE);
    out->payload.reference.cell = cell;
    out->extra.binding = cast(REBNOD*, specifier);
}

inline static RELVAL *VAL_REFERENCE(const RELVAL *v) {
    assert(VAL_TYPE(v) == REB_0_REFERENCE);
    return v->payload.reference.cell; // Use VAL_SPECIFIC() to get specifier
}


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
    (MISC(s).area.wide)

#define IMG_HIGH(s) \
    (MISC(s).area.high)

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
// these.  With GOB! being moved to an extension, it is not likely that it
// would hook the memory pools directly.
//

#if defined(NDEBUG) || !defined(__cplusplus)
    #define VAL_GOB(v) \
        (v)->payload.gob.gob

    #define VAL_GOB_INDEX(v) \
        (v)->payload.gob.index
#else
    inline static REBGOB* const &VAL_GOB(const RELVAL *v) {
        assert(IS_GOB(v));
        return v->payload.gob.gob;
    }

    inline static REBCNT const &VAL_GOB_INDEX(const RELVAL *v) {
        assert(IS_GOB(v));
        return v->payload.gob.index;
    }

    inline static REBGOB* &VAL_GOB(RELVAL *v) {
        assert(IS_GOB(v));
        return v->payload.gob.gob;
    }

    inline static REBCNT &VAL_GOB_INDEX(RELVAL *v) {
        assert(IS_GOB(v));
        return v->payload.gob.index;
    }
#endif

inline static void SET_GOB(RELVAL *v, REBGOB *g) {
    VAL_RESET_HEADER(v, REB_GOB);
    VAL_GOB(v) = g;
    VAL_GOB_INDEX(v) = 0;
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  RELATIVE AND SPECIFIC VALUES
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a REBNOD which constitutes their notion of "binding".
//
// At time of writing, this can be either a pointer to EMPTY_ARRAY (which
// indicates UNBOUND), or to a function's paramlist (which indicates a
// relative binding), or to a context's varlist (which indicates a specific
// binding.)
//
// The ordering of %types.r is chosen specially so that all bindable types
// are at lower values than the unbindable types.
//

// An ANY-WORD! is relative if it refers to a local or argument of a function,
// and has its bits resident in the deep copy of that function's body.
//
// An ANY-ARRAY! in the deep copy of a function body must be relative also to
// the same function if it contains any instances of such relative words.
//
inline static REBOOL IS_RELATIVE(const RELVAL *v) {
    if (Not_Bindable(v)) // uses VAL_TYPE_RAW(), don't check unreadable blank
        return FALSE;
    return LOGICAL(v->extra.binding->header.bits & ARRAY_FLAG_PARAMLIST);
}

#if defined(__cplusplus) && __cplusplus >= 201103L
    //
    // Take special advantage of the fact that C++ can help catch when we are
    // trying to see if a REBVAL is specific or relative (it will always
    // be specific, so the call is likely in error).  In the C build, they
    // are the same type so there will be no error.
    //
    REBOOL IS_RELATIVE(const REBVAL *v);
#endif

#define IS_SPECIFIC(v) \
    NOT(IS_RELATIVE(v))

inline static REBFUN *VAL_RELATIVE(const RELVAL *v) {
    assert(IS_RELATIVE(v));
    return cast(REBFUN*, v->extra.binding);
}

inline static REBCTX *VAL_SPECIFIC_COMMON(const RELVAL *v) {
    assert(IS_SPECIFIC(v));
    return cast(REBCTX*, v->extra.binding);
}

#ifdef NDEBUG
    #define VAL_SPECIFIC(v) \
        VAL_SPECIFIC_COMMON(v)
#else
    #define VAL_SPECIFIC(v) \
        VAL_SPECIFIC_Debug(v)
#endif

// When you have a RELVAL* (e.g. from a REBARR) that you "know" to be specific,
// the KNOWN macro can be used for that.  Checks to make sure in debug build.
//
// Use for: "invalid conversion from 'Reb_Value*' to 'Reb_Specific_Value*'"

inline static const REBVAL *const_KNOWN(const RELVAL *value) {
    assert(IS_END(value) || IS_SPECIFIC(value));
    return cast(const REBVAL*, value); // we asserted it's actually specific
}

inline static REBVAL *KNOWN(RELVAL *value) {
    //
    // Trash is tolerated for KNOWN() because it is often used to indicate
    // a cell which may be targeted for writing or reading, which when read
    // can't be relative.  This only applies to mutable slots, so it's *not*
    // tolerated in the const_KNOWN() case.
    //
    // Because this is called so often, though, we speed it up and don't do
    // a full trash-validity or end-validity check.  Just check the bits.
    //
    assert(
        (value->header.bits & (NODE_FLAG_END | NODE_FLAG_FREE)) != 0
        || IS_SPECIFIC(value)
    );
    return cast(REBVAL*, value); // we asserted it's actually specific
}

inline static const RELVAL *const_REL(const REBVAL *v) {
    return cast(const RELVAL*, v); // cast w/input restricted to REBVAL
}

inline static RELVAL *REL(REBVAL *v) {
    return cast(RELVAL*, v); // cast w/input restricted to REBVAL
}

// Originally a NULL pointer was used to indicate when a value was specified
// by its nature.  However, by using something that is a valid REBNOD there
// is an advantage of being able to merely test it for NODE_FLAG_MANAGED
// in Move_Value() without special-casing the NULL handling.
//
#define SPECIFIED \
    cast(REBSPC*, PG_Empty_Array)

// A WORD! cannot be merely "specified" as its binding, because that is not
// meaningful.  But in order to avoid having a separate flag, the same idea
// is used with an EMPTY_ARRAY.
//
#define UNBOUND \
   NOD(PG_Empty_Array)

#ifdef NDEBUG
    #define ASSERT_NO_RELATIVE(array,deep) NOOP
#else
    #define ASSERT_NO_RELATIVE(array,deep) \
        Assert_No_Relative((array),(deep))
#endif

inline static REBNOD *VAL_BINDING(const RELVAL *v) {
    assert(Is_Bindable(v));
    return v->extra.binding;
}

inline static void INIT_BINDING(RELVAL *v, void *p) {
    //
    // This can be used on a partially initialized value, hence GET_VAL_FLAG
    // is not appropriate to apply, since it assumes a fully formed value.
    //
    assert(Is_Bindable(v));

    REBNOD *binding = NOD(p);

#if !defined(NDEBUG)
    if (binding->header.bits & NODE_FLAG_CELL) {
        //
        // !!! This is a good assert but it is limiting the applications of
        // trying to do evaluations into API cells.  Review.
        //
        /* assert(v->header.bits & VALUE_FLAG_STACK); */
    }
    else if (NOT(v->header.bits & VALUE_FLAG_STACK)) {
        assert(binding->header.bits & NODE_FLAG_MANAGED);
    }
#endif

    // !!! might be nice to do more checks here, may have to break this out
    // into a _Debug function in %c-value.c

    v->extra.binding = binding;
}

inline static void Move_Value_Header(RELVAL *out, const RELVAL *v)
{
    assert(out != v); // usually a sign of a mistake; not worth supporting
    assert(
        (v->header.bits & (NODE_FLAG_CELL | NODE_FLAG_NODE))
        && NOT(v->header.bits & (NODE_FLAG_END | NODE_FLAG_FREE))
    );

    ASSERT_CELL_WRITABLE(out, __FILE__, __LINE__);

    out->header.bits &= CELL_MASK_RESET;
    out->header.bits |= v->header.bits & CELL_MASK_COPY;
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
    Move_Value_Header(out, v);

    out->payload = v->payload; // payloads cannot hold references to stackvars

    if (
        NOT(v->header.bits & VALUE_FLAG_STACK)
        || NOT(Is_Bindable(v))
        || (v->extra.binding->header.bits & NODE_FLAG_MANAGED)
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
        //    
    #if !defined(NDEBUG)
        if (Is_Bindable(v)) {
            if (NOT(v->header.bits & VALUE_FLAG_STACK))
                assert(NOT(v->extra.binding->header.bits & NODE_FLAG_CELL));
        }
    #endif
        out->extra = v->extra;
        return KNOWN(out);
    }

    // If we get here, the source value is on the stack and has a non-reified
    // binding of some kind.  Check to see if the target stack level will
    // outlive the stack level of the non-reified material in the binding. 

    assert(v->extra.binding->header.bits & NODE_FLAG_CELL);
    REBFRM *f = cast(REBFRM*, v->extra.binding);

    REBCNT bind_depth = 1; // !!! need to determine v's binding stack level
    REBCNT out_depth;
    if (NOT(out->header.bits & VALUE_FLAG_STACK))
        out_depth = 0;
    else
        out_depth = 1; // !!! need to determine out's stack level

    const REBOOL smarts_enabled = FALSE; 
    if (smarts_enabled && out_depth >= bind_depth) {
        //
        // The non-reified binding will outlive the output slot, so there is
        // no reason to reify it.
        //
        INIT_BINDING(out, f);
        return KNOWN(out);
    }

    // This is the expensive case, we know the binding as-is will not outlive
    // the target slot.  A reification is necessary.

    // !!! For now we very conservatively reify on all cases, and reach
    // beneath the const of the source in order to update its binding too.
    
    REBCTX *reified = Context_For_Frame_May_Reify_Managed(f);
    INIT_BINDING(out, reified);
    INIT_BINDING(m_cast(REBVAL*, v), reified);

    return KNOWN(out);
}


// Generally speaking, you cannot take a RELVAL from one cell and copy it
// blindly into another...it needs to be Derelativize()'d.  This routine is
// for the rare cases where it's legal, e.g. shuffling a cell from one place
// in an array to another cell in the same array.
//
inline static void Blit_Cell(RELVAL *out, const RELVAL *v)
{
    assert(out != v); // usually a sign of a mistake; not worth supporting
    assert(
        (v->header.bits & (NODE_FLAG_CELL | NODE_FLAG_NODE))
        && NOT(v->header.bits & (NODE_FLAG_END | NODE_FLAG_FREE))
    );

    ASSERT_CELL_WRITABLE(out, __FILE__, __LINE__);

    // Examine just the cell's preparation bits.  Are they identical?  If so,
    // we are not losing any information by blindly copying the header in
    // the release build.
    //
    assert(
        (out->header.bits & CELL_MASK_RESET)
        == (v->header.bits & CELL_MASK_RESET)
    );

    out->header = v->header;
    out->payload = v->payload;
    out->extra = v->extra;
}


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
// Note: It sets NODE_FLAG_FREE, so this is a "trash" cell by default.
//
#define DECLARE_LOCAL(name) \
    REBSER name##_pair; \
    Prep_Stack_Cell(cast(REBVAL*, &name##_pair)); /* tbd: FS_TOP FRAME! */ \
    REBVAL * const name = cast(REBVAL*, &name##_pair) + 1; \
    Prep_Stack_Cell(name)
