//
//  File: %sys-value.h
//  Summary: {any-value! defs AFTER %tmp-internals.h (see: %sys-rebval.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2020 Ren-C Open Source Contributors
// Copyright 2012 REBOL Technologies
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
// memory block for a Rebol array.  The memory block for an array can be
// resized and require a reallocation, or it may become invalid if the
// containing series is garbage-collected.  This means that many pointers to
// REBVAL are unstable, and could become invalid if arbitrary user code
// is run...this includes values on the data stack, which is implemented as
// an array under the hood.  (See %sys-stack.h)
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

#if DEBUG_HAS_PROBE
    #if CPLUSPLUS_11
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
// Asserting writablity helps catch writes to unanticipated locations.  Making
// places that are cell-adjacent not carry NODE_FLAG_CELL, or be marked with
// CELL_FLAG_STALE, helps make this check useful.
//

#if DEBUG_CELL_WRITABILITY
    //
    // In the debug build, functions aren't inlined, and the overhead actually
    // adds up very quickly.  Run the risk of repeating macro arguments to
    // speed up this critical test, then wrap in READABLE() and WRITABLE()
    // functions for higher-level callers that don't mind the cost.
    //
    // Note this isn't in a `do {...} while (0)`.  Slight chance it helps in
    // some debug builds.  Just a further detail callers should bear in mind.

    #define ASSERT_CELL_READABLE_EVIL_MACRO(c) \
        if ( \
            (FIRST_BYTE((c)->header) & ( \
                NODE_BYTEMASK_0x01_CELL | NODE_BYTEMASK_0x80_NODE \
                    | NODE_BYTEMASK_0x40_FREE \
            )) != 0x81 \
        ){ \
            if (not ((c)->header.bits & NODE_FLAG_CELL)) \
                printf("Non-cell passed to cell read routine\n"); \
            else if (not ((c)->header.bits & NODE_FLAG_NODE)) \
                printf("Non-node passed to cell read routine\n"); \
            else \
                printf( \
                    "ASSERT_CELL_READABLE() on CELL_FLAG_STALE cell\n" \
                    "Maybe valid but just has access to it limited\n" \
                    "see Mark_Eval_Out_Stale()\n" \
                ); \
            panic (c); \
        }

    // Because you can't write to cells that aren't pure 0 (prep'd/calloc'd)
    // or formatted with cell and node flags, it doesn't make sense to
    // disallow writing CELL_FLAG_STALE cells.  So the flag is used instead as
    // a generic "not readable" status.
    //
    // Warning: Cells with CELL_FLAG_STALE may seem like UTF-8 strings if they
    // leak to the user; so this flag should only be used for internal states.

    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c) \
        if ( \
            (FIRST_BYTE((c)->header) & ( \
                NODE_BYTEMASK_0x01_CELL | NODE_BYTEMASK_0x80_NODE \
                    | CELL_FLAG_PROTECTED \
            )) != 0x81 \
        ){ \
            if (not ((c)->header.bits & NODE_FLAG_CELL)) \
                printf("Non-cell passed to cell write routine\n"); \
            else if (not ((c)->header.bits & NODE_FLAG_NODE)) \
                printf("Non-node passed to cell write routine\n"); \
            else \
                printf("Protected cell passed to writing routine\n"); \
            panic (c); \
        } \

    inline static REBCEL(const*) READABLE(REBCEL(const*) c) {
        ASSERT_CELL_READABLE_EVIL_MACRO(c);
        return c;
    }

    inline static RELVAL *WRITABLE(RELVAL *c) {
        ASSERT_CELL_WRITABLE_EVIL_MACRO(c);
        return c;
    }

    // We try to make the concept of a "prepped" cell allow for that to be
    // fully zeroed out memory.  But you shouldn't be setting cell flags on
    // that.  Make a separate category (initable) that includes prep cells.
    //
    #define ASSERT_CELL_INITABLE_EVIL_MACRO(c) \
        if ((c)->header.bits != CELL_MASK_PREP) { \
            ASSERT_CELL_WRITABLE_EVIL_MACRO(c); \
        }

    inline static RELVAL *INITABLE(RELVAL *c) {
        ASSERT_CELL_INITABLE_EVIL_MACRO(c);
        return c;
    }
#else
    #define ASSERT_CELL_READABLE_EVIL_MACRO(c)    NOOP
    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c)    NOOP
    #define ASSERT_CELL_INITABLE_EVIL_MACRO(c)    NOOP

    #define READABLE(c) (c)
    #define WRITABLE(c) (c)
    #define INITABLE(c) (c)
#endif


// Note: If incoming p is mutable, we currently assume that's allowed by the
// flag bits of the node.  This could have a runtime check in debug build
// with a C++ variation that only takes mutable pointers.
//
inline static void INIT_VAL_NODE1(RELVAL *v, option(const REBNOD*) node) {
    assert(v->header.bits & CELL_FLAG_FIRST_IS_NODE);
    PAYLOAD(Any, v).first.node = try_unwrap(node);
}

inline static void INIT_VAL_NODE2(RELVAL *v, option(const REBNOD*) node) {
    assert(v->header.bits & CELL_FLAG_SECOND_IS_NODE);
    PAYLOAD(Any, v).second.node = try_unwrap(node);
}

#define VAL_NODE1(v) \
    m_cast(REBNOD*, PAYLOAD(Any, (v)).first.node)

#define VAL_NODE2(v) \
    m_cast(REBNOD*, PAYLOAD(Any, (v)).second.node)


//=//// "KIND3Q" HEADER BYTE [REB_XXX + (n * REB_64)] /////////////////////=//
//
// See KIND3Q_BYTE_UNCHECKED in %sys-rebval.h for explanation.

#if defined(NDEBUG)
    #define KIND3Q_BYTE KIND3Q_BYTE_UNCHECKED
#else
    inline static REBYTE KIND3Q_BYTE_Debug(
        const RELVAL *v,  // can't be used on REBCEL(const*)
        const char *file,
        int line
    ){
        if (
            (v->header.bits & (
                NODE_FLAG_NODE
                | NODE_FLAG_CELL
                | CELL_FLAG_STALE
            )) == (NODE_FLAG_CELL | NODE_FLAG_NODE)
        ){
            return KIND3Q_BYTE_UNCHECKED(v);  // majority return here
        }

        if (
            (v->header.bits & CELL_FLAG_STALE)
            and KIND3Q_BYTE_UNCHECKED(v) == REB_BAD_WORD
        ){
            printf("KIND3Q_BYTE() called on unreadable cell\n");
          #if DEBUG_TRACK_EXTEND_CELLS
            printf("Made on tick: %d\n", cast(int, v->tick));
          #endif
            panic_at (v, file, line);
        }

        // Non-cells are allowed to signal REB_END; see Init_Endlike_Header.
        // (We should not be seeing any rebEND signals here, because we have
        // a REBVAL*, and rebEND is a 2-byte character string that can be
        // at any alignment...not necessarily that of a Reb_Header union!)
        //
        if (KIND3Q_BYTE_UNCHECKED(v) == REB_0_END)
            if (v->header.bits & NODE_FLAG_NODE)
                return REB_0_END;

        if (not (v->header.bits & NODE_FLAG_CELL)) {
            printf("KIND3Q_BYTE() called on non-cell\n");
            panic_at (v, file, line);
        }
        if (v->header.bits & CELL_FLAG_STALE) {
            printf(
                "KIND3Q_BYTE() called on cell with CELL_FLAG_STALE\n"
                "It may be valid but just having access to it limited\n"
                "see Mark_Eval_Out_Stale()\n"
            );
            panic_at (v, file, line);
        }
        return KIND3Q_BYTE_UNCHECKED(v);
    }

    #define KIND3Q_BYTE(v) \
        KIND3Q_BYTE_Debug((v), __FILE__, __LINE__)
#endif

// Note: Only change bits of existing cells if the new type payload matches
// the type and bits (e.g. ANY-WORD! to another ANY-WORD!).  Otherwise the
// value-specific flags might be misinterpreted.
//
#define mutable_KIND3Q_BYTE(v) \
    mutable_SECOND_BYTE(WRITABLE(v)->header)


#define CELL_KIND_UNCHECKED(cell) \
    cast(enum Reb_Kind, KIND3Q_BYTE_UNCHECKED(cell) % REB_64)

#define CELL_HEART_UNCHECKED(cell) \
    cast(enum Reb_Kind, HEART_BYTE(cell))


#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define CELL_KIND CELL_KIND_UNCHECKED
    #define CELL_HEART CELL_HEART_UNCHECKED
#else
    inline static enum Reb_Kind CELL_KIND(REBCEL(const*) cell) {
        assert(HEART_BYTE(cell) != REB_QUOTED);
        return CELL_KIND_UNCHECKED(cell);
    }

    inline static enum Reb_Kind CELL_HEART(REBCEL(const*) cell) {
        assert(HEART_BYTE(cell) != REB_QUOTED);
        return CELL_HEART_UNCHECKED(cell);
    }

    // We want to disable asking for low level implementation details on a
    // cell that may be a REB_QUOTED; you have to call VAL_UNESCAPED() first.
    //
    inline static enum Reb_Kind CELL_KIND(const RELVAL *v) = delete;
    inline static enum Reb_Kind CELL_HEART(const RELVAL *v) = delete;
#endif

inline static const REBTYP *CELL_CUSTOM_TYPE(REBCEL(const*) v) {
    assert(CELL_KIND(v) == REB_CUSTOM);
    return cast(const REBBIN*, EXTRA(Any, v).node);
}

// Sometimes you have a REBCEL* and need to pass a REBVAL* to something.  It
// doesn't seem there's too much bad that can happen if you do; you'll get
// back something that might be quoted up to 3 levels...if it's an escaped
// cell then it won't be quoted at all.  Main thing to know is that you don't
// necessarily get the original value you had back.
//
inline static const RELVAL* CELL_TO_VAL(REBCEL(const*) cell)
  { return cast(const RELVAL*, cell); }

#if CPLUSPLUS_11
    inline static const RELVAL* CELL_TO_VAL(const RELVAL* cell) = delete;
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
        if (KIND3Q_BYTE(v) >= REB_64)
            return REB_QUOTED;
        return cast(enum Reb_Kind, KIND3Q_BYTE(v));
    }
#else
    inline static enum Reb_Kind VAL_TYPE_Debug(
        const RELVAL *v,
        const char *file,
        int line
    ){
        REBYTE kind_byte = KIND3Q_BYTE(v);

        // Special messages for END and trash (as these are common)
        //
        if (kind_byte == REB_0_END) {
            printf("VAL_TYPE() on END marker (use IS_END() or KIND3Q_BYTE())\n");
            panic_at (v, file, line);
        }
        if (kind_byte % REB_64 >= REB_MAX) {
            printf("VAL_TYPE() on pseudotype/garbage (use KIND3Q_BYTE())\n");
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
    (WRITABLE(v)->header.bits |= CELL_FLAG_##name)

#define GET_CELL_FLAG(v,name) \
    ((READABLE(v)->header.bits & CELL_FLAG_##name) != 0)

#define CLEAR_CELL_FLAG(v,name) \
    (WRITABLE(v)->header.bits &= ~CELL_FLAG_##name)

#define NOT_CELL_FLAG(v,name) \
    ((READABLE(v)->header.bits & CELL_FLAG_##name) == 0)

// CELL_FLAG_STALE is special; we use it for things like debug build indicating
// array termination.  It doesn't make an empty/prepped cell non-empty.  We
// use the WRITABLE() test for reading as well as setting the flag, and just
// in case the cell is CELL_MASK_PREP we add NODE_FLAG_NODE and NODE_FLAG_CELL.

#define SET_CELL_FREE(v) \
    (INITABLE(v)->header.bits |= \
        (NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_STALE))

#define IS_CELL_FREE(v) \
    (INITABLE(m_cast(RELVAL*, cast(const RELVAL*, (v))))->header.bits \
        & CELL_FLAG_STALE)


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//

inline static RELVAL *RESET_Untracked(RELVAL *v) {
    ASSERT_CELL_INITABLE_EVIL_MACRO(v);  // header may be CELL_MASK_PREP, all 0
    v->header.bits &= CELL_MASK_PERSIST;
    return v;
}

#if CPLUSPLUS_11
    inline static REBVAL *RESET_Untracked(REBVAL *v)
      { return cast(REBVAL*, RESET_Untracked(cast(RELVAL*, v))); }
#endif

#define RESET(v) \
    TRACK(RESET_Untracked(v))  // TRACK expects REB_0, so call *after* reset

inline static void Init_Cell_Header_Untracked(
    RELVAL *v,
    enum Reb_Kind k,
    uintptr_t extra
){
  #if !defined(NDEBUG)
    if (not Is_Fresh(v))
        panic (v);
  #endif
    v->header.bits &= CELL_MASK_PERSIST;
    v->header.bits |= NODE_FLAG_NODE | NODE_FLAG_CELL  // must ensure NODE+CELL
        | FLAG_KIND3Q_BYTE(k) | FLAG_HEART_BYTE(k) | extra;

    // Don't return a value to help convey the cell is likely incomplete
}

#define Reset_Cell_Header_Untracked(v,k,extra) \
    Init_Cell_Header_Untracked(RESET_Untracked(v), (k), (extra))

inline static REBVAL *RESET_CUSTOM_CELL(
    RELVAL *out,
    REBTYP *type,
    REBFLGS flags
){
    Reset_Cell_Header_Untracked(out, REB_CUSTOM, flags);
    EXTRA(Any, out).node = type;
    return cast(REBVAL*, out);
}


// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
// This is another case where the debug build doesn't inline functions.
// Run the risk of repeating macro args to speed up this critical check.
//
#if (! DEBUG_MEMORY_ALIGN)
    #define ALIGN_CHECK_CELL_EVIL_MACRO(c)    NOOP
#else
    #define ALIGN_CHECK_CELL_EVIL_MACRO(c) \
        if (cast(uintptr_t, (c)) % ALIGN_SIZE != 0) { \
            printf( \
                "Cell address %p not aligned to %d bytes\n", \
                cast(const void*, (c)), \
                cast(int, ALIGN_SIZE) \
            ); \
            panic (c); \
        }
#endif


inline static RELVAL *Prep_Cell_Untracked(RELVAL *c) {
    ALIGN_CHECK_CELL_EVIL_MACRO(c);
    c->header.bits = CELL_MASK_PREP;
    return c;
}

#define Prep_Cell(c) \
    TRACK(Prep_Cell_Untracked(c))  // TRACK() expects REB_0, so track *after*


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
inline static bool IS_RELATIVE(const RELVAL *v) {
    if (not Is_Bindable(v))
        return false;  // may use extra for non-GC-marked uintptr_t-size data

    REBSER *binding = BINDING(v);
    if (not binding)
        return false;  // INTEGER! and other types are inherently "specific"

    if (not IS_SER_ARRAY(binding))
        return false;

    return IS_DETAILS(binding);  // action
}

#if CPLUSPLUS_11
    bool IS_RELATIVE(const REBVAL *v) = delete;  // error on superfluous check
#endif

#define IS_SPECIFIC(v) \
    (not IS_RELATIVE(v))


// When you have a RELVAL* (e.g. from a REBARR) that you KNOW to be specific,
// you might be bothered by an error like:
//
//     "invalid conversion from 'Reb_Value*' to 'Reb_Specific_Value*'"
//
// You can use SPECIFIC to cast it if you are *sure* that it has been
// derelativized -or- is a value type that doesn't have a specifier (e.g. an
// integer).  If the value is actually relative, this will assert at runtime!
//
// Because SPECIFIC has cost in the debug build, there may be situations where
// one is sure that the value is specific, and `cast(REBVAL*, v`) is a better
// choice for efficiency.  This applies to things like `Copy_Cell()`, which
// is called often and already knew its input was a REBVAL* to start with.
//
// Also, if you are enumerating an array of items you "know to be specific"
// then you have to worry about if the array is empty:
//
//     REBVAL *head = SPECIFIC(ARR_HEAD(a));  // !!! a might be tail!
//

inline static REBVAL *SPECIFIC(const_if_c RELVAL *v) {
    assert(IS_SPECIFIC(v));
    return m_cast(REBVAL*, cast(const REBVAL*, v));
}

#if CPLUSPLUS_11
    inline static const REBVAL *SPECIFIC(const RELVAL *v) {
        assert(IS_SPECIFIC(v));
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

#define UNBOUND nullptr  // not always a REBNOD* (sometimes REBCTX)
#define UNSPECIFIED nullptr


inline static const REBSYM *VAL_SYMBOL(REBCEL(const*) cell) {
    assert(ANY_WORD_KIND(CELL_HEART(cell)) or CELL_KIND(cell) == REB_SYMBOL);
    return SYM(VAL_NODE1(cell));
}

inline static void INIT_VAL_WORD_SYMBOL(RELVAL *v, const REBSYM *symbol)
  { INIT_VAL_NODE1(v, symbol); }

inline static const REBSYM *VAL_WORD_SYMBOL(REBCEL(const*) cell) {
    assert(ANY_WORD_KIND(CELL_HEART(cell)));
    return SYM(VAL_NODE1(cell));
}

#define INDEX_PATCHED 1  // Make it easier to find patch (LET) index settings

// In order to signal that something is bound a module, we use the largest
// binding index possible.  Being nonzero means that answers that find the
// position won't confuse it with 0, and so 0 is saved for the unbound state.
//
#define INDEX_ATTACHED ((1 << 20) - 1)

#define VAL_WORD_INDEX_U32(v)         PAYLOAD(Any, (v)).second.u32


inline static void Copy_Cell_Header(
    RELVAL *out,
    const RELVAL *v
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    assert(KIND3Q_BYTE_UNCHECKED(v) != REB_0_END);  // faster than NOT_END()

    ASSERT_CELL_INITABLE_EVIL_MACRO(out);  // may be CELL_MASK_PREP, all 0

    out->header.bits &= CELL_MASK_PERSIST;
    out->header.bits |= NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & CELL_MASK_COPY);

  #if DEBUG_TRACK_EXTEND_CELLS
    out->file = v->file;
    out->line = v->line;
    out->tick = TG_Tick;  // initialization tick
    out->touch = v->touch;  // arbitrary debugging use via TOUCH_CELL
  #endif
}


// Because you cannot assign REBVALs to one another (e.g. `*dest = *src`)
// a function is used.  This provides an opportunity to check things like
// moving data into protected locations, and to mask out bits that should
// not be propagated.
//
// Interface designed to line up with Derelativize()
//
inline static RELVAL *Copy_Cell_Untracked(
    RELVAL *out,
    const RELVAL *v,
    REBFLGS copy_mask  // typically you don't copy UNEVALUATED, PROTECTED, etc
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    assert(KIND3Q_BYTE_UNCHECKED(v) != REB_0_END);  // faster than NOT_END()

    RESET_Untracked(out);

    // Q: Will optimizer notice if copy mask is CELL_MASK_ALL, and not bother
    // with masking out CELL_MASK_PERSIST since all bits are overwritten?
    //
    out->header.bits &= CELL_MASK_PERSIST;
    out->header.bits |= NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & copy_mask);

    // Note: must be copied over *before* INIT_BINDING_MAY_MANAGE is called,
    // so that if it's a REB_QUOTED it can find the literal->cell.
    //
    out->payload = v->payload;

    if (Is_Bindable(v))  // extra is either a binding or a plain C value/ptr
        INIT_BINDING_MAY_MANAGE(out, BINDING(v));
    else
        out->extra = v->extra;  // extra inert bits

    if (IS_RELATIVE(v)) {
        //
        // You shouldn't be getting relative values out of cells that are
        // actually API handles.
        //
        assert(not (v->header.bits & NODE_FLAG_ROOT));

        // However, you should not write relative bits into API destinations,
        // not even hypothetically.  The target should not be an API cell.
        //
        assert(not (out->header.bits & (NODE_FLAG_ROOT | NODE_FLAG_MANAGED)));
    }

    return out;
}

#if CPLUSPLUS_11  // REBVAL and RELVAL are checked distinctly
    inline static REBVAL *Copy_Cell_Untracked(
        RELVAL *out,
        const REBVAL *v,
        REBFLGS copy_mask
    ){
        return cast(REBVAL*, Copy_Cell_Untracked(
            out,
            cast(const RELVAL*, v),
            copy_mask
        ));
    }

    inline static REBVAL *Copy_Cell_Untracked(
        REBVAL *out,
        const REBVAL *v,
        REBFLGS copy_mask
    ){
        return cast(REBVAL*, Copy_Cell_Untracked(
            cast(RELVAL*, out),
            cast(const RELVAL*, v),
            copy_mask
        ));
    }

    inline static RELVAL *Copy_Cell_Untracked(
        REBVAL *out,
        const RELVAL *v,
        REBFLGS copy_mask
    ) = delete;
#endif

#define Copy_Cell(out,v) \
    Copy_Cell_Untracked(TRACK(out), (v), CELL_MASK_COPY)

#define Copy_Cell_Core(out,v,copy_mask) \
    Copy_Cell_Untracked(TRACK(out), (v), (copy_mask))


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
// !!! REBVAL on the C stack cannot be preserved in stackless, and they also
// cannot have their cell RESET() when a fail() occurs.  This means these are
// likely to be converted to API allocated cells.
//
#define DECLARE_LOCAL(name) \
    REBVAL name##_cell; \
    Prep_Cell(&name##_cell); \
    REBVAL * const name = &name##_cell
