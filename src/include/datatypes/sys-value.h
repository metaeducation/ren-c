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
// accessors dereference REBVAL (or Cell) pointers, the inline functions
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
// a REBSER*, Context(*), or Array(*) it can be used with those as well.  In C++,
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
                std::is_pointer<T>::value  // assume pointers are Node*
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

    #define WHERE(f) \
        Where_Core_Debug(f)

#elif !defined(NDEBUG) // don't cause compile time error on PROBE()
    #define PROBE(v) \
        do { \
            printf("DEBUG_HAS_PROBE disabled %s %d\n", __FILE__, __LINE__); \
            fflush(stdout); \
        } while (0)
#endif


//=//// CELL VALIDATION (DEBUG BUILD ONLY) ////////////////////////////////=//
//
// There are three categories of checks, which are used pervasively in the
// system and help catch a lot of mistakes:
//
// ["FRESHNESS"]
//
// Most read and write operations of cells assert that the header has both
// NODE_FLAG_NODE and NODE_FLAG_CELL set.  But there is an exception made when
// it comes to initialization: a cell is allowed to have a header that is all
// 0 bits (e.g. CELL_MASK_0).  Ranges of cells can be memset() to 0 very
// quickly, and the OS sets C globals to all 0 bytes when the process starts
// for security reasons.
//
// So a "fresh" cell is one that does not need to have its CELL_MASK_PERSIST
// portions masked out.  An initialization routine can just bitwise OR the
// flags it wants overlaid on the persisted flags (if any).  However, it
// should include NODE_FLAG_NODE and NODE_FLAG_CELL in that masking in case
// they weren't there.
//
// Fresh cells can occur "naturally" (from memset() or other 0 memory), be
// made manually with Erase_Cell(), or an already initialized cell can have
// its CELL_MASK_PERSIST portions wiped out with FRESHEN().
//
// Note that if CELL_FLAG_PROTECTED is set on a cell, it will not be considered
// fresh for initialization.  So the flag must be cleared or the cell erased
// in order to overwrite it.
//
// [READABILITY]
//
// Readable cells have NODE_FLAG_NODE and NODE_FLAG_CELL set.  It's important
// that they do, because if they don't then the first byte of the header
// could be mistaken for valid UTF-8 (see Detect_Rebol_Pointer() for the
// machinery that relies upon this for mixing UTF-8, cells, and series in
// variadic API calls).
//
// Also, readable cells don't have NODE_FLAG_FREE set.  At one time the
// evaluator would start off by marking all cells with this bit in order to
// track that the output had not been assigned.  This helped avoid spurious
// reads and differentiated `(void) else [...]` from `(else [...])`.  But
// it required a bit being added and removed, so it was replaced with the
// concept of "freshness" removing NODE_FLAG_NODE and NODE_FLAG_CELL to get
// the effect with less overhead.  So NODE_FLAG_FREE is now used in a more
// limited sense to get "poisoning"--a cell you can't read or write.
//
// [WRITABILITY]
//
// A writable cell is one that has NODE_FLAG_NODE and NODE_FLAG_CELL set, but
// that also does not have NODE_FLAG_PROTECTED.  While the Init_XXX() routines
// generally want to test for freshness, things like Set_Cell_Flag() are
// based on writability...e.g. a cell that's already been initialized and can
// have its properties manipulated.

#define Is_Fresh(c) \
    (((c)->header.bits & (~ CELL_MASK_PERSIST)) == 0)

#if DEBUG_CELL_WRITABILITY

  // These macros are "evil", because in the debug build, functions aren't
  // inlined, and the overhead actually adds up very quickly.  Run the risk
  // of repeating macro arguments to speed up these critical tests, then
  // wrap in READABLE() and WRITABLE() functions for higher-level callers
  // that don't mind the cost.

    #define ASSERT_CELL_FRESH_EVIL_MACRO(c) do {  /* someday may be EVIL! */ \
        assert(Is_Fresh(c)); \
    } while (0)

    #define ASSERT_CELL_READABLE_EVIL_MACRO(c) do {  /* EVIL! see above */ \
        if ( \
            (FIRST_BYTE((c)->header) & ( \
                NODE_BYTEMASK_0x01_CELL | NODE_BYTEMASK_0x80_NODE \
                    | NODE_BYTEMASK_0x40_STALE \
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
                ); \
            panic (c); \
        } \
    } while (0)

    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c) do {  /* EVIL! see above */ \
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
    } while (0)

    inline static Cell(const*) READABLE(const RawCell *c) {
        ASSERT_CELL_READABLE_EVIL_MACRO(c);  // ^-- should this be a template?
        return cast(Cell(const*), c);
    }

    inline static Cell(*) WRITABLE(Cell(*) c) {
        ASSERT_CELL_WRITABLE_EVIL_MACRO(c);
        return c;
    }

#else
    #define ASSERT_CELL_FRESH_EVIL_MACRO(c)    NOOP
    #define ASSERT_CELL_READABLE_EVIL_MACRO(c)    NOOP
    #define ASSERT_CELL_WRITABLE_EVIL_MACRO(c)    NOOP

    #define READABLE(c) (c)
    #define WRITABLE(c) (c)
#endif


// Note: If incoming p is mutable, we currently assume that's allowed by the
// flag bits of the node.  This could have a runtime check in debug build
// with a C++ variation that only takes mutable pointers.
//
inline static void INIT_VAL_NODE1(Cell(*) v, option(const Node*) node) {
    assert(v->header.bits & CELL_FLAG_FIRST_IS_NODE);
    PAYLOAD(Any, v).first.node = try_unwrap(node);
}

inline static void INIT_VAL_NODE2(Cell(*) v, option(const Node*) node) {
    assert(v->header.bits & CELL_FLAG_SECOND_IS_NODE);
    PAYLOAD(Any, v).second.node = try_unwrap(node);
}

#define VAL_NODE1(v) \
    m_cast(Node*, PAYLOAD(Any, (v)).first.node)

#define VAL_NODE2(v) \
    m_cast(Node*, PAYLOAD(Any, (v)).second.node)



// Note: Only change bits of existing cells if the new type payload matches
// the type and bits (e.g. ANY-WORD! to another ANY-WORD!).  Otherwise the
// value-specific flags might be misinterpreted.
//
#define mutable_HEART_BYTE(v) \
    mutable_SECOND_BYTE(WRITABLE(v)->header)


#define CELL_HEART_UNCHECKED(cell) \
    cast(enum Reb_Kind, HEART_BYTE_UNCHECKED(cell))

#define CELL_HEART(cell) \
    CELL_HEART_UNCHECKED(READABLE(cell))


inline static const REBTYP *CELL_CUSTOM_TYPE(noquote(Cell(const*)) v) {
    assert(CELL_HEART(v) == REB_CUSTOM);
    return cast(Binary(const*), EXTRA(Any, v).node);
}

// Sometimes you have a noquote and need to pass a REBVAL* to something.  It
// doesn't seem there's too much bad that can happen if you do; you'll get
// back something that might be quoted up to 3 levels...if it's an escaped
// cell then it won't be quoted at all.  Main thing to know is that you don't
// necessarily get the original value you had back.
//
inline static Cell(const*) CELL_TO_VAL(noquote(Cell(const*)) cell)
  { return cast(Cell(const*), cell); }

#if CPLUSPLUS_11
    inline static Cell(const*) CELL_TO_VAL(Cell(const*) cell) = delete;
#endif


//=//// VALUE TYPE (always REB_XXX <= REB_MAX) ////////////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...though it's a WORD! underneath.
//
// (Instead of VAL_TYPE(), use CELL_HEART() if you wish to know that the cell
// pointer you pass in is carrying a word payload.  It disregards the quotes.)
//

inline static option(SymId) VAL_WORD_ID(noquote(Cell(const*)) v);

inline static enum Reb_Kind VAL_TYPE_UNCHECKED(Cell(const*) v) {
    switch (QUOTE_BYTE_UNCHECKED(v)) {
      case ISOTOPE_0:
        if (HEART_BYTE_UNCHECKED(v) == REB_BLANK)
            return REB_NULL;
        if (HEART_BYTE_UNCHECKED(v) == REB_VOID)
            return REB_NIHIL;
        if (HEART_BYTE_UNCHECKED(v) == REB_WORD) {
            if (VAL_WORD_ID(v) == SYM_TRUE or VAL_WORD_ID(v) == SYM_FALSE)
                return REB_LOGIC;  // !!! Temporary compatibility
        }
        return REB_ISOTOPE;

      case UNQUOTED_1:
        return cast(enum Reb_Kind, HEART_BYTE_UNCHECKED(v));

      case QUASI_2:
        return REB_QUASI;

      default:
        return REB_QUOTED;
    }
}

#if defined(NDEBUG)
    #define VAL_TYPE VAL_TYPE_UNCHECKED
#else
    #define VAL_TYPE(v) \
        VAL_TYPE_UNCHECKED(READABLE(v))
#endif


//=//// GETTING, SETTING, and CLEARING VALUE FLAGS ////////////////////////=//
//
// The header of a cell contains information about what kind of cell it is,
// as well as some flags that are reserved for system purposes.  These are
// the NODE_FLAG_XXX and CELL_FLAG_XXX flags, that work on any cell.
//

#define Get_Cell_Flag(v,name) \
    ((READABLE(v)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag(v,name) \
    ((READABLE(v)->header.bits & CELL_FLAG_##name) == 0)

#define Set_Cell_Flag(v,name) \
    (WRITABLE(v)->header.bits |= CELL_FLAG_##name)

#define Clear_Cell_Flag(v,name) \
    (WRITABLE(v)->header.bits &= ~CELL_FLAG_##name)


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


//=//// CELL "ERASING" ////////////////////////////////////////////////////=//
//
// To help be robust, the code ensures that NODE_FLAG_NODE and NODE_FLAG_CELL
// are set in the header of a memory slot before reading or writing info for
// a cell.  But an exception is made for efficiency that allows initialization
// in the case of a header that is all zeros.  This pattern is efficiently
// produced by memset(), and global memory for a C program is initialized to
// all zeros to protect leakage from other processes...so it's good to be
// able to take advantage of it where possible.
//
// Note that an erased cell is FRESH(), but not READABLE() or WRITABLE().

inline static Cell(*) Erase_Cell_Untracked(RawCell* c) {
    ALIGN_CHECK_CELL_EVIL_MACRO(c);
    c->header.bits = CELL_MASK_0;
    return cast(Cell(*), c);
}

#define Erase_Cell(c) \
    TRACK(Erase_Cell_Untracked(c))

#define Is_Cell_Erased(v) \
    ((v)->header.bits == CELL_MASK_0)


//=//// CELL "POISONING" //////////////////////////////////////////////////=//
//
// Poisoning is used in the spirit of things like Address Sanitizer to block
// reading or writing locations such as beyond the allocated memory of an
// array series.  It leverages the checks done by READABLE(), WRITABLE() and
// FRESH()
//
// Another use for the poisoned state is in an optimized array representation
// that fits 0 or 1 cells into the series node itself.  Since the cell lives
// where the content tracking information would usually be, there's no length.
// Hence the presence of a poison cell in the slot indicates length 0.
//
// * To stop reading but not stop writing, use "TRASHING" cells instead.
//
// * This will defeat Detect_Rebol_Pointer(), so it will not realize the value
//   is a cell any longer.  Hence poisoned cells should (perhaps obviously) not
//   be passed to API functions--as they'd appear to be UTF-8 strings.

#define Poison_Cell(v) \
    (TRACK(Erase_Cell(v))->header.bits = CELL_MASK_POISON)

#define Is_Cell_Poisoned(v) \
    ((v)->header.bits == CELL_MASK_POISON)


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//

// 1. In order to avoid the accidental ignoring of raised errors, they must
//    be deliberately suppressed vs. overwritten.
//
// 2. The requirement for suppression does not apply to a cell that is being
//    erased after having been moved, because it's the new cell that takes
//    over the "hot potato" of the error.

#define FRESHEN_CELL_EVIL_MACRO(v) do { \
    if (HEART_BYTE_UNCHECKED(v) == REB_ERROR)  /* must suppress, see [1] */ \
        assert(QUOTE_BYTE_UNCHECKED(v) != ISOTOPE_0);\
    assert(not ((v)->header.bits & CELL_FLAG_PROTECTED)); \
    (v)->header.bits &= CELL_MASK_PERSIST;  /* Note: no CELL or NODE flags */ \
} while (0)

#define FRESHEN_MOVED_CELL_EVIL_MACRO(v) do {  /* no suppress, see [2] */ \
    assert(not ((v)->header.bits & CELL_FLAG_PROTECTED)); \
    (v)->header.bits &= CELL_MASK_PERSIST;  /* Note: no CELL or NODE flags */ \
} while (0)


inline static void Reset_Unquoted_Header_Untracked(Cell(*) v, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE(ISOTOPE_0));
    FRESHEN_CELL_EVIL_MACRO(v);
    v->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // must ensure NODE+CELL
        | flags | FLAG_QUOTE_BYTE(UNQUOTED_1));
}

inline static REBVAL *RESET_CUSTOM_CELL(
    Cell(*) out,
    REBTYP *type,
    Flags flags
){
    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(REB_CUSTOM) | flags
    );
    EXTRA(Any, out).node = type;
    return cast(REBVAL*, out);
}

inline static REBVAL* Freshen_Cell_Untracked(Cell(*) v) {
    FRESHEN_CELL_EVIL_MACRO(v);
    return cast(REBVAL*, v);
}

#define FRESHEN(v) \
    TRACK(Freshen_Cell_Untracked(v))
        // ^-- track AFTER reset, so you can diagnose cell origin in WRITABLE()



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
inline static bool IS_RELATIVE(Cell(const*) v) {
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


// When you have a Cell(*) (e.g. from a array) that you KNOW to be specific,
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

inline static REBVAL *SPECIFIC(const_if_c Cell(*) v) {
    assert(IS_SPECIFIC(v));
    return m_cast(REBVAL*, cast(const REBVAL*, v));
}

#if CPLUSPLUS_11
    inline static const REBVAL *SPECIFIC(Cell(const*) v) {
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
// a Node which constitutes their notion of "binding".
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

#define UNBOUND nullptr  // not always a Node* (sometimes Context(*))
#define UNSPECIFIED nullptr


inline static bool ANY_ARRAYLIKE(noquote(Cell(const*)) v) {
    if (ANY_ARRAY_KIND(CELL_HEART(v)))
        return true;
    if (not ANY_SEQUENCE_KIND(CELL_HEART(v)))
        return false;
    if (Not_Cell_Flag(v, FIRST_IS_NODE))
        return false;
    const Node* node1 = VAL_NODE1(v);
    if (Is_Node_A_Cell(node1))
        return false;
    return SER_FLAVOR(SER(node1)) == FLAVOR_ARRAY;
}

inline static bool ANY_WORDLIKE(noquote(Cell(const*)) v) {
    if (ANY_WORD_KIND(CELL_HEART(v)))
        return true;
    if (not ANY_SEQUENCE_KIND(CELL_HEART(v)))
        return false;
    if (Not_Cell_Flag(v, FIRST_IS_NODE))
        return false;
    const Node* node1 = VAL_NODE1(v);
    if (Is_Node_A_Cell(node1))
        return false;
    return SER_FLAVOR(SER(node1)) == FLAVOR_SYMBOL;
}

inline static bool ANY_STRINGLIKE(noquote(Cell(const*)) v) {
    if (ANY_STRING_KIND(CELL_HEART(v)))
        return true;
    if (CELL_HEART(v) == REB_URL)
        return true;
    if (CELL_HEART(v) != REB_ISSUE)
        return false;
    return Get_Cell_Flag(v, ISSUE_HAS_NODE);
}


inline static void INIT_VAL_WORD_SYMBOL(Cell(*) v, Symbol(const*) symbol)
  { INIT_VAL_NODE1(v, symbol); }

inline static const Raw_Symbol* VAL_WORD_SYMBOL(noquote(Cell(const*)) cell) {
    assert(ANY_WORDLIKE(cell));
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
    Cell(*) out,
    Cell(const*) v
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    ASSERT_CELL_READABLE_EVIL_MACRO(v);  // allow copy void object vars

    FRESHEN_CELL_EVIL_MACRO(out);
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & CELL_MASK_COPY));

  #if DEBUG_TRACK_EXTEND_CELLS
    out->file = v->file;
    out->line = v->line;
    out->tick = TG_tick;  // initialization tick
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
inline static Cell(*) Copy_Cell_Untracked(
    Cell(*) out,
    Cell(const*) v,
    Flags copy_mask  // typically you don't copy UNEVALUATED, PROTECTED, etc
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    ASSERT_CELL_READABLE_EVIL_MACRO(v);  // allow copy void object vars

    // Q: Will optimizer notice if copy mask is CELL_MASK_ALL, and not bother
    // with masking out CELL_MASK_PERSIST since all bits are overwritten?
    //
    FRESHEN_CELL_EVIL_MACRO(out);
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & copy_mask));

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

#if CPLUSPLUS_11  // REBVAL and Cell are checked distinctly
    inline static REBVAL *Copy_Cell_Untracked(
        Cell(*) out,
        const REBVAL *v,
        Flags copy_mask
    ){
        return cast(REBVAL*, Copy_Cell_Untracked(
            out,
            cast(Cell(const*), v),
            copy_mask
        ));
    }

    inline static REBVAL *Copy_Cell_Untracked(
        REBVAL *out,
        const REBVAL *v,
        Flags copy_mask
    ){
        return cast(REBVAL*, Copy_Cell_Untracked(
            cast(Cell(*), out),
            cast(Cell(const*), v),
            copy_mask
        ));
    }

    inline static Cell(*) Copy_Cell_Untracked(
        REBVAL *out,
        Cell(const*) v,
        Flags copy_mask
    ) = delete;
#endif

#define Copy_Cell(out,v) \
    TRACK(Copy_Cell_Untracked((out), (v), CELL_MASK_COPY))

#define Copy_Cell_Core(out,v,copy_mask) \
    TRACK(Copy_Cell_Untracked((out), (v), (copy_mask)))


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//

// Moving a cell invalidates the old location.  This idea is a potential
// prelude to being able to do some sort of reference counting on series based
// on the cells that refer to them tracking when they are overwritten.  One
// advantage would be being able to leave the reference counting as-is.
//
// In the meantime, this just does a Copy + RESET.

inline static REBVAL *Move_Cell_Untracked(
    Cell(*) out,
    REBVAL *v,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, v, copy_mask);  // Move_Cell() adds track to `out`
    FRESHEN_MOVED_CELL_EVIL_MACRO(v);  // track to here not useful

  #if DEBUG_TRACK_EXTEND_CELLS  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TG_tick;
  #endif

    return cast(REBVAL*, out);
}

#define CELL_MASK_MOVE (CELL_MASK_COPY | CELL_FLAG_UNEVALUATED)

#define Move_Cell(out,v) \
    TRACK(Move_Cell_Untracked((out), (v), CELL_MASK_MOVE))

#define Move_Cell_Core(out,v,cell_mask) \
    TRACK(Move_Cell_Untracked((out), (v), (cell_mask)))


// !!! Super primordial experimental `const` feature.  Concept is that various
// operations have to be complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.  const input always
// gets you const output, but mutable input will get you const output if
// the value itself is const (so it inherits).
//
inline static REBVAL *Inherit_Const(REBVAL *out, Cell(const*) influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}
#define Trust_Const(value) \
    (value) // just a marking to say the const is accounted for already

inline static REBVAL *Constify(REBVAL *v) {
    Set_Cell_Flag(v, CONST);
    return v;
}


//
// Rather than allow Cell storage to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
//
// Note: because this will run instructions, a routine should avoid doing a
// DECLARE_LOCAL() inside of a loop.  It should be at the outermost scope of
// the function.
//
// !!! Cells on the C stack can't be preserved across stackless continuations.
// Rather than using DECLARE_LOCAL(), natives should use <local> in their spec
// to define cells that are part of the frame, and access them via LOCAL().
//
#define DECLARE_LOCAL(name) \
    REBVAL name##_cell; \
    Erase_Cell(&name##_cell); \
    REBVAL * const name = &name##_cell
