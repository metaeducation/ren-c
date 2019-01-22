//
//  File: %sys-value.h
//  Summary: {any-value! defs AFTER %tmp-internals.h (see: %sys-rebval.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2018 Rebol Open Source Contributors
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


//=////////////////////////////////////////////////////////////////////////=//
//
//  DEBUG PROBE <== **THIS IS VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
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
        template <typename T>
        T Probe_Cpp_Helper(T v, const char *file, int line) {
            return cast(T, Probe_Core_Debug(v, file, line));
        }

        #define PROBE(v) \
            Probe_Cpp_Helper((v), __FILE__, __LINE__) // passes input as-is
    #else
        #define PROBE(v) \
            Probe_Core_Debug((v), __FILE__, __LINE__) // just returns void* :(
    #endif
#elif !defined(NDEBUG) // don't cause compile time error on PROBE()
    #define PROBE(v) \
        do { \
            printf("DEBUG_HAS_PROBE disabled %s %d\n", __FILE__, __LINE__); \
            fflush(stdout); \
        } while (0)
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  TRACKING PAYLOAD <== **THIS IS VERY USEFUL**
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In the debug build, "Trash" cells (NODE_FLAG_FREE) can use their payload to
// store where and when they were initialized.  This also applies to some
// datatypes like BLANK!, BAR!, LOGIC!, or VOID!--since they only use their
// header bits, they can also use the payload for this in the debug build.
//
// (Note: The release build does not canonize unused bits of payloads, so
// they are left as random data in that case.)
//
// View this information in the debugging watchlist under the `track` union
// member of a value's payload.  It is also reported by panic().
//

#if defined(DEBUG_TRACK_CELLS)
    #if defined(DEBUG_COUNT_TICKS) && defined(DEBUG_TRACK_EXTEND_CELLS)
        #define TOUCH_CELL(c) \
            ((c)->touch = TG_Tick)
    #endif

    inline static void Set_Track_Payload_Extra_Debug(
        RELVAL *c,
        const char *file,
        int line
    ){
      #ifdef DEBUG_TRACK_EXTEND_CELLS // cell is made bigger to hold it
        c->track.file = file;
        c->track.line = line;

        #ifdef DEBUG_COUNT_TICKS
            c->extra.tick = c->tick = TG_Tick;
            c->touch = 0;
        #else
            c->extra.tick = 1; // unreadable blank needs for debug payload
        #endif
      #else // in space that is overwritten for cells that fill in payloads 
        c->payload.track.file = file;
        c->payload.track.line = line;
          
        #ifdef DEBUG_COUNT_TICKS
            c->extra.tick = TG_Tick;
        #else
            c->extra.tick = 1; // unreadable blank needs for debug payload
        #endif
      #endif
    }

    #define TRACK_CELL_IF_DEBUG(c,file,line) \
        Set_Track_Payload_Extra_Debug((c), (file), (line))

#elif !defined(NDEBUG)

    #define TRACK_CELL_IF_DEBUG(c,file,line) \
        ((c)->extra.tick = 1) // unreadable blank needs for debug payload

#else

    #define TRACK_CELL_IF_DEBUG(c,file,line) \
        NOOP

#endif


//=//// CELL WRITABILITY //////////////////////////////////////////////////=//
//
// Asserting writiablity helps avoid very bad catastrophies that might ensue
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

    inline static const REBCEL *READABLE(
        const REBCEL *c,
        const char *file,
        int line
    ){
        ASSERT_CELL_READABLE_EVIL_MACRO(c, file, line);
        return c;
    }

    inline static REBCEL *WRITABLE(
        REBCEL *c,
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
        // KIND_BYTE is called *a lot*, so that makes it a great place to do
        // sanity checks in the debug build.  But a debug build will not
        // inline this function, and makes *no* optimizations.  Using no
        // stack space e.g. no locals) is ideal.  (If -Og "debug" optimization
        // is used, that should actually be able to be fast, since it isn't
        // needing to keep an actual local around to display.)

        if (
            (v->header.bits & (
                NODE_FLAG_NODE
                | NODE_FLAG_CELL
                | NODE_FLAG_FREE
                | VALUE_FLAG_FALSEY // all the "bad" types are also falsey
            )) == (NODE_FLAG_CELL | NODE_FLAG_NODE)
        ){
            return KIND_BYTE_UNCHECKED(v); // majority return here
        }

        // Non-cells are allowed to signal REB_END; see Init_Endlike_Header.
        // (We should not be seeing any rebEND signals here, because we have
        // a REBVAL*, and rebEND is a 2-byte character string that can be
        // at any alignment...not necessarily that of a Reb_Header union!)
        //
        if (KIND_BYTE_UNCHECKED(v) == REB_0_END)
            if (v->header.bits & NODE_FLAG_NODE)
                return REB_0_END;

        // Could be a LOGIC! false, blank, or NULL bit pattern in bad cell
        //
        if (not (v->header.bits & NODE_FLAG_CELL)) {
            printf("KIND_BYTE() called on non-cell\n");
            panic_at (v, file, line);
        }
        if (v->header.bits & NODE_FLAG_FREE) {
            printf("KIND_BYTE() called on invalid cell--marked FREE\n");
            panic_at (v, file, line);
        }

        // Unreadable blank is signified in the Extra by a negative tick
        //
        if (KIND_BYTE_UNCHECKED(v) == REB_BLANK) {
            if (v->extra.tick < 0) {
                printf("KIND_BYTE() called on unreadable BLANK!\n");
              #ifdef DEBUG_COUNT_TICKS
                printf("Was made on tick: %d\n", cast(int, -v->extra.tick));
              #endif
                panic_at (v, file, line);
            }
            return REB_BLANK;
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
        const REBCEL *v,
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

#define FLAGIT_KIND(t) \
    (cast(REBU64, 1) << (t)) // makes a 64-bit bitflag

// A cell may have a larger KIND_BYTE() than legal Reb_Kind to represent a
// literal in-situ, and you may want to know its actual CELL_KIND().

inline static const REBCEL *VAL_UNESCAPED(const RELVAL *v);

#define CELL_KIND_UNCHECKED(cell) \
    cast(enum Reb_Kind, KIND_BYTE_UNCHECKED(cell) % REB_64)

#if defined(NDEBUG)
    #define CELL_KIND CELL_KIND_UNCHECKED
#else
    #if !defined(CPLUSPLUS_11)
        #define CELL_KIND(cell) \
            cast(enum Reb_Kind, KIND_BYTE(cast(const RELVAL*, cell)) % REB_64)
    #else
        inline static enum Reb_Kind CELL_KIND(const REBCEL *cell) {
            return cast(enum Reb_Kind,
                KIND_BYTE(cast(const RELVAL*, cell)) % REB_64
            );
        }

        // Don't want to ask an ordinary value cell its kind modulo 64 is;
        // it may be REB_QUOTED and we need to call VAL_UNESCAPED() first!
        //
        inline static enum Reb_Kind CELL_KIND(const RELVAL *v) = delete;
    #endif
#endif


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
        if (kind_byte % REB_64 == REB_0_END) {
            printf("VAL_TYPE() on END marker (use IS_END() or KIND_BYTE())\n");
            panic_at (v, file, line);
        }
        if (kind_byte % REB_64 > REB_MAX_NULLED) {
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
// the NODE_FLAG_XXX and VALUE_FLAG_XXX flags, that work on any cell.
//
// (A previous concept where cells could use some of the header bits to carry
// more data that wouldn't fit in the "extra" or "payload" is deprecated.
// If those three pointers are not enough for the data a type needs, then it
// has to use an additional allocation and point to that.)
//

#define SET_VAL_FLAGS(v,f) \
    (WRITABLE(v, __FILE__, __LINE__)->header.bits |= (f))

#define ANY_VAL_FLAGS(v,f) \
    ((READABLE(v, __FILE__, __LINE__)->header.bits & (f)) != 0)

inline static bool ALL_VAL_FLAGS(REBCEL *v, uintptr_t f) // repeats parameter
    { return (READABLE(v, __FILE__, __LINE__)->header.bits & f) == f; }

#define CLEAR_VAL_FLAGS(v,f) \
    (WRITABLE(v, __FILE__, __LINE__)->header.bits &= ~(f))

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    //
    // Debug builds don't inline functions; don't add another of layer of call
    // for a compile-time check that the release build can take care of.
    // 
    #define SET_VAL_FLAG(v,f)       SET_VAL_FLAGS((v), (f))
    #define GET_VAL_FLAG(v, f)      ANY_VAL_FLAGS((v), (f))
    #define CLEAR_VAL_FLAG(v,f)     CLEAR_VAL_FLAGS((v), (f))
#else
    // Use compile-time check on variants for dealing with exactly one bit.
    // While this may seem kind of overkill, it's actually good for forcing
    // you to distinguish if you were trying to test that ANY of the bits
    // were set, vs ALL of the bits set...an easy thing to get confused on.

    template <uintptr_t f>
    inline static void SET_VAL_FLAG_cplusplus(REBCEL *v) {
        static_assert(
            f and (f & (f - 1)) == 0, // only one bit is set
            "use SET_VAL_FLAGS() to set multiple bits"
        );
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v, __FILE__, __LINE__);
        v->header.bits |= f;
    }
    #define SET_VAL_FLAG(v,f) \
        SET_VAL_FLAG_cplusplus<f>(v)
        
    template <uintptr_t f>
    inline static bool GET_VAL_FLAG_cplusplus(const REBCEL *v) {
        static_assert(
            f and (f & (f - 1)) == 0, // only one bit is set
            "use ANY_VAL_FLAGS() or ALL_VAL_FLAGS() to test multiple bits"
        );
        ASSERT_CELL_READABLE_EVIL_MACRO(v, __FILE__, __LINE__);
        return (v->header.bits & f);
    }
    #define GET_VAL_FLAG(v,f) \
        GET_VAL_FLAG_cplusplus<f>(v)

    template <uintptr_t f>
    inline static void CLEAR_VAL_FLAG_cplusplus(REBCEL *v) {
        static_assert(
            f and (f & (f - 1)) == 0, // only one bit is set
            "use CLEAR_VAL_FLAGS() to remove multiple bits"
        );
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v, __FILE__, __LINE__);
        v->header.bits &= ~f;
    }
    #define CLEAR_VAL_FLAG(v,f) \
        CLEAR_VAL_FLAG_cplusplus<f>(v)
#endif

#define NOT_VAL_FLAG(v,f) \
    (not GET_VAL_FLAG((v), (f)))


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL HEADERS AND PREPARATION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// RESET_VAL_HEADER clears out the header of *most* bits, setting it to a
// new type.  The type takes up the full second byte of the header (see
// details in %sys-quoted.h for how this byte is used).
//
// The value is expected to already be "pre-formatted" with the NODE_FLAG_CELL
// bit, so that is left as-is.  It is also expected that CELL_FLAG_STACK has
// been set if the value is stack-based (e.g. on the C stack or in a frame),
// so that is left as-is also.  See CELL_MASK_PERSIST.
//

inline static REBVAL *RESET_VAL_HEADER_EXTRA_Core(
    RELVAL *v,
    enum Reb_Kind kind,
    uintptr_t extra

  #if defined(DEBUG_CELL_WRITABILITY)
  , const char *file
  , int line
  #endif
){
    ASSERT_CELL_WRITABLE_EVIL_MACRO(v, file, line);

    v->header.bits &= CELL_MASK_PERSIST;
    v->header.bits |= FLAG_KIND_BYTE(kind) | extra;
    return cast(REBVAL*, v);
}

#if defined(DEBUG_CELL_WRITABILITY)
    #define RESET_VAL_HEADER_EXTRA(v,kind,extra) \
        RESET_VAL_HEADER_EXTRA_Core((v), (kind), (extra), __FILE__, __LINE__)
#else
    #define RESET_VAL_HEADER_EXTRA(v,kind,extra) \
        RESET_VAL_HEADER_EXTRA_Core((v), (kind), (extra))
#endif

#define RESET_VAL_HEADER(v,kind) \
    RESET_VAL_HEADER_EXTRA((v), (kind), 0)

#ifdef DEBUG_TRACK_CELLS
    //
    // RESET_CELL_EXTRA is a variant of RESET_VAL_HEADER_EXTRA that actually
    // overwrites the payload with tracking information.  It should not be
    // used if the intent is to preserve the payload and extra.
    //
    // (Because of DEBUG_TRACK_EXTEND_CELLS, it's not necessarily a waste
    // even if you overwrite the Payload/Extra immediately afterward; it also
    // corrupts the data to help ensure all relevant fields are overwritten.)
    //
    inline static REBVAL *RESET_CELL_EXTRA_Debug(
        RELVAL *out,
        enum Reb_Kind kind,
        uintptr_t extra,
        const char *file,
        int line
    ){
      #ifdef DEBUG_CELL_WRITABILITY
        RESET_VAL_HEADER_EXTRA_Core(out, kind, extra, file, line);
      #else
        RESET_VAL_HEADER_EXTRA(out, kind, extra);
      #endif

        TRACK_CELL_IF_DEBUG(out, file, line);
        return cast(REBVAL*, out);
    }

    #define RESET_CELL_EXTRA(out,kind,extra) \
        RESET_CELL_EXTRA_Debug((out), (kind), (extra), __FILE__, __LINE__)
#else
    #define RESET_CELL_EXTRA(out,kind,extra) \
       RESET_VAL_HEADER_EXTRA((out), (kind), (extra))
#endif

#define RESET_CELL(out,kind) \
    RESET_CELL_EXTRA((out), (kind), 0)


// This is another case where the debug build doesn't inline functions, and
// for such central routines the overhead of passing 3 args is on the radar.
// Run the risk of repeating macro args to speed up this critical check.
//
#define ALIGN_CHECK_CELL_EVIL_MACRO(c,file,line) \
    if (cast(uintptr_t, (c)) % ALIGN_SIZE != 0) { \
        printf( \
            "Cell address %p not aligned to %d bytes\n", \
            cast(const void*, (c)), \
            cast(int, ALIGN_SIZE) \
        ); \
        panic_at ((c), file, line); \
    }

#define CELL_MASK_NON_STACK \
    (NODE_FLAG_NODE | NODE_FLAG_CELL)

#define CELL_MASK_NON_STACK_END \
    (CELL_MASK_NON_STACK | FLAG_KIND_BYTE(REB_0)) // same, but more explicit

inline static void Prep_Non_Stack_Cell_Core(
    RELVAL *c

  #if defined(DEBUG_TRACK_CELLS)
  , const char *file
  , int line
  #endif
){
  #ifdef DEBUG_MEMORY_ALIGN
    ALIGN_CHECK_CELL_EVIL_MACRO(c, file, line);
  #endif

    c->header.bits = CELL_MASK_NON_STACK;
    TRACK_CELL_IF_DEBUG(cast(RELVAL*, c), file, line);
}

#if defined(DEBUG_TRACK_CELLS)
    #define Prep_Non_Stack_Cell(c) \
        Prep_Non_Stack_Cell_Core((c), __FILE__, __LINE__)
#else
    #define Prep_Non_Stack_Cell(c) \
        Prep_Non_Stack_Cell_Core(c)
#endif

#define CELL_MASK_STACK \
    (NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_STACK)

inline static RELVAL *Prep_Stack_Cell_Core(
    RELVAL *c

  #if defined(DEBUG_TRACK_CELLS)
  , const char *file
  , int line
  #endif
){
  #ifdef DEBUG_MEMORY_ALIGN
    ALIGN_CHECK_CELL_EVIL_MACRO(c, file, line);
  #endif
  #ifdef DEBUG_TRASH_MEMORY
    c->header.bits = CELL_MASK_STACK | FLAG_KIND_BYTE(REB_T_TRASH)
        | VALUE_FLAG_FALSEY; // speeds up VAL_TYPE_Debug() check
  #else
    c->header.bits = CELL_MASK_STACK | FLAG_KIND_BYTE(REB_0);
  #endif
    TRACK_CELL_IF_DEBUG(cast(RELVAL*, c), file, line);
    return c;
}

#if defined(DEBUG_TRACK_CELLS)
    #define Prep_Stack_Cell(c) \
        Prep_Stack_Cell_Core((c), __FILE__, __LINE__)
#else
    #define Prep_Stack_Cell(c) \
        Prep_Stack_Cell_Core(c)
#endif


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
        v->header.bits |= FLAG_KIND_BYTE(REB_T_TRASH)
            | VALUE_FLAG_FALSEY; // speeds up VAL_TYPE_Debug() check

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


//=////////////////////////////////////////////////////////////////////////=//
//
//  END marker (not a value type, only writes `struct Reb_Value_Flags`)
//
//=////////////////////////////////////////////////////////////////////////=//
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

        mutable_SECOND_BYTE(v->header) = REB_0_END; // release build behavior
        v->header.bits |= VALUE_FLAG_FALSEY; // speeds VAL_TYPE_Debug() check

        TRACK_CELL_IF_DEBUG(v, file, line);
        return cast(REBVAL*, v);
    }

    #define SET_END(v) \
        SET_END_Debug((v), __FILE__, __LINE__)
#else
    inline static REBVAL *SET_END(RELVAL *v) {
        mutable_SECOND_BYTE(v->header) = REB_0_END; // must be a prepared cell
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

// An ANY-WORD! is relative if it refers to a local or argument of a function,
// and has its bits resident in the deep copy of that function's body.
//
// An ANY-ARRAY! in the deep copy of a function body must be relative also to
// the same function if it contains any instances of such relative words.
//
inline static bool IS_RELATIVE(const REBCEL *v) {
    if (Not_Bindable(v) or not v->extra.binding)
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
        KIND_BYTE_UNCHECKED(v) == REB_ACTION
        and Natives[N_skinner_return_helper_ID].payload.action.paramlist
            == v->payload.action.paramlist
    ){
        return false;
    }
  #endif

    return GET_SER_FLAG(v->extra.binding, ARRAY_FLAG_PARAMLIST);
}

#if defined(__cplusplus) && __cplusplus >= 201103L
    //
    // Take special advantage of the fact that C++ can help catch when we are
    // trying to see if a REBVAL is specific or relative (it will always
    // be specific, so the call is likely in error).  In the C build, they
    // are the same type so there will be no error.
    //
    bool IS_RELATIVE(const REBVAL *v);
#endif

#define IS_SPECIFIC(v) \
    cast(bool, not IS_RELATIVE(v))

inline static REBACT *VAL_RELATIVE(const RELVAL *v) {
    assert(IS_RELATIVE(v));
    return ACT(v->extra.binding);
}


// When you have a RELVAL* (e.g. from a REBARR) that you "know" to be specific,
// the KNOWN macro can be used for that.  Checks to make sure in debug build.
//
// Use for: "invalid conversion from 'Reb_Value*' to 'Reb_Specific_Value*'"

#if !defined(__cplusplus) // poorer protection in C, loses constness
    inline static REBVAL *KNOWN(const REBCEL *v) {
        assert(IS_END(v) or IS_SPECIFIC(v));
        return m_cast(REBVAL*, c_cast(REBCEL*, v));
    }
#else
    inline static const REBVAL *KNOWN(const REBCEL *v) {
        assert(IS_END(v) or IS_SPECIFIC(v)); // END for KNOWN(ARR_HEAD()), etc.
        return cast(const REBVAL*, v);
    }

    inline static REBVAL *KNOWN(REBCEL *v) {
        assert(IS_END(v) or IS_SPECIFIC(v)); // END for KNOWN(ARR_HEAD()), etc.
        return cast(REBVAL*, v);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  NULLED CELLS (*internal* form of Rebol NULL)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's null is a transient evaluation product.  It is used as a signal for
// "soft failure", e.g. `find [a b] 'c` is null, hence they are conditionally
// false.  But null isn't an "ANY-VALUE!", and can't be stored in BLOCK!s that
// are seen by the user--nor can it be assigned to variables.
//
// The libRebol API takes advantage of this by actually using C's concept of
// a null pointer to directly represent the optional state.  By promising this
// is the case, clients of the API can write `if (value)` or `if (!value)`
// and be sure that there's not some nonzero address of a "null-valued cell".
// So there is no `isRebolNull()` API.
//
// But that's the API.  Internal to Rebol, cells are the currency used, and
// if they are to represent an "optional" value, there must be a special
// bit pattern used to mark them as not containing any value at all.  These
// are called "nulled cells" and marked by means of their VAL_TYPE(), but they
// use REB_MAX--because that is one past the range of valid REB_XXX values
// in the enumeration created for the actual types.
//

#define NULLED_CELL \
    c_cast(const REBVAL*, &PG_Nulled_Cell[0])

#define IS_NULLED(v) \
    (VAL_TYPE(v) == REB_MAX_NULLED)

#define Init_Nulled(out) \
    RESET_CELL_EXTRA((out), REB_MAX_NULLED, VALUE_FLAG_FALSEY)

// !!! A theory was that the "evaluated" flag would help a function that took
// both <opt> and <end>, which are converted to nulls, distinguish what kind
// of null it is.  This may or may not be a good idea, but unevaluating it
// here just to make a note of the concept, and tag it via the callsites.
//
#define Init_Endish_Nulled(out) \
    RESET_CELL_EXTRA((out), REB_MAX_NULLED, \
        VALUE_FLAG_FALSEY | VALUE_FLAG_UNEVALUATED)

inline static bool IS_ENDISH_NULLED(const RELVAL *v) {
    return IS_NULLED(v) and GET_VAL_FLAG(v, VALUE_FLAG_UNEVALUATED);
}

// To help ensure full nulled cells don't leak to the API, the variadic
// interface only accepts nullptr.  Any internal code with a REBVAL* that may
// be a "nulled cell" must translate any such cells to nullptr.
//
inline static const REBVAL *NULLIFY_NULLED(const REBVAL *cell)
  { return VAL_TYPE(cell) == REB_MAX_NULLED ? nullptr : cell; }

inline static const REBVAL *REIFY_NULL(const REBVAL *cell)
  { return cell == nullptr ? NULLED_CELL : cell; }


//=////////////////////////////////////////////////////////////////////////=//
//
//  VOID!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Void! results are the default for `do []`, and unlike NULL a void! *is*
// a value...however a somewhat unfriendly one.  While NULLs are falsey, void!
// is *neither* truthy nor falsey.  Though a void! can be put in an array (a
// NULL can't) if the evaluator tries to run a void! cell in an array, it will
// trigger an error.
//
// Void! also comes into play in what is known as "voidification" of NULLs.
// Loops wish to reserve NULL as the return result if there is a BREAK, and
// conditionals like IF and SWITCH want to reserve NULL to mean there was no
// branch taken.  So when branches or loop bodies produce null, they need
// to be converted to some ANY-VALUE!.
//
// The console doesn't print anything for void! evaluation results by default,
// so that routines like HELP won't have additional output than what they
// print out.
//

#define VOID_VALUE \
    c_cast(const REBVAL*, &PG_Void_Value[0])

#define Init_Void(out) \
    RESET_CELL((out), REB_VOID)

inline static REBVAL *Voidify_If_Nulled(REBVAL *cell) {
    if (IS_NULLED(cell))
        Init_Void(cell);
    return cell;
}

// Many loop constructs use BLANK! as a unique signal that the loop body
// never ran, e.g. `for-each x [] [<unreturned>]` or `loop 0 [<unreturned>]`.
// It's more valuable to have that signal be unique and have it be falsey
// than it is to be able to return BLANK! from a loop, so blanks are voidified
// alongside NULL (reserved for BREAKing)
//
inline static REBVAL *Voidify_If_Nulled_Or_Blank(REBVAL *cell) {
    if (IS_NULLED_OR_BLANK(cell))
        Init_Void(cell);
    return cell;
}


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

#define Init_Bar(out) \
    RESET_CELL((out), REB_BAR);


//=////////////////////////////////////////////////////////////////////////=//
//
//  BLANK!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Blank! values are a kind of "reified" null/void!, and you can convert
// between them using TRY and OPT:
//
//     >> try ()
//     == _
//
//     >> opt _
//     ;-- no result
//
// Like null, they are considered to be false--like the LOGIC! #[false] value.
// Only these three things are conditionally false in Rebol, and testing for
// conditional truth and falsehood is frequent.  Hence in addition to its
// type, BLANK! also carries a header bit that can be checked for conditional
// falsehood, to save on needing to separately test the type.
//
// In the debug build, it is possible to make an "unreadable" blank!.  This
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
    RESET_CELL_EXTRA((v), REB_BLANK, VALUE_FLAG_FALSEY)

#ifdef DEBUG_UNREADABLE_BLANKS
    inline static REBVAL *Init_Unreadable_Blank_Debug(
        RELVAL *out, const char *file, int line
    ){
        RESET_CELL_EXTRA_Debug(out, REB_BLANK, VALUE_FLAG_FALSEY, file, line);
        assert(out->extra.tick > 0);
        out->extra.tick = -out->extra.tick;
        return KNOWN(out);
    }

    #define Init_Unreadable_Blank(out) \
        Init_Unreadable_Blank_Debug((out), __FILE__, __LINE__)

    #define IS_BLANK_RAW(v) \
        (KIND_BYTE_UNCHECKED(v) == REB_BLANK)

    inline static bool IS_UNREADABLE_DEBUG(const RELVAL *v) {
        if (KIND_BYTE_UNCHECKED(v) != REB_BLANK)
            return false;
        return v->extra.tick < 0;
    }

    #define ASSERT_UNREADABLE_IF_DEBUG(v) \
        assert(IS_UNREADABLE_DEBUG(v))

    #define ASSERT_READABLE_IF_DEBUG(v) \
        assert(not IS_UNREADABLE_DEBUG(v))
#else
    #define Init_Unreadable_Blank(v) \
        Init_Blank(v)

    #define IS_BLANK_RAW(v) \
        IS_BLANK(v)

    #define ASSERT_UNREADABLE_IF_DEBUG(v) \
        assert(IS_BLANK(v)) // would have to be a blank even if not unreadable

    #define ASSERT_READABLE_IF_DEBUG(v) \
        NOOP
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
// that a single check can test for BLANK!, logic false, or nulled.
//

#define FALSE_VALUE \
    c_cast(const REBVAL*, &PG_False_Value[0])

#define TRUE_VALUE \
    c_cast(const REBVAL*, &PG_True_Value[0])

inline static bool IS_TRUTHY(const RELVAL *v) {
    if (KIND_BYTE(v) >= REB_64) {
        //
        // QUOTED! at an escape level low enough to reuse cell.  So if that
        // cell happens to be false/blank/nulled, VALUE_FLAG_FALSEY will
        // be set, but don't heed it! `if lit '_ [-- "this is truthy"]`
        //
        return true;
    }
    if (GET_VAL_FLAG(v, VALUE_FLAG_FALSEY))
        return false;
    if (IS_VOID(v))
        fail (Error_Void_Conditional_Raw());
    return true;
}

#define IS_FALSEY(v) \
    (not IS_TRUTHY(v))

#define Init_Logic(out,b) \
    RESET_CELL_EXTRA((out), REB_LOGIC, (b) ? 0 : VALUE_FLAG_FALSEY)

#define Init_True(out) \
    Init_Logic((out), true)

#define Init_False(out) \
    Init_Logic((out), false)


// Although a BLOCK! value is true, some constructs are safer by not allowing
// literal blocks.  e.g. `if [x] [print "this is not safe"]`.  The evaluated
// bit can let these instances be distinguished.  Note that making *all*
// evaluations safe would be limiting, e.g. `foo: any [false-thing []]`...
// So ANY and ALL use IS_TRUTHY() directly
//
inline static bool IS_CONDITIONAL_TRUE(const REBVAL *v) {
    if (IS_FALSEY(v))
        return false;
    if (KIND_BYTE(v) == REB_BLOCK)
        if (GET_VAL_FLAG(v, VALUE_FLAG_UNEVALUATED))
            fail (Error_Block_Conditional_Raw(v));
    return true;
}

#define IS_CONDITIONAL_FALSE(v) \
    (not IS_CONDITIONAL_TRUE(v))

inline static bool VAL_LOGIC(const REBCEL *v) {
    assert(CELL_KIND(v) == REB_LOGIC);
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
// %words.r is arranged so symbols for types are at the start of the enum.
// Note REB_0 is not a type, which lines up with SYM_0 used for symbol IDs as
// "no symbol".  Also, NULL is not a value type, and is at REB_MAX past the
// end of the list.
//
// !!! Consider renaming (or adding a synonym) to just TYPE!
//

#define VAL_TYPE_KIND(v) \
    ((v)->payload.datatype.kind)

#define VAL_TYPE_SPEC(v) \
    ((v)->payload.datatype.spec)


//=////////////////////////////////////////////////////////////////////////=//
//
//  CHAR!
//
//=////////////////////////////////////////////////////////////////////////=//

#define MAX_CHAR 0xffff

#define VAL_CHAR(v) \
    ((v)->payload.character)

inline static REBVAL *Init_Char(RELVAL *out, REBUNI uni) {
    RESET_CELL(out, REB_CHAR);
    VAL_CHAR(out) = uni;
    return cast(REBVAL*, out);
}

#define SPACE_VALUE \
    Root_Space_Char

#define NEWLINE_VALUE \
    Root_Newline_Char


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

#if defined(NDEBUG) || !defined(CPLUSPLUS_11) 
    #define VAL_INT64(v) \
        ((v)->payload.integer)
#else
    // allows an assert, but also lvalue: `VAL_INT64(v) = xxx`
    //
    inline static REBI64 & VAL_INT64(REBCEL *v) { // C++ reference type
        assert(CELL_KIND(v) == REB_INTEGER);
        return v->payload.integer;
    }
    inline static REBI64 VAL_INT64(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_INTEGER);
        return v->payload.integer;
    }
#endif

inline static REBVAL *Init_Integer(RELVAL *out, REBI64 i64) {
    RESET_CELL(out, REB_INTEGER);
    out->payload.integer = i64;
    return cast(REBVAL*, out);
}

inline static int32_t VAL_INT32(const REBCEL *v) {
    if (VAL_INT64(v) > INT32_MAX or VAL_INT64(v) < INT32_MIN)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(int32_t, VAL_INT64(v));
}

inline static uint32_t VAL_UINT32(const REBCEL *v) {
    if (VAL_INT64(v) < 0 or VAL_INT64(v) > UINT32_MAX)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(uint32_t, VAL_INT64(v));
}

inline static REBYTE VAL_UINT8(const REBCEL *v) {
    if (VAL_INT64(v) > 255 or VAL_INT64(v) < 0)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(REBYTE, VAL_INT32(v));
}


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

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define VAL_DECIMAL(v) \
        ((v)->payload.decimal)
#else
    // allows an assert, but also lvalue: `VAL_DECIMAL(v) = xxx`
    //
    inline static REBDEC & VAL_DECIMAL(REBCEL *v) { // C++ reference type
        assert(CELL_KIND(v) == REB_DECIMAL or CELL_KIND(v) == REB_PERCENT);
        return v->payload.decimal;
    }
    inline static REBDEC VAL_DECIMAL(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_DECIMAL or CELL_KIND(v) == REB_PERCENT);
        return v->payload.decimal;
    }
#endif

inline static REBVAL *Init_Decimal(RELVAL *out, REBDEC d) {
    RESET_CELL(out, REB_DECIMAL);
    out->payload.decimal = d;
    return cast(REBVAL*, out);
}

inline static REBVAL *Init_Percent(RELVAL *out, REBDEC d) {
    RESET_CELL(out, REB_PERCENT);
    out->payload.decimal = d;
    return cast(REBVAL*, out);
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
// !!! It would be better if there were no "deci" structure independent of
// a REBVAL itself, so long as it is designed to fit in a REBVAL anyway.
//

inline static deci VAL_MONEY_AMOUNT(const REBCEL *v) {
    deci amount;
    amount.m0 = v->extra.m0;
    amount.m1 = v->payload.money.m1;
    amount.m2 = v->payload.money.m2;
    amount.s = v->payload.money.s;
    amount.e = v->payload.money.e;
    return amount;
}

inline static REBVAL *Init_Money(RELVAL *out, deci amount) {
    RESET_CELL(out, REB_MONEY);
    out->extra.m0 = amount.m0;
    out->payload.money.m1 = amount.m1;
    out->payload.money.m2 = amount.m2;
    out->payload.money.s = amount.s;
    out->payload.money.e = amount.e;
    return cast(REBVAL*, out);
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
    ((sizeof(uint32_t) * 2) - 1) // for same properties on 64-bit and 32-bit

#if !defined(CPLUSPLUS_11)
    #define VAL_TUPLE(v) \
        ((v)->payload.tuple.tuple + 1)

    #define VAL_TUPLE_DATA(v) \
        ((v)->payload.tuple.tuple)

    #define VAL_TUPLE_LEN(v) \
        ((v)->payload.tuple.tuple[0])
#else
    // C++ build can give const-correctness so you don't change read-only data

    inline static const REBYTE *VAL_TUPLE(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple + 1;
    }

    inline static REBYTE *VAL_TUPLE(REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple + 1;
    }

    inline static const REBYTE *VAL_TUPLE_DATA(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple;
    }

    inline static REBYTE *VAL_TUPLE_DATA(REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple;
    }

    inline static REBYTE VAL_TUPLE_LEN(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple[0];
    }

    inline static REBYTE &VAL_TUPLE_LEN(REBCEL *v) {
        assert(CELL_KIND(v) == REB_TUPLE);
        return v->payload.tuple.tuple[0];
    }
#endif


inline static REBVAL *Init_Tuple(RELVAL *out, const REBYTE *data) {
    RESET_CELL(out, REB_TUPLE);
    memcpy(VAL_TUPLE_DATA(out), data, sizeof(out->payload.tuple.tuple));
    return cast(REBVAL*, out);
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

inline static void SET_EVENT_INFO(
    RELVAL *val,
    uint8_t type,
    uint8_t flags,
    uint8_t win
){
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
    VAL_EVENT_DATA(v) = cast(uint32_t, ((y << 16) | (x & 0xffff)));
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

#if defined(NDEBUG) || !defined(CPLUSPLUS_11)
    #define VAL_GOB(v) \
        (v)->payload.gob.gob

    #define VAL_GOB_INDEX(v) \
        (v)->payload.gob.index
#else
    inline static REBGOB* const &VAL_GOB(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return v->payload.gob.gob;
    }

    inline static REBCNT const &VAL_GOB_INDEX(const REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return v->payload.gob.index;
    }

    inline static REBGOB* &VAL_GOB(REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return v->payload.gob.gob;
    }

    inline static REBCNT &VAL_GOB_INDEX(REBCEL *v) {
        assert(CELL_KIND(v) == REB_GOB);
        return v->payload.gob.index;
    }
#endif

inline static REBVAL *Init_Gob(RELVAL *out, REBGOB *g) {
    RESET_CELL(out, REB_GOB);
    VAL_GOB(out) = g;
    VAL_GOB_INDEX(out) = 0;
    return KNOWN(out);
}


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
    cast(REBSPC*, 0) // cast() doesn't like nullptr, fix

#define UNBOUND \
   cast(REBNOD*, 0) // cast() doesn't like nullptr, fix

inline static REBNOD *VAL_BINDING(const REBCEL *v) {
    assert(Is_Bindable(v));
    return v->extra.binding;
}

inline static void INIT_BINDING(RELVAL *v, void *p) {
    assert(Is_Bindable(v)); // works on partially formed values

    REBNOD *binding = cast(REBNOD*, p);
    v->extra.binding = binding;

  #if !defined(NDEBUG)
    if (not binding)
        return; // e.g. UNBOUND

    assert(not (binding->header.bits & NODE_FLAG_CELL)); // not currently used

    if (binding->header.bits & NODE_FLAG_MANAGED) {
        assert(
            binding->header.bits & ARRAY_FLAG_VARLIST // specific
            or binding->header.bits & ARRAY_FLAG_PARAMLIST // relative
            or (
                IS_VARARGS(v) and not IS_SER_DYNAMIC(binding)
            ) // varargs from MAKE VARARGS! [...], else is a varlist
        );
    }
    else {
        // Can only store unmanaged pointers in stack cells (and only if the
        // lifetime of the stack entry is guaranteed to outlive the binding)
        //
        assert(CTX(p));
        if (v->header.bits & NODE_FLAG_TRANSIENT) {
            // let anything go... for now.
            // SERIES_FLAG_STACK might not be set yet due to construction
            // constraints, see Make_Context_For_Action_Int_Partials()
        }
        else {
            assert(v->header.bits & CELL_FLAG_STACK);
            assert(binding->header.bits & SERIES_FLAG_STACK);
        }
    }
  #endif
}

inline static void Move_Value_Header(RELVAL *out, const RELVAL *v)
{
    assert(out != v); // usually a sign of a mistake; not worth supporting
    assert(NOT_END(v)); // SET_END() is the only way to write an end

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


// !!! Because you cannot assign REBVALs to one another (e.g. `*dest = *src`)
// a function is used.  The reason that a function is used is because this
// gives more flexibility in decisions based on the destination cell regarding
// whether it is necessary to reify information in the source cell.
//
// That advanced purpose has not yet been implemented, because it requires
// being able to "sniff" a cell for its lifetime.  For now it only preserves
// the CELL_FLAG_STACK bit, without actually doing anything with it.
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

    if (Not_Bindable(v))
        out->extra = v->extra; // extra isn't a binding (INTEGER! MONEY!...)
    else
        INIT_BINDING_MAY_MANAGE(out, v->extra.binding);

    return KNOWN(out);
}


// When doing something like a COPY of an OBJECT!, the var cells have to be
// handled specially, e.g. by preserving VALUE_FLAG_ENFIXED.
//
// !!! What about other non-copyable properties like CELL_FLAG_PROTECTED?
//
inline static REBVAL *Move_Var(RELVAL *out, const REBVAL *v)
{
    assert(not (out->header.bits & CELL_FLAG_STACK));

    // This special kind of copy can only be done into another object's
    // variable slot. (Since the source may be a FRAME!, v *might* be stack
    // but it should never be relative.  If it's stack, we have to go through
    // the whole potential reification process...double-set header for now.)

    Move_Value(out, v);
    out->header.bits |= (
        v->header.bits & (VALUE_FLAG_ENFIXED | ARG_MARKED_CHECKED)
    );
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
    assert(NOT_END(v));

    ASSERT_CELL_WRITABLE_EVIL_MACRO(out, __FILE__, __LINE__);

    // Examine just the cell's preparation bits.  Are they identical?  If so,
    // we are not losing any information by blindly copying the header in
    // the release build.
    //
    assert(
        (out->header.bits & CELL_MASK_PERSIST)
        == (v->header.bits & CELL_MASK_PERSIST)
    );

    out->header = v->header;
    out->payload = v->payload;
    out->extra = v->extra;
}


// !!! Super primordial experimental `const` feature.  Concept is that various
// operations have to be complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.  const input always
// gets you const output, but mutable input will get you const output if
// the value itself is const (so it inherits).
//
inline static REBVAL *Inherit_Const(REBVAL *out, const RELVAL *influencer) {
    out->header.bits |= (influencer->header.bits & VALUE_FLAG_CONST);
    return out;
}
#define Trust_Const(value) \
    (value) // just a marking to say the const is accounted for already

inline static REBVAL *Const(REBVAL *v) {
    SET_VAL_FLAG(v, VALUE_FLAG_CONST);
    return v;
}


//
// Rather than allow a REBVAL to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
// See CELL_FLAG_STACK for the experimental motivation.  However, even if
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
    REBVAL name##_pair[2]; \
    Prep_Stack_Cell(cast(REBVAL*, &name##_pair)); /* tbd: FS_TOP FRAME! */ \
    REBVAL * const name = cast(REBVAL*, &name##_pair) + 1; \
    Prep_Stack_Cell(name)
