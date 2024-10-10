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
// accessors dereference Cell pointers, the inline functions need the complete
// struct definition available from all the payload types.
//
// See notes in %sys-rebval.h for the definition of the Cell structure.
//
// While some Cells are in C stack variables, most reside in the allocated
// memory block for a Rebol series.  The memory block for a series can be
// resized and require a reallocation, or it may become invalid if the
// containing series is garbage-collected.  This means that many pointers to
// cells are unstable, and could become invalid if arbitrary user code
// is run...this includes values on the data stack, which is implemented as
// a series under the hood.  (See %sys-stack.h)
//
// A cell in a C stack variable does not have to worry about its memory
// address becoming invalid--but by default the garbage collector does not
// know that value exists.  So while the address may be stable, any series
// it has in the payload might go bad.  Use Push_GC_Guard() to protect a
// stack variable's payload, and then Drop_GC_Guard() when the protection
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
// The PROBE macro can be used in debug builds to mold a cell much like the
// Rebol `probe` operation.  But it's actually polymorphic, and if you have
// a Flex*, VarList*, or Array* it can be used with those as well.  In C++,
// you can even get the same value and type out as you put in...just like in
// Rebol, permitting things like `return PROBE(Make_Some_Flex(...));`
//
// In order to make it easier to find out where a piece of debug spew is
// coming from, the file and line number will be output as well.
//
// Note: As a convenience, PROBE also flushes the `stdout` and `stderr` in
// case the debug build was using printf() to output contextual information.
//

#if defined(DEBUG_HAS_PROBE)
    #if CPLUSPLUS_11
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
// In the debug build, Poison cells (NODE_FLAG_FREE) can use their payload to
// store where and when they were initialized.  This also applies to some
// datatypes like BLANK!, VOID!, LOGIC!, or NOTHING--since they only use their
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

    INLINE void Set_Track_Payload_Extra_Debug(
        Cell* c,
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


//=////////////////////////////////////////////////////////////////////////=//
//
//  VALUE "KIND" (1 out of 64 different foundational types)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Every value has 6 bits reserved for its VAL_TYPE().  The reason only 6
// are used is because low-level TYPESET!s are only 64-bits (so they can fit
// into a cell payload, along with a key symbol to represent a function
// parameter).  If there were more types, they couldn't be flagged in a
// typeset that fit in a cell under that constraint.
//
// !!! A full header byte is used, to simplify masking and hopefully offer
// a speedup.  Larger values could be used for some purposes, but they could
// not be put in typesets as written.
//

#define VAL_TYPE_RAW(v) \
    cast(enum Reb_Kind, KIND_BYTE(v))

#define FLAGIT_KIND(t) \
    (cast(REBU64, 1) << (t)) // makes a 64-bit bitflag

#ifdef NDEBUG
    #define VAL_TYPE(v) \
        VAL_TYPE_RAW(v)
#else
    INLINE enum Reb_Kind VAL_TYPE(const Cell* v){
        // VAL_TYPE is called *a lot*, so that makes it a great place to do
        // sanity checks in the debug build.  But a debug build will not
        // inline this function, and makes *no* optimizations.  Using no
        // stack space e.g. no locals) is ideal.  (If -Og "debug" optimization
        // is used, that should actually be able to be fast, since it isn't
        // needing to keep an actual local around to display.)

        if (
            (v->header.bits & (
                NODE_FLAG_CELL
                | NODE_FLAG_FREE
                | CELL_FLAG_FALSEY // all the "bad" types are also falsey
            )) == NODE_FLAG_CELL
        ){
            assert(VAL_TYPE_RAW(v) <= REB_MAX);
            return VAL_TYPE_RAW(v); // majority of calls hopefully return here
        }

        // Could be a LOGIC! false, blank, or NULL bit pattern in bad cell
        //
        if (not (v->header.bits & NODE_FLAG_CELL)) {
            printf("VAL_TYPE() called on non-cell\n");
            panic (v);
        }
        if (v->header.bits & NODE_FLAG_FREE) {
            printf("VAL_TYPE() called on invalid poison cell--marked FREE\n");
            panic (v);
        }

        // Cell is good, so let the good cases pass through
        //
        if (VAL_TYPE_RAW(v) == REB_MAX_NULLED)
            return REB_MAX_NULLED;
        if (VAL_TYPE_RAW(v) == REB_LOGIC)
            return REB_LOGIC;

        // Unreadable blank is signified in the Extra by a negative tick
        //
        if (VAL_TYPE_RAW(v) == REB_BLANK) {
            if (v->extra.tick < 0) {
                printf("VAL_TYPE() called on unreadable BLANK!\n");
              #ifdef DEBUG_COUNT_TICKS
                printf("Was made on tick: %d\n", cast(int, -v->extra.tick));
              #endif
                panic (v);
            }
            return REB_BLANK;
        }

        // Special messages for END and trash (as these are common)
        //
        if (VAL_TYPE_RAW(v) == REB_0_END) {
            printf("VAL_TYPE() called on END marker\n");
            panic (v);
        }

        printf("non-RAW VAL_TYPE() called on pseudotype (or garbage)");
        panic (v);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// const status is ignored when setting or clearing flags in the header.
//

#define Get_Cell_Flag(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) == 0)

#define Set_Cell_Flag(c,name) \
    m_cast(union HeaderUnion*, &(c)->header)->bits |= CELL_FLAG_##name

#define Clear_Cell_Flag(c,name) \
    m_cast(union HeaderUnion*, &(c)->header)->bits &= ~CELL_FLAG_##name


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL WRITABILITY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Asserting writiablity helps avoid very bad catastrophies that might ensue
// if "implicit end markers" could be overwritten.  These are the ENDs that
// are actually other bitflags doing double duty inside a data structure, and
// there is no cell storage backing the position.
//
// (A fringe benefit is catching writes to other unanticipated locations.)
//

#if defined(DEBUG_CELL_WRITABILITY)
    //
    // In the debug build, functions aren't inlined, and the overhead actually
    // adds up very quickly of getting the 3 parameters passed in.  Run the
    // risk of repeating macro arguments to speed up this critical test.
    //
    #define Assert_Cell_Writable(c,file,line) \
        do { \
            STATIC_ASSERT_LVALUE(c);  /* evil macro, ensures used correctly */ \
            if (not ((c)->header.bits & NODE_FLAG_CELL)) { \
                printf("Non-cell passed to cell writing routine\n"); \
                panic_at ((c), (file), (line)); \
            } \
            else if (not ((c)->header.bits & NODE_FLAG_NODE)) { \
                printf("Non-node passed to cell writing routine\n"); \
                panic_at ((c), (file), (line)); \
            } else if (\
                (c)->header.bits & (CELL_FLAG_PROTECTED | NODE_FLAG_FREE) \
            ){ \
                printf("Protected/free cell passed to writing routine\n"); \
                panic_at ((c), (file), (line)); \
            } \
        } while (0)
#else
    #define Assert_Cell_Writable(c,file,line) \
        NOOP
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  CELL HEADERS AND PREPARATION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// RESET_VAL_HEADER clears out the header of *most* bits, setting it to a
// new type.  The type takes up the full "rightmost" byte of the header,
// despite the fact it only needs 6 bits.  However, the performance advantage
// of not needing to mask to do VAL_TYPE() is worth it...also there may be a
// use for 256 types (although type bitsets are only 64-bits at the moment)
//
// The value is expected to already be "pre-formatted" with the NODE_FLAG_CELL
// bit, so that is left as-is.
//

INLINE Value* RESET_VAL_HEADER_EXTRA_Core(
    Cell* v,
    enum Reb_Kind kind,
    uintptr_t extra

  #if defined(DEBUG_CELL_WRITABILITY)
  , const char *file
  , int line
  #endif
){
    Assert_Cell_Writable(v, file, line);

    v->header.bits &= CELL_MASK_PERSIST;
    v->header.bits |= FLAG_KIND_BYTE(kind) | extra;
    return cast(Value*, v);
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
    INLINE Value* RESET_CELL_EXTRA_Debug(
        Cell* out,
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
        return cast(Value*, out);
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
#ifdef DEBUG_MEMORY_ALIGN
    #define Assert_Cell_Aligned(c) \
      do { \
        STATIC_ASSERT_LVALUE(c);  /* evil macro, make sure used safely */ \
        if (i_cast(uintptr_t, (c)) % ALIGN_SIZE != 0) { \
            printf( \
                "Cell address %p not aligned to %d bytes\n", \
                cast(const void*, (c)), \
                cast(int, ALIGN_SIZE) \
            ); \
            panic_at ((c), file, line); \
        } \
      } while (0)
#else
    #define Assert_Cell_Aligned(c)  NOOP
#endif

#define CELL_MASK_ERASE \
    (NODE_FLAG_NODE | NODE_FLAG_CELL)

#define CELL_MASK_ERASE_END \
    (CELL_MASK_ERASE | FLAG_KIND_BYTE(REB_0)) // same, but more explicit

INLINE Cell* Erase_Cell_Core(
    Cell* c

  #if defined(DEBUG_TRACK_CELLS)
  , const char *file
  , int line
  #endif
){
    Assert_Cell_Aligned(c);

    c->header.bits = CELL_MASK_ERASE;
    TRACK_CELL_IF_DEBUG(cast(Cell*, c), file, line);
    return c;
}

#if defined(DEBUG_TRACK_CELLS)
    #define Erase_Cell(c) \
        Erase_Cell_Core((c), __FILE__, __LINE__)
#else
    #define Erase_Cell(c) \
        Erase_Cell_Core(c)
#endif

INLINE bool Is_Cell_Erased(const Cell *cell)
  { return cell->header.bits == CELL_MASK_ERASE; }


INLINE void CHANGE_VAL_TYPE_BITS(Cell* v, enum Reb_Kind kind) {
    //
    // Note: Only use if you are sure the new type payload is in sync with
    // the type and bits (e.g. changing ANY-WORD! to another ANY-WORD!).
    // Otherwise the value-specific flags might be misinterpreted.
    //
    Assert_Cell_Writable(v, __FILE__, __LINE__);
    KIND_BYTE(v) = kind;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  POISON CELLS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Poison mask has NODE_FLAG_CELL but no NODE_FLAG_NODE, so Ensure_Readable()
// will fail, and it is CELL_FLAG_PROTECTED so Ensure_Writable() will fail.
// Nor can it be freshened with Freshen_Cell().  It has to be Erase_Cell()'d.
//
#define CELL_MASK_POISON \
    (NODE_FLAG_CELL | CELL_FLAG_PROTECTED)

INLINE void Poison_Cell_Core(
    Cell* v

    #ifdef DEBUG_TRACK_CELLS
    , const char *file
    , int line
    #endif
){
    v->header.bits = CELL_MASK_POISON;

    TRACK_CELL_IF_DEBUG(v, file, line);
}

#ifdef DEBUG_TRACK_CELLS
    #define Poison_Cell(v) \
        Poison_Cell_Core((v), __FILE__, __LINE__)
#else
    #define Poison_Cell(v) \
        Poison_Cell_Core(v)
#endif

INLINE bool Is_Cell_Poisoned(const Cell* v) {
    assert(v->header.bits & NODE_FLAG_CELL);
    return v->header.bits == CELL_MASK_POISON;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  END marker (not a value type, only writes `struct Reb_Value_Flags`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Historically Rebol arrays were always one value longer than their maximum
// content, and this final slot was used for a cell type called END!.
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
    cast(const Value*, &PG_End_Node) // rebEND is char*, not Value* aligned!

#if defined(DEBUG_TRACK_CELLS) || defined(DEBUG_CELL_WRITABILITY)
    INLINE Value* SET_END_Debug(
        Cell* v

      #if defined(DEBUG_TRACK_CELLS) || defined(DEBUG_CELL_WRITABILITY)
      , const char *file
      , int line
      #endif
    ){
        Assert_Cell_Writable(v, file, line);

        SECOND_BYTE(&v->header) = REB_0_END;  // only line in release build
        v->header.bits |= CELL_FLAG_FALSEY;  // speeds VAL_TYPE_Debug() check

        TRACK_CELL_IF_DEBUG(v, file, line);
        return cast(Value*, v);
    }

    #define SET_END(v) \
        SET_END_Debug((v), __FILE__, __LINE__)
#else
    INLINE Value* SET_END(Cell* v) {
        SECOND_BYTE(&v->header) = REB_0_END;  // needs to be a prepared cell
        return cast(Value*, v);
    }
#endif

#ifdef NDEBUG
    #define IS_END(p) \
        (cast(const Byte*, p)[1] == REB_0_END)
#else
    INLINE bool IS_END_Debug(
        const void *p, // may not have NODE_FLAG_CELL, may be short as 2 bytes
        const char *file,
        int line
    ){
        if (cast(const Byte*, p)[0] & 0x40) { // e.g. NODE_FLAG_FREE
            printf("NOT_END() called on garbage\n");
            panic_at(p, file, line);
        }

        if (cast(const Byte*, p)[1] == REB_0_END)
            return true;

        if (not (cast(const Byte*, p)[0] & 0x01)) { // e.g. NODE_FLAG_CELL
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
// a Node which constitutes their notion of "binding".
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
INLINE bool IS_RELATIVE(const Cell* v) {
    if (Not_Bindable(v) or not v->extra.binding)
        return false; // INTEGER! and other types are inherently "specific"
    return Get_Array_Flag(v->extra.binding, IS_PARAMLIST);
}

#if defined(__cplusplus) && __cplusplus >= 201103L
    //
    // Take special advantage of the fact that C++ can help catch when we are
    // trying to see if a cell is specific or relative (it will always
    // be specific, so the call is likely in error).  In the C build, they
    // are the same type so there will be no error.
    //
    bool IS_RELATIVE(const Value* v);
#endif

#define IS_SPECIFIC(v) \
    cast(bool, not IS_RELATIVE(v))

INLINE REBACT *VAL_RELATIVE(const Cell* v) {
    assert(IS_RELATIVE(v));
    return ACT(v->extra.binding);
}


// When you have a Cell* (e.g. from an Array) that you "know" to be specific,
// the KNOWN macro can be used for that.  Checks to make sure in debug build.
//
// Use for: "invalid conversion from 'Reb_Value*' to 'Reb_Specific_Value*'"

#if !defined(__cplusplus) // poorer protection in C, loses constness
    INLINE Value* KNOWN(const Cell* v) {
        assert(IS_END(v) or IS_SPECIFIC(v));
        return m_cast(Value*, c_cast(Cell*, v));
    }
#else
    INLINE const Value* KNOWN(const Cell* v) {
        assert(IS_END(v) or IS_SPECIFIC(v)); // END for KNOWN(Array_Head()), etc.
        return cast(const Value*, v);
    }

    INLINE Value* KNOWN(Cell* v) {
        assert(IS_END(v) or IS_SPECIFIC(v)); // END for KNOWN(Array_Head()), etc.
        return cast(Value*, v);
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
    c_cast(const Value*, &PG_Nulled_Cell[0])

#define Is_Nulled(v) \
    (VAL_TYPE(v) == REB_MAX_NULLED)

#define Init_Nulled(out) \
    RESET_CELL_EXTRA((out), REB_MAX_NULLED, CELL_FLAG_FALSEY)

#define CELL_FLAG_NULL_IS_ENDISH FLAG_TYPE_SPECIFIC_BIT(0)

// !!! A theory was that the "evaluated" flag would help a function that took
// both ~null~ and <end>, which are converted to nulls, distinguish what kind
// of null it is.  This may or may not be a good idea, but unevaluating it
// here just to make a note of the concept, and tag it via the callsites.
//
#define Init_Endish_Nulled(out) \
    RESET_CELL_EXTRA((out), REB_MAX_NULLED, \
        CELL_FLAG_FALSEY | CELL_FLAG_NULL_IS_ENDISH)

INLINE bool Is_Endish_Nulled(const Cell* v) {
    return Is_Nulled(v) and Get_Cell_Flag(v, NULL_IS_ENDISH);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  NOTHING!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// NOTHING! results are the default for `do []`, and unlike NULL nothing! *is*
// a value...however a somewhat unfriendly one.  While NULLs are falsey,
// nothin is *neither* truthy nor falsey.
//
// NOTHING! also comes into play in the "nothingification" of NULLs.
// Loops wish to reserve NULL as the return result if there is a BREAK, and
// conditionals like IF and SWITCH want to reserve NULL to mean there was no
// branch taken.  So when branches or loop bodies produce null, they need
// to be converted to some ANY-VALUE!.
//

#define NOTHING_VALUE \
    c_cast(const Value*, &PG_Nothing_Value[0])

#define Init_Nothing(out) \
    RESET_CELL((out), REB_NOTHING)

INLINE Value* Nothingify_Branched(Value* cell) {
    if (Is_Nulled(cell) or Is_Void(cell))
        Init_Nothing(cell);
    return cell;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  VOID
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Void is a non-valued type from the future of Ren-C.  It has been lightly
// patched into this old R3C branch, to be the "opt out" case instead of NULL.
//

#define Init_Void(out) \
    RESET_CELL((out), REB_VOID)



//=////////////////////////////////////////////////////////////////////////=//
//
//  BAR!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Was for a time the expression barrier.  That is now COMMA! in modern Ren-C.
//

#define BAR_VALUE \
    c_cast(const Value*, &PG_Bar_Value[0])

#define Init_Bar(out) \
    Init_Word((out), Canon(SYM_BAR_1));

#define Is_Bar(v) \
    (Is_Word(v) and Cell_Word_Id(v) == SYM_BAR_1)


//=////////////////////////////////////////////////////////////////////////=//
//
//  BLANK!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Blank! values are sort of array's equivalent to "space" in strings.
//
// Historically they were falsey, but have been reconsidered in modern Ren-C
// and are no longer special in that way:
//
// https://forum.rebol.info/t/blank-2022-revisiting-the-datatype/1942
//
// They are kept falsey in this historical branch in order to be backwards
// compatible for bootstrap purposes, so that this codebase can be built
// either with itself or with r3-8994d23.
//
// In the debug build, it is possible to make an "unreadable" blank!.  This
// will behave neutrally as far as the garbage collector is concerned, so
// it can be used as a placeholder for a value that will be filled in at
// some later time--spanning an evaluation.  But if the special IS_UNREADABLE
// checks are not used, it will not respond to Is_Blank() and will also
// refuse VAL_TYPE() checks.  This is useful anytime a placeholder is needed
// in a slot temporarily where the code knows it's supposed to come back and
// fill in the correct thing later...where the asserts serve as a reminder
// if that fill in never happens.
//

#define BLANK_VALUE \
    c_cast(const Value*, &PG_Blank_Value[0])

#define Init_Blank(v) \
    RESET_CELL_EXTRA((v), REB_BLANK, CELL_FLAG_FALSEY)

#ifdef DEBUG_UNREADABLE_BLANKS
    INLINE Value* Init_Unreadable_Debug(
        Cell* out, const char *file, int line
    ){
        RESET_CELL_EXTRA_Debug(out, REB_BLANK, CELL_FLAG_FALSEY, file, line);
        assert(out->extra.tick > 0);
        out->extra.tick = -out->extra.tick;
        return KNOWN(out);
    }

    #define Init_Unreadable(out) \
        Init_Unreadable_Debug((out), __FILE__, __LINE__)

    INLINE bool IS_BLANK_RAW(const Cell* v) {
        return VAL_TYPE_RAW(v) == REB_BLANK;
    }

    INLINE bool Is_Unreadable_Debug(const Cell* v) {
        if (VAL_TYPE_RAW(v) != REB_BLANK)
            return false;
        return v->extra.tick < 0;
    }

    #define Assert_Unreadable_If_Debug(v) \
        assert(Is_Unreadable_Debug(v))

    #define Assert_Readable_If_Debug(v) \
        assert(not Is_Unreadable_Debug(v))
#else
    #define Init_Unreadable(v) \
        Init_Blank(v)

    #define IS_BLANK_RAW(v) \
        Is_Blank(v)

    #define Assert_Unreadable_If_Debug(v) \
        assert(Is_Blank(v)) // would have to be a blank even if not unreadable

    #define Assert_Readable_If_Debug(v) \
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
// that a single check can test for both BLANK! and logic false.
//
// Conditional truth and falsehood allows an interpretation where a BLANK!
// is a "falsey" value as well.
//

#define FALSE_VALUE \
    c_cast(const Value*, &PG_False_Value[0])

#define TRUE_VALUE \
    c_cast(const Value*, &PG_True_Value[0])

INLINE void FAIL_IF_ERROR(const Cell* c);

INLINE bool IS_TRUTHY(const Cell* v) {
    if (Get_Cell_Flag(v, FALSEY))
        return false;
    if (Is_Void(v))
        fail (Error_Void_Conditional_Raw());
    FAIL_IF_ERROR(v);  // approximate definitional errors...
    return true;
}

#define IS_FALSEY(v) \
    (not IS_TRUTHY(v))


#define Init_Logic(out,b) \
    RESET_CELL_EXTRA((out), REB_LOGIC, (b) ? 0 : CELL_FLAG_FALSEY)

#define Init_True(out) \
    Init_Logic((out), true)

#define Init_False(out) \
    Init_Logic((out), false)

INLINE bool VAL_LOGIC(const Cell* v) {
    assert(Is_Logic(v));
    return Not_Cell_Flag((v), FALSEY);
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

INLINE Value* Init_Char(Cell* out, Ucs2Unit uni) {
    RESET_CELL(out, REB_CHAR);
    VAL_CHAR(out) = uni;
    return cast(Value*, out);
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

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_INT64(v) \
        ((v)->payload.integer)
#else
    // allows an assert, but also lvalue: `VAL_INT64(v) = xxx`
    //
    INLINE REBI64 & VAL_INT64(Cell* v) { // C++ reference type
        assert(Is_Integer(v));
        return v->payload.integer;
    }
    INLINE REBI64 VAL_INT64(const Cell* v) {
        assert(Is_Integer(v));
        return v->payload.integer;
    }
#endif

INLINE Value* Init_Integer(Cell* out, REBI64 i64) {
    RESET_CELL(out, REB_INTEGER);
    out->payload.integer = i64;
    return cast(Value*, out);
}

INLINE int32_t VAL_INT32(const Cell* v) {
    if (VAL_INT64(v) > INT32_MAX or VAL_INT64(v) < INT32_MIN)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(int32_t, VAL_INT64(v));
}

INLINE uint32_t VAL_UINT32(const Cell* v) {
    if (VAL_INT64(v) < 0 or VAL_INT64(v) > UINT32_MAX)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(uint32_t, VAL_INT64(v));
}

INLINE Byte VAL_UINT8(const Cell* v) {
    if (VAL_INT64(v) > 255 or VAL_INT64(v) < 0)
        fail (Error_Out_Of_Range(KNOWN(v)));
    return cast(Byte, VAL_INT32(v));
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

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_DECIMAL(v) \
        ((v)->payload.decimal)
#else
    // allows an assert, but also lvalue: `VAL_DECIMAL(v) = xxx`
    //
    INLINE REBDEC & VAL_DECIMAL(Cell* v) { // C++ reference type
        assert(Is_Decimal(v) or Is_Percent(v));
        return v->payload.decimal;
    }
    INLINE REBDEC VAL_DECIMAL(const Cell* v) {
        assert(Is_Decimal(v) or Is_Percent(v));
        return v->payload.decimal;
    }
#endif

INLINE Value* Init_Decimal(Cell* out, REBDEC d) {
    RESET_CELL(out, REB_DECIMAL);
    out->payload.decimal = d;
    return cast(Value*, out);
}

INLINE Value* Init_Percent(Cell* out, REBDEC d) {
    RESET_CELL(out, REB_PERCENT);
    out->payload.decimal = d;
    return cast(Value*, out);
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

INLINE deci VAL_MONEY_AMOUNT(const Cell* v) {
    deci amount;
    amount.m0 = v->extra.m0;
    amount.m1 = v->payload.money.m1;
    amount.m2 = v->payload.money.m2;
    amount.s = v->payload.money.s;
    amount.e = v->payload.money.e;
    return amount;
}

INLINE Value* Init_Money(Cell* out, deci amount) {
    RESET_CELL(out, REB_MONEY);
    out->extra.m0 = amount.m0;
    out->payload.money.m1 = amount.m1;
    out->payload.money.m2 = amount.m2;
    out->payload.money.s = amount.s;
    out->payload.money.e = amount.e;
    return cast(Value*, out);
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

#if (! CPLUSPLUS_11)
    #define VAL_TUPLE(v) \
        ((v)->payload.tuple.tuple + 1)

    #define VAL_TUPLE_DATA(v) \
        ((v)->payload.tuple.tuple)

    #define VAL_TUPLE_LEN(v) \
        ((v)->payload.tuple.tuple[0])
#else
    // C++ build can give const-correctness so you don't change read-only data

    INLINE const Byte *VAL_TUPLE(const Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple + 1;
    }

    INLINE Byte *VAL_TUPLE(Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple + 1;
    }

    INLINE const Byte *VAL_TUPLE_DATA(const Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple;
    }

    INLINE Byte *VAL_TUPLE_DATA(Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple;
    }

    INLINE Byte VAL_TUPLE_LEN(const Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple[0];
    }

    INLINE Byte &VAL_TUPLE_LEN(Cell* v) {
        assert(Is_Tuple(v));
        return v->payload.tuple.tuple[0];
    }
#endif


INLINE Value* Init_Tuple(Cell* out, const Byte *data) {
    RESET_CELL(out, REB_TUPLE);
    memcpy(VAL_TUPLE_DATA(out), data, sizeof(out->payload.tuple.tuple));
    return cast(Value*, out);
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  EVENT!
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Rebol's events are used for the GUI and for network and I/O.  They are
// essentially just a union of some structures which are packed so they can
// fit into a cell's payload size.
//
// The available event models are:
//
// * EVM_PORT
// * EVM_OBJECT
// * EVM_DEVICE
// * EVM_CALLBACK
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

#define VAL_EVENT_FLEX(v) \
    ((v)->extra.eventee.flex)

#define IS_EVENT_MODEL(v,f) \
    (VAL_EVENT_MODEL(v) == (f))

INLINE void SET_EVENT_INFO(
    Cell* val,
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

INLINE void SET_EVENT_XY(Cell* v, REBINT x, REBINT y) {
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

INLINE void SET_EVENT_KEY(Cell* v, REBLEN k, REBLEN c) {
    VAL_EVENT_DATA(v) = ((c << 16) + k);
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINDING
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a Node which constitutes their notion of "binding".
//
// This can either be null (a.k.a. UNBOUND), or to a function's paramlist
// (indicates a relative binding), or to a context's varlist (which indicates
// a specific binding.)
//
// NOTE: Instead of using null for UNBOUND, a special global StubStruct was
// experimented with.  It was at a location in memory known at compile time,
// and it had its ->header and ->info bits set in such a way as to avoid the
// need for some conditional checks.  e.g. instead of writing:
//
//     if (binding and binding->leader.bits & NODE_FLAG_MANAGED) {...}
//
// The special UNBOUND node set some bits, such as to pretend to be managed:
//
//     if (binding->leader.bits & NODE_FLAG_MANAGED) {...} // incl. UNBOUND
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
    cast(Specifier*, nullptr)

#define UNBOUND \
   cast(Stub*, nullptr)

INLINE Stub* VAL_BINDING(const Cell* v) {
    assert(Is_Bindable(v));
    return v->extra.binding;
}

INLINE void INIT_BINDING(Cell* v, Stub* binding) {
    assert(Is_Bindable(v)); // works on partially formed values

    v->extra.binding = binding;

  #if !defined(NDEBUG)
    if (not binding)
        return; // e.g. UNBOUND

    assert(not (binding->leader.bits & NODE_FLAG_CELL)); // not currently used

    if (binding->leader.bits & NODE_FLAG_MANAGED) {
        assert(
            binding->leader.bits & ARRAY_FLAG_IS_VARLIST // specific
            or binding->leader.bits & ARRAY_FLAG_IS_PARAMLIST // relative
            or (
                Is_Varargs(v) and not Is_Flex_Dynamic(binding)
            ) // varargs from MAKE VARARGS! [...], else is a varlist
        );
    }
    else {
        // Can only store unmanaged pointers in stack cells (and only if the
        // lifetime of the stack entry is guaranteed to outlive the binding)
        //
        assert(CTX(binding));
    }
  #endif
}

INLINE void Move_Value_Header(Cell* out, const Cell* v)
{
    assert(out != v); // usually a sign of a mistake; not worth supporting
    assert(NOT_END(v)); // SET_END() is the only way to write an end
    assert(VAL_TYPE_RAW(v) <= REB_MAX_NULLED); // don't move pseudotypes

    Assert_Cell_Writable(out, __FILE__, __LINE__);

    out->header.bits &= CELL_MASK_PERSIST;
    out->header.bits |= v->header.bits & CELL_MASK_COPY;

  #ifdef DEBUG_TRACK_EXTEND_CELLS
    out->track = v->track;
    out->tick = v->tick; // initialization tick
    out->touch = v->touch; // arbitrary debugging use via TOUCH_CELL
  #endif
}


// If the cell we're writing into is a stack cell, there's a chance that
// management/reification of the binding can be avoided.
//
INLINE void INIT_BINDING_MAY_MANAGE(Cell* out, Stub* binding) {
    if (not binding) {
        out->extra.binding = nullptr; // unbound
        return;
    }
    if (Is_Node_Managed(binding)) {
        out->extra.binding = binding; // managed is safe for any `out`
        return;
    }

    Level* L = LVL(LINK(binding).keysource);
    assert(IS_END(L->param)); // cannot manage frame varlist in mid fulfill!
    UNUSED(L); // !!! not actually used yet, coming soon

    binding->leader.bits |= NODE_FLAG_MANAGED; // burdens the GC, now...
    out->extra.binding = binding;
}


// !!! Because you cannot assign REBVALs to one another (e.g. `*dest = *src`)
// a function is used.  The reason that a function is used is because this
// gives more flexibility in decisions based on the destination cell.
//
INLINE Value* Copy_Cell(Cell* out, const Value* v)
{
    Move_Value_Header(out, v);

    if (Not_Bindable(v))
        out->extra = v->extra; // extra isn't a binding (INTEGER! MONEY!...)
    else
        INIT_BINDING_MAY_MANAGE(out, v->extra.binding);

    out->payload = v->payload; // payloads cannot hold references to stackvars
    return KNOWN(out);
}


// When doing something like a COPY of an OBJECT!, the var cells have to be
// handled specially, e.g. by preserving the checked status.
//
// !!! What about other non-copyable properties like CELL_FLAG_PROTECTED?
//
INLINE Value* Move_Var(Cell* out, const Value* v)
{
    // This special kind of copy can only be done into another object's
    // variable slot. (Since the source may be a FRAME!, v *might* be stack
    // but it should never be relative.  If it's stack, we have to go through
    // the whole potential reification process...double-set header for now.)

    Copy_Cell(out, v);
    out->header.bits |= (v->header.bits & CELL_FLAG_ARG_MARKED_CHECKED);
    return KNOWN(out);
}


// Generally speaking, you cannot take a Cell from one cell and copy it
// blindly into another...it needs to be Derelativize()'d.  This routine is
// for the rare cases where it's legal, e.g. shuffling a cell from one place
// in an array to another cell in the same array.
//
INLINE void Blit_Cell(Cell* out, const Cell* v)
{
    assert(out != v); // usually a sign of a mistake; not worth supporting
    assert(NOT_END(v));

    Assert_Cell_Writable(out, __FILE__, __LINE__);

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


//
// Rather than allow a cell to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
//
// Note: because this will run instructions, a routine should avoid doing a
// DECLARE_VALUE inside of a loop.  It should be at the outermost scope of
// the function.
//
// Note: It sets NODE_FLAG_FREE, so this is a "trash" cell by default.
//
#define DECLARE_VALUE(name) \
    Value name##_pair[2]; \
    Erase_Cell(cast(Value*, &name##_pair)); \
    Value* const name = cast(Value*, &name##_pair) + 1; \
    Erase_Cell(name)

#define DECLARE_ELEMENT(name) /* compatibility w/modern EXE synonym */ \
    Value name##_pair[2]; \
    Erase_Cell(cast(Value*, &name##_pair)); \
    Value* const name = cast(Value*, &name##_pair) + 1; \
    Erase_Cell(name)
