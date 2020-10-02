//
//  File: %sys-value.h
//  Summary: {any-value! defs AFTER %tmp-internals.h (see: %sys-rebval.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Ren-C Open Source Contributors
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
// This file provides basic accessors for value types.  Because these
// accessors dereference REBVAL (or RELVAL) pointers, the inline functions
// need the complete struct definition available from all the payload types.
//
// See notes in %sys-rebval.h for the definition of the REBVAL structure.
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
// it has in the payload might go bad.  Use PUSH_GC_GUARD() to protect a
// stack variable's payload, and then DROP_GC_GUARD() when the protection
// is not needed.  (You must always drop the most recently pushed guard.)
//
// Function invocations keep their arguments in FRAME!s, which can be accessed
// via ARG() and have stable addresses as long as the function is running.
//


//=//// DEBUG PROBE <== **THIS IS VERY USEFUL** //////////////////////////=//
//
// The PROBE macro can be used in debug builds to mold a REBVAL much like the
// Rebol `probe` operation.  But it's actually polymorphic, and if you have
// a REBSER*, REBCTX*, or REBARR* it can be used with those as well.  In C++,
// you can even get the same value and type out as you put in...just like in
// Rebol, permitting things like `return PROBE(Make_Some_Series(...));`
//
// In order to make it easier to find out where a piece of debug spew is
// coming from, the file and line number will be output as well.
//
// Note: As a convenience, PROBE also flushes the `stdout` and `stderr` in
// case the debug build was using printf() to output contextual information.
//

#if defined(DEBUG_HAS_PROBE)
    #ifdef CPLUSPLUS_11
        template <
            typename T,
            typename std::enable_if<
                std::is_pointer<T>::value  // assume pointers are REBNOD*
            >::type* = nullptr
        >
        T Probe_Cpp_Helper(T v, const char *expr, const char *file, int line)
        {
            Probe_Core_Debug(v, expr, file, line);
            return v;
        }

        template <
            typename T,
            typename std::enable_if<
                !std::is_pointer<T>::value  // ordinary << output operator
            >::type* = nullptr
        >
        T Probe_Cpp_Helper(T v, const char *expr, const char *file, int line)
        {
            std::stringstream ss;
            ss << v;
            printf("PROBE(%s) => %s\n", expr, ss.str().c_str());
            UNUSED(file);
            UNUSED(line);
            return v;
        }

        #define PROBE(v) \
            Probe_Cpp_Helper((v), #v, __FILE__, __LINE__)
    #else
        #define PROBE(v) \
            Probe_Core_Debug((v), #v, __FILE__, __LINE__)  // returns void*
    #endif
#elif !defined(NDEBUG) // don't cause compile time error on PROBE()
    #define PROBE(v) \
        do { \
            printf("DEBUG_HAS_PROBE disabled %s %d\n", __FILE__, __LINE__); \
            fflush(stdout); \
        } while (0)
#endif


//=//// CELL WRITABILITY //////////////////////////////////////////////////=//
//
// Asserting writablity helps avoid very bad catastrophies that might ensue
// if "implicit end markers" could be overwritten.  These are the ENDs that
// are actually other bitflags doing double duty inside a data structure, and
// there is no REBVAL storage backing the position.
//
// (A fringe benefit is catching writes to other unanticipated locations.)
//

#if defined(DEBUG_CELL_WRITABILITY)
    //
    // In the debug build, functions aren't inlined, and the overhead actually
    // adds up very quickly of getting the 3 parameters passed in.  Run the
    // risk of repeating macro arguments to speed up this critical test.

    #define ASSERT_CELL_READABLE_EVIL_MACRO(c,file,line) \
        do { \
            if (not ((c)->header.bits & NODE_FLAG_CELL)) { \
                printf("Non-cell passed to cell read/write routine\n"); \
                panic_at ((c), (file), (line)); \
            } \
            else if (not ((c)->header.bits & NODE_FLAG_NODE)) { \
                printf("Non-node passed to cell read/write routine\n"); \
                panic_at ((c), (file), (line)); \
            } \
        } while (0) // assume trash-oriented checks will catch "free" cells

    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c,file,line) \
        do { \
            ASSERT_CELL_READABLE_EVIL_MACRO(c, file, line); \
            if ((c)->header.bits & (CELL_FLAG_PROTECTED | NODE_FLAG_FREE)) { \
                printf("Protected/free cell passed to writing routine\n"); \
                panic_at ((c), (file), (line)); \
            } \
        } while (0)

    inline static REBCEL(const*) READABLE(
        REBCEL(const*) c,
        const char *file,
        int line
    ){
        ASSERT_CELL_READABLE_EVIL_MACRO(c, file, line);
        return c;
    }

    inline static RELVAL *WRITABLE(
        RELVAL *c,
        const char *file,
        int line
    ){
        ASSERT_CELL_WRITABLE_EVIL_MACRO(c, file, line);
        return c;
    }
#else
    #define ASSERT_CELL_READABLE_EVIL_MACRO(c,file,line)    NOOP
    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c,file,line)    NOOP

    #define READABLE(c,file,line) (c)
    #define WRITABLE(c,file,ilne) (c)
#endif


//=//// "KIND" HEADER BYTE (a REB_XXX type or variation) //////////////////=//
//
// The "kind" of fundamental datatype a cell is lives in the second byte for
// a very deliberate reason.  This means that the signal for an end can be
// a zero byte, allow a C string that is one character long (plus zero
// terminator) to function as an end signal...using only two bytes, while
// still not conflicting with arbitrary UTF-8 strings (including empty ones).
//
// An additional trick is that while there are only up to 64 fundamental types
// in the system (including END), higher values in the byte are used to encode
// escaping levels.  Up to 3 encoding levels can be in the cell itself, with
// additional levels achieved with REB_QUOTED and pointing to another cell.
//

#define FLAG_KIND_BYTE(kind) \
    FLAG_SECOND_BYTE(kind)

#define KIND_BYTE_UNCHECKED(v) \
    SECOND_BYTE((v)->header)

#if defined(NDEBUG)
    #define KIND_BYTE KIND_BYTE_UNCHECKED
#else
    inline static REBYTE KIND_BYTE_Debug(
        const RELVAL *v,
        const char *file,
        int line
    ){
        if (
            (v->header.bits & (
                NODE_FLAG_NODE
                | NODE_FLAG_CELL
                | NODE_FLAG_FREE
            )) == (NODE_FLAG_CELL | NODE_FLAG_NODE)
        ){
            // Unreadable void is signified in the Extra by a negative tick
            //
            if (KIND_BYTE_UNCHECKED(v) == REB_VOID) {
                if (v->extra.tick < 0) {
                    printf("KIND_BYTE() called on unreadable VOID!\n");
                  #ifdef DEBUG_COUNT_TICKS
                    printf("Made on tick: %d\n", cast(int, -v->extra.tick));
                  #endif
                    panic_at (v, file, line);
                }
                return REB_VOID;
            }

            return KIND_BYTE_UNCHECKED(v);  // majority return here
        }

        // Non-cells are allowed to signal REB_END; see Init_Endlike_Header.
        // (We should not be seeing any rebEND signals here, because we have
        // a REBVAL*, and rebEND is a 2-byte character string that can be
        // at any alignment...not necessarily that of a Reb_Header union!)
        //
        if (KIND_BYTE_UNCHECKED(v) == REB_0_END)
            if (v->header.bits & NODE_FLAG_NODE)
                return REB_0_END;

        if (not (v->header.bits & NODE_FLAG_CELL)) {
            printf("KIND_BYTE() called on non-cell\n");
            panic_at (v, file, line);
        }
        if (v->header.bits & NODE_FLAG_FREE) {
            printf("KIND_BYTE() called on invalid cell--marked FREE\n");
            panic_at (v, file, line);
        }
        return KIND_BYTE_UNCHECKED(v);
    }

  #ifdef CPLUSPLUS_11
    //
    // Since KIND_BYTE() is used by checks like IS_WORD() for efficiency, we
    // don't want that to contaminate the use with cells, because if you
    // bothered to change the type view to a cell you did so because you
    // were interested in its unquoted kind.
    //
    inline static REBYTE KIND_BYTE_Debug(
        REBCEL(const*) v,
        const char *file,
        int line
    ) = delete;
  #endif

    #define KIND_BYTE(v) \
        KIND_BYTE_Debug((v), __FILE__, __LINE__)
#endif

// Note: Only change bits of existing cells if the new type payload matches
// the type and bits (e.g. ANY-WORD! to another ANY-WORD!).  Otherwise the
// value-specific flags might be misinterpreted.
//
#if !defined(__cplusplus)
    #define mutable_KIND_BYTE(v) \
        mutable_SECOND_BYTE((v)->header)
#else
    inline static REBYTE& mutable_KIND_BYTE(RELVAL *v) {
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v, __FILE__, __LINE__);
        return mutable_SECOND_BYTE((v)->header);
    }
#endif


// A cell may have a larger KIND_BYTE() than legal Reb_Kind to represent a
// literal in-situ...the actual kind comes from that byte % 64.  But if you
// are interested in the kind of *cell* (for purposes of knowing its bit
// layout expectations) that is stored in the MIRROR_BYTE().

inline static REBCEL(const*) VAL_UNESCAPED(const RELVAL *v);

#define CELL_KIND_UNCHECKED(cell) \
    cast(enum Reb_Kind, MIRROR_BYTE(cell))

#define CELL_TYPE_UNCHECKED(cell) \
    cast(enum Reb_Kind, KIND_BYTE_UNCHECKED(cell) % REB_64)


#if defined(NDEBUG)
    #define CELL_KIND CELL_KIND_UNCHECKED
    #define CELL_TYPE CELL_TYPE_UNCHECKED
    #define CELL_HEART CELL_KIND_UNCHECKED
#else
    #define CELL_TYPE CELL_TYPE_UNCHECKED  // develop this more rigorously

    #if !defined(CPLUSPLUS_11)
        #define CELL_KIND(cell) \
            cast(enum Reb_Kind, MIRROR_BYTE(cast(const RELVAL*, cell)))

        #define CELL_HEART(cell) \
            cast(enum Reb_Kind, MIRROR_BYTE(cast(const RELVAL*, cell)))
    #else
        inline static enum Reb_Kind CELL_KIND(REBCEL(const*) cell) {
            return cast(enum Reb_Kind, MIRROR_BYTE(cast(const RELVAL*, cell)));
        }

        // Don't want to ask an ordinary value cell its kind modulo 64 is;
        // it may be REB_QUOTED and we need to call VAL_UNESCAPED() first!
        //
        inline static enum Reb_Kind CELL_KIND(const RELVAL *v) = delete;

        // !!! New name for mirror byte is "heart byte" (WIP)

        inline static enum Reb_Kind CELL_HEART(REBCEL(const*) cell) {
            return cast(enum Reb_Kind, MIRROR_BYTE(cast(const RELVAL*, cell)));
        }

        // Don't want to ask an ordinary value cell its kind modulo 64 is;
        // it may be REB_QUOTED and we need to call VAL_UNESCAPED() first!
        //
        inline static enum Reb_Kind CELL_HEART(const RELVAL *v) = delete;

    #endif
#endif

inline static REBTYP *CELL_CUSTOM_TYPE(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_CUSTOM);
    return SER(EXTRA(Any, v).node);
}

// Sometimes you have a REBCEL* and need to pass a REBVAL* to something.  It
// doesn't seem there's too much bad that can happen if you do; you'll get
// back something that might be quoted up to 3 levels...if it's an escaped
// cell then it won't be quoted at all.  Main thing to know is that you don't
// necessarily get the original value you had back.
//
inline static const RELVAL* CELL_TO_VAL(REBCEL(const*) cell)
  { return cast(const RELVAL*, cell); }


//=//// VALUE TYPE (always REB_XXX <= REB_MAX) ////////////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...despite the kind byte being
// REB_WORD + REB_64 + REB_64.  Use CELL_KIND() if you wish to know that the
// cell pointer you pass in is carrying a word payload, it does a modulus.
//
// This has additional checks as well, that you're not using "pseudotypes"
// or garbage, or REB_0_END (which should be checked separately with IS_END())
//

#if defined(NDEBUG)
    inline static enum Reb_Kind VAL_TYPE(const RELVAL *v) {
        if (KIND_BYTE(v) >= REB_64)
            return REB_QUOTED;
        return cast(enum Reb_Kind, KIND_BYTE(v));
    }
#else
    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v,
        const char *file,
        int line
    ){
        REBYTE kind_byte = KIND_BYTE(v);

        // Special messages for END and trash (as these are common)
        //
        if (kind_byte == REB_0_END) {
            printf("VAL_TYPE() on END marker (use IS_END() or KIND_BYTE())\n");
            panic_at (v, file, line);
        }
        if (kind_byte % REB_64 >= REB_MAX) {
            printf("VAL_TYPE() on pseudotype/garbage (use KIND_BYTE())\n");
            panic_at (v, file, line);
        }

        if (kind_byte >= REB_64)
            return REB_QUOTED;
        return cast(enum Reb_Kind, kind_byte);
    }

    #define VAL_TYPE(v) \
        VAL_TYPE_Debug(v, __FILE__, __LINE__)
#endif


//=//// GETTING, SETTING, and CLEARING VALUE FLAGS ////////////////////////=//
//
// The header of a cell contains information about what kind of cell it is,
// as well as some flags that are reserved for system purposes.  These are
// the NODE_FLAG_XXX and CELL_FLAG_XXX flags, that work on any cell.
//
// (A previous concept where cells could use some of the header bits to carry
// more data that wouldn't fit in the "extra" or "payload" is deprecated.
// If those three pointers are not enough for the data a type needs, then it
// has to use an additional allocation and point to that.)
//

#define SET_CELL_FLAG(v,name) \
    (WRITABLE(v, __FILE__, __LINE__)->header.bits |= CELL_FLAG_##name)

#define GET_CELL_FLAG(v,name) \
    ((READABLE(v, __FILE__, __LINE__)->header.bits & CELL_FLAG_##name) != 0)

#define CLEAR_CELL_FLAG(v,name) \
    (WRITABLE(v, __FILE__, __LINE__)->header.bits &= ~CELL_FLAG_##name)

#define NOT_CELL_FLAG(v,name) \
    ((READABLE(v, __FILE__, __LINE__)->header.bits & CELL_FLAG_##name) == 0)


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//
//
// RESET_VAL_HEADER clears out the header of *most* bits, setting it to a
// new type.  The type takes up the full second byte of the header (see
// details in %sys-quoted.h for how this byte is used).
//
// The value is expected to already be "pre-formatted" with the NODE_FLAG_CELL
// bit, so that is left as-is.  See also CELL_MASK_PERSIST.
//

inline static REBVAL *RESET_VAL_HEADER_at(
    RELVAL *v,
    enum Reb_Kind k,
    uintptr_t extra

  #if defined(DEBUG_CELL_WRITABILITY)
  , const char *file
  , int line
  #endif
){
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v, file, line);

    v->header.bits &= CELL_MASK_PERSIST;
    v->header.bits |= FLAG_KIND_BYTE(k) | FLAG_MIRROR_BYTE(k) | extra;
    return cast(REBVAL*, v);
}

#if defined(DEBUG_CELL_WRITABILITY)
    #define RESET_VAL_HEADER(v,kind,extra) \
        RESET_VAL_HEADER_at((v), (kind), (extra), __FILE__, __LINE__)
#else
    #define RESET_VAL_HEADER(v,kind,extra) \
        RESET_VAL_HEADER_at((v), (kind), (extra))
#endif

#ifdef DEBUG_TRACK_CELLS
    //
    // RESET_CELL is a variant of RESET_VAL_HEADER that overwrites the entire
    // cell payload with tracking information.  It should not be used if the
    // intent is to preserve the payload and extra.
    //
    // (Because of DEBUG_TRACK_EXTEND_CELLS, it's not necessarily a waste
    // even if you overwrite the Payload/Extra immediately afterward; it also
    // corrupts the data to help ensure all relevant fields are overwritten.)
    //
    inline static REBVAL *RESET_CELL_Debug(
        RELVAL *out,
        enum Reb_Kind kind,
        uintptr_t extra,
        const char *file,
        int line
    ){
      #ifdef DEBUG_CELL_WRITABILITY
        RESET_VAL_HEADER_at(out, kind, extra, file, line);
      #else
        RESET_VAL_HEADER(out, kind, extra);
      #endif

        TRACK_CELL_IF_DEBUG(out, file, line);
        return cast(REBVAL*, out);
    }

    #define RESET_CELL(out,kind,flags) \
        RESET_CELL_Debug((out), (kind), (flags), __FILE__, __LINE__)
#else
    #define RESET_CELL(out,kind,flags) \
       RESET_VAL_HEADER((out), (kind), (flags))
#endif

inline static REBVAL *RESET_CUSTOM_CELL(
    RELVAL *out,
    REBTYP *type,
    REBFLGS flags
){
    RESET_CELL(out, REB_CUSTOM, flags);
    EXTRA(Any, out).node = NOD(type);
    return cast(REBVAL*, out);
}


// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
// This is another case where the debug build doesn't inline functions, and
// for such central routines the overhead of passing 3 args is on the radar.
// Run the risk of repeating macro args to speed up this critical check.
//
#ifdef DEBUG_DONT_CHECK_ALIGN
    #define ALIGN_CHECK_CELL_EVIL_MACRO(c,file,line) \
        NOOP
#else
    #define ALIGN_CHECK_CELL_EVIL_MACRO(c,file,line) \
        if (cast(uintptr_t, (c)) % ALIGN_SIZE != 0) { \
            printf( \
                "Cell address %p not aligned to %d bytes\n", \
                cast(const void*, (c)), \
                cast(int, ALIGN_SIZE) \
            ); \
            panic_at ((c), file, line); \
        }
#endif


#define CELL_MASK_PREP \
    (NODE_FLAG_NODE | NODE_FLAG_CELL)

#define CELL_MASK_PREP_END \
    (CELL_MASK_PREP \
        | FLAG_KIND_BYTE(REB_0) \
        | FLAG_MIRROR_BYTE(REB_0))  // a more explicit CELL_MASK_PREP

inline static RELVAL *Prep_Cell_Core(
    RELVAL *c

  #if defined(DEBUG_TRACK_CELLS)
  , const char *file
  , int line
  #endif
){
  #ifdef DEBUG_MEMORY_ALIGN
    ALIGN_CHECK_CELL_EVIL_MACRO(c, file, line);
  #endif

    c->header.bits = CELL_MASK_PREP;
    TRACK_CELL_IF_DEBUG(cast(RELVAL*, c), file, line);
    return c;
}

#if defined(DEBUG_TRACK_CELLS)
    #define Prep_Cell(c) \
        Prep_Cell_Core((c), __FILE__, __LINE__)
#else
    #define Prep_Cell(c) \
        Prep_Cell_Core(c)
#endif


//=//// TRASH CELLS ///////////////////////////////////////////////////////=//
//
// Trash is a cell (marked by NODE_FLAG_CELL) with NODE_FLAG_FREE set.  To
// prevent it from being inspected while it's in an invalid state, VAL_TYPE
// used on a trash cell will assert in the debug build.
//
// The garbage collector is not tolerant of trash.
//

#if defined(DEBUG_TRASH_MEMORY)
    inline static RELVAL *Set_Trash_Debug(
        RELVAL *v

      #ifdef DEBUG_TRACK_CELLS
      , const char *file
      , int line
      #endif
    ){
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v, file, line);

        v->header.bits &= CELL_MASK_PERSIST;
        v->header.bits |=
            FLAG_KIND_BYTE(REB_T_TRASH)
                | FLAG_MIRROR_BYTE(REB_T_TRASH);

        TRACK_CELL_IF_DEBUG(v, file, line);
        return v;
    }

    #define TRASH_CELL_IF_DEBUG(v) \
        Set_Trash_Debug((v), __FILE__, __LINE__)

    inline static bool IS_TRASH_DEBUG(const RELVAL *v) {
        assert(v->header.bits & NODE_FLAG_CELL);
        return KIND_BYTE_UNCHECKED(v) == REB_T_TRASH;
    }
#else
    inline static REBVAL *TRASH_CELL_IF_DEBUG(RELVAL *v) {
        return cast(REBVAL*, v); // #define of (v) gives compiler warnings
    } // https://stackoverflow.com/q/29565161/
#endif


//=//// END MARKER ////////////////////////////////////////////////////////=//
//
// Historically Rebol arrays were always one value longer than their maximum
// content, and this final slot was used for a REBVAL type called END!.
// Like a '\0' terminator in a C string, it was possible to start from one
// point in the series and traverse to find the end marker without needing
// to look at the length (though the length in the series header is maintained
// in sync, also).
//
// Ren-C changed this so that END is not a user-exposed data type, and that
// it's not a requirement for the byte sequence containing the end byte be
// the full size of a cell.  The type byte (which is 0 for an END) lives in
// the second byte, hence two bytes are sufficient to indicate a terminator.
//

#define END_NODE \
    cast(const REBVAL*, &PG_End_Node) // rebEND is char*, not REBVAL* aligned!

#if defined(DEBUG_TRACK_CELLS) || defined(DEBUG_CELL_WRITABILITY)
    inline static REBVAL *SET_END_Debug(
        RELVAL *v

      #if defined(DEBUG_TRACK_CELLS) || defined(DEBUG_CELL_WRITABILITY)
      , const char *file
      , int line
      #endif
    ){
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v, file, line);

        mutable_KIND_BYTE(v) = REB_0_END; // release build behavior
        mutable_MIRROR_BYTE(v) = REB_0_END;

        TRACK_CELL_IF_DEBUG(v, file, line);
        return cast(REBVAL*, v);
    }

    #define SET_END(v) \
        SET_END_Debug((v), __FILE__, __LINE__)
#else
    inline static REBVAL *SET_END(RELVAL *v) {
        mutable_KIND_BYTE(v) = REB_0_END; // must be a prepared cell
        mutable_MIRROR_BYTE(v) = REB_0_END;
        return cast(REBVAL*, v);
    }
#endif

#ifdef NDEBUG
    #define IS_END(p) \
        (cast(const REBYTE*, p)[1] == REB_0_END)
#else
    inline static bool IS_END_Debug(
        const void *p, // may not have NODE_FLAG_CELL, may be short as 2 bytes
        const char *file,
        int line
    ){
        if (cast(const REBYTE*, p)[0] & 0x40) { // e.g. NODE_FLAG_FREE
            printf("NOT_END() called on garbage\n");
            panic_at(p, file, line);
        }

        if (cast(const REBYTE*, p)[1] == REB_0_END)
            return true;

        if (not (cast(const REBYTE*, p)[0] & 0x01)) { // e.g. NODE_FLAG_CELL
            printf("IS_END() found non-END pointer that's not a cell\n");
            panic_at(p, file, line);
        }

        return false;
    }

    #define IS_END(v) \
        IS_END_Debug((v), __FILE__, __LINE__)
#endif

#define NOT_END(v) \
    (not IS_END(v))


//=////////////////////////////////////////////////////////////////////////=//
//
//  RELATIVE AND SPECIFIC VALUES
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a REBNOD which constitutes their notion of "binding".
//
// This can be null (which indicates unbound), to a function's paramlist
// (which indicates a relative binding), or to a context's varlist (which
// indicates a specific binding.)
//
// The ordering of %types.r is chosen specially so that all bindable types
// are at lower values than the unbindable types.
//


// Note: If incoming p is mutable, we currently assume that's allowed by the
// flag bits of the node.  This could have a runtime check in debug build
// with a C++ variation that only takes mutable pointers.
//
inline static void INIT_VAL_NODE(RELVAL *v, const void *p) {
    assert(GET_CELL_FLAG(v, FIRST_IS_NODE));
    PAYLOAD(Any, v).first.node = NOD(m_cast(void*, p));
}

#define VAL_NODE(v) \
    PAYLOAD(Any, (v)).first.node


// An ANY-WORD! is relative if it refers to a local or argument of a function,
// and has its bits resident in the deep copy of that function's body.
//
// An ANY-ARRAY! in the deep copy of a function body must be relative also to
// the same function if it contains any instances of such relative words.
//
inline static bool IS_RELATIVE(REBCEL(const*) v) {
    if (not Is_Bindable(v) or not EXTRA(Binding, v).node)
        return false; // INTEGER! and other types are inherently "specific"

  #if !defined(NDEBUG)
    //
    // !!! A trick used by RESKINNED for checking return types after its
    // dispatcher is no longer on a stack uses CHAIN's mechanics to run a
    // single argument function that does the test.  To avoid creating a new
    // ACTION! for each such scenario, it makes the value it queues distinct
    // by putting the paramlist that has the return to check in the binding.
    // Ordinarily this would make it a "relative value" which actions never
    // should be, but it's a pretty good trick so it subverts debug checks.
    // Review if this category of trick can be checked for more cleanly.
    if (
        KIND_BYTE_UNCHECKED(v) == REB_ACTION  // v-- VAL_NODE() is paramlist
        and NOD(Natives[N_skinner_return_helper_ID]) == VAL_NODE(v)
    ){
        return false;
    }
  #endif

    if (EXTRA(Binding, v).node->header.bits & ARRAY_FLAG_IS_PARAMLIST)
        return true;

    return false;
}

#ifdef CPLUSPLUS_11
    bool IS_RELATIVE(const REBVAL *v) = delete;  // error on superfluous check
#endif

#define IS_SPECIFIC(v) \
    (not IS_RELATIVE(v))

inline static REBACT *VAL_RELATIVE(const RELVAL *v) {
    assert(IS_RELATIVE(v));
    return ACT(EXTRA(Binding, v).node);
}

// When you have a RELVAL* (e.g. from a REBARR) that you KNOW to be specific,
// you might be bothered by an error like:
//
//     "invalid conversion from 'Reb_Value*' to 'Reb_Specific_Value*'"
//
// You can use SPECIFIC to cast it if you are *sure* that it has been
// derelativized -or- is a value type that doesn't have a specifier (e.g. an
// integer).  If the value is actually relative, this will assert at runtime!

inline static REBVAL *SPECIFIC(const_if_c RELVAL *v) {
    //
    // Note: END is tolerated to help in specified array enumerations, e.g.
    //
    //     REBVAL *head = SPECIFIC(ARR_HEAD(specified_array));  // may be end
    //
    assert(IS_END(v) or IS_SPECIFIC(v));
    return m_cast(REBVAL*, cast(const REBVAL*, v));
}

#if defined(__cplusplus)
    inline static const REBVAL *SPECIFIC(const RELVAL *v) {
        assert(IS_END(v) or IS_SPECIFIC(v));  // ^-- see note about END
        return cast(const REBVAL*, v);
    }

    inline static REBVAL *SPECIFIC(const REBVAL *v) = delete;
#endif



//=////////////////////////////////////////////////////////////////////////=//
//
//  BINDING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a REBNOD which constitutes their notion of "binding".
//
// This can either be null (a.k.a. UNBOUND), or to a function's paramlist
// (indicates a relative binding), or to a context's varlist (which indicates
// a specific binding.)
//
// NOTE: Instead of using null for UNBOUND, a special global REBSER struct was
// experimented with.  It was at a location in memory known at compile time,
// and it had its ->header and ->info bits set in such a way as to avoid the
// need for some conditional checks.  e.g. instead of writing:
//
//     if (binding and binding->header.bits & NODE_FLAG_MANAGED) {...}
//
// The special UNBOUND node set some bits, such as to pretend to be managed:
//
//     if (binding->header.bits & NODE_FLAG_MANAGED) {...} // incl. UNBOUND
//
// Question was whether avoiding the branching involved from the extra test
// for null would be worth it for a consistent ability to dereference.  At
// least on x86/x64, the answer was: No.  It was maybe even a little slower.
// Testing for null pointers the processor has in its hand is very common and
// seemed to outweigh the need to dereference all the time.  The increased
// clarity of having unbound be nullptr is also in its benefit.
//
// NOTE: The ordering of %types.r is chosen specially so that all bindable
// types are at lower values than the unbindable types.
//

#define SPECIFIED \
    ((REBSPC*)nullptr)  // cast() doesn't like nullptr, fix

#define UNBOUND \
   ((REBNOD*)nullptr)  // cast() doesn't like nullptr, fix

inline static REBNOD *VAL_BINDING(REBCEL(const*) v) {
    assert(Is_Bindable(v));
    return EXTRA(Binding, v).node;
}

inline static void INIT_BINDING(RELVAL *v, const void *p) {
    assert(Is_Bindable(v)); // works on partially formed values

    const REBNOD *binding = cast(const REBNOD*, p);
    EXTRA(Binding, v).node = m_cast(REBNOD*, binding);

  #if !defined(NDEBUG)
    if (not binding)
        return; // e.g. UNBOUND

    assert(not (binding->header.bits & NODE_FLAG_CELL)); // not currently used

    if (binding->header.bits & NODE_FLAG_MANAGED) {
        assert(
            binding->header.bits & ARRAY_FLAG_IS_PARAMLIST  // relative
            or binding->header.bits & ARRAY_FLAG_IS_VARLIST  // specific
            or (
                IS_VARARGS(v) and not IS_SER_DYNAMIC(binding)
            ) // varargs from MAKE VARARGS! [...], else is a varlist
        );
    }
    else
        assert(binding->header.bits & ARRAY_FLAG_IS_VARLIST);
  #endif
}

inline static void Move_Value_Header(RELVAL *out, const RELVAL *v)
{
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    assert(KIND_BYTE_UNCHECKED(v) != REB_0_END);  // faster than NOT_END()

    // Note: Pseudotypes are legal to move, but none of them are bindable

    ASSERT_CELL_WRITABLE_EVIL_MACRO(out, __FILE__, __LINE__);

    out->header.bits &= CELL_MASK_PERSIST;
    out->header.bits |= v->header.bits & CELL_MASK_COPY;

  #ifdef DEBUG_TRACK_EXTEND_CELLS
    out->track = v->track;
    out->tick = v->tick; // initialization tick
    out->touch = v->touch; // arbitrary debugging use via TOUCH_CELL
  #endif
}


// Because you cannot assign REBVALs to one another (e.g. `*dest = *src`)
// a function is used.  This provides an opportunity to check things like
// moving data into protected locations, and to mask out bits that should
// not be propagated.
//
// Interface designed to line up with Derelativize()
//
inline static REBVAL *Move_Value(RELVAL *out, const REBVAL *v)
{
    Move_Value_Header(out, v);

    // Payloads cannot hold references to stackvars, raw bit transfer ok.
    //
    // Note: must be copied over *before* INIT_BINDING_MAY_MANAGE is called,
    // so that if it's a REB_QUOTED it can find the literal->cell.
    //
    out->payload = v->payload;

    if (Is_Bindable(v))
        INIT_BINDING_MAY_MANAGE(out, EXTRA(Binding, v).node);
    else
        out->extra = v->extra; // extra isn't a binding (INTEGER! MONEY!...)

    return SPECIFIC(out);
}


// When doing something like a COPY of an OBJECT!, the var cells have to be
// handled specially, e.g. by preserving CELL_FLAG_ARG_MARKED_CHECKED.
//
// !!! What about other non-copyable properties like CELL_FLAG_PROTECTED?
//
inline static REBVAL *Move_Var(RELVAL *out, const REBVAL *v)
{
    // This special kind of copy can only be done into another object's
    // variable slot. (Since the source may be a FRAME!, v *might* be stack
    // but it should never be relative.  If it's stack, we have to go through
    // the whole potential reification process...double-set header for now.)

    Move_Value(out, v);
    out->header.bits |= (
        v->header.bits & (CELL_FLAG_ARG_MARKED_CHECKED)
    );
    return SPECIFIC(out);
}


// Generally speaking, you cannot take a RELVAL from one cell and copy it
// blindly into another...it needs to be Derelativize()'d.  This routine is
// for the rare cases where it's legal, e.g. shuffling a cell from one place
// in an array to another cell in the same array.
//
inline static RELVAL *Blit_Cell(RELVAL *out, const RELVAL *v)
{
    // It's imaginable that you might try to blit a cell from a source that
    // could be an API node.  But it should never be *actually* relative
    // (just tunneled down through some chain of RELVAL*-accepting functions).
    //
    assert(not ((v->header.bits & NODE_FLAG_ROOT) and IS_RELATIVE(v)));

    // However, you should not write relative bits into API destinations,
    // not even hypothetically.  The target should not be an API cell.
    //
    assert(not (out->header.bits & (NODE_FLAG_ROOT | NODE_FLAG_MANAGED)));

    Move_Value_Header(out, v);

    out->payload = v->payload;
    out->extra = v->extra;

    return out;  // still (potentially) relative!
}

#ifdef __cplusplus  // avoid blitting into REBVAL*, and use without specifier
    inline static RELVAL *Blit_Cell(REBVAL *out, const RELVAL *v) = delete;
#endif

// !!! Super primordial experimental `const` feature.  Concept is that various
// operations have to be complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.  const input always
// gets you const output, but mutable input will get you const output if
// the value itself is const (so it inherits).
//
inline static REBVAL *Inherit_Const(REBVAL *out, const RELVAL *influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}
#define Trust_Const(value) \
    (value) // just a marking to say the const is accounted for already

inline static REBVAL *Constify(REBVAL *v) {
    SET_CELL_FLAG(v, CONST);
    return v;
}


//
// Rather than allow a REBVAL to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
// This faciliates the differentiation of cell lifetimes (API vs. stack),
// as well as cell protection states.  It can also be useful for debugging
// scenarios, for knowing where cells are initialized.
//
// Note: because this will run instructions, a routine should avoid doing a
// DECLARE_LOCAL inside of a loop.  It should be at the outermost scope of
// the function.
//
// Note: It sets NODE_FLAG_FREE, so this is a "trash" cell by default.
//
#define DECLARE_LOCAL(name) \
    REBVAL name##_cell; \
    Prep_Cell(&name##_cell); \
    REBVAL * const name = &name##_cell;
