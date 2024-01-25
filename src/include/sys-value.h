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
// it has in the payload might go bad.  Use Push_GC_Guard() to protect a
// stack variable's payload, and then Drop_GC_Guard() when the protection
// is not needed.  (You must always drop the most recently pushed guard.)
//
// Function invocations keep their arguments in FRAME!s, which can be accessed
// via ARG() and have stable addresses as long as the function is running.
//


//=//// DEBUG PROBE <== **THIS IS VERY USEFUL** //////////////////////////=//
//
// The PROBE macro can be used in debug builds to mold a REBVAL much like the
// Rebol `probe` operation.  But it's actually polymorphic, and if you have
// a Series*, Context*, or Array* it can be used with those as well.  In C++,
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

    #define WHERE(L) \
        Where_Core_Debug(L)

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
// limited sense to get "unreadables"--a cell you can write, but not read.
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

  // 1. These macros are "evil", because in the debug build, functions aren't
  //    inlined, and the overhead actually adds up very quickly.  We repeat
  //    arguments to speed up these critical tests, then wrap in READABLE()
  //    and WRITABLE() functions for callers that don't mind the cost.  The
  //    STATIC_ASSERT_LVALUE() macro catches any potential violators.

    #define ASSERT_CELL_READABLE(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* "evil macro" [1] */ \
        if ( \
            (FIRST_BYTE(&(c)->header.bits) & ( \
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
                    "ASSERT_CELL_READABLE() on NODE_FLAG_FREE cell\n" \
                ); \
            panic (c); \
        } \
    } while (0)

    #define ASSERT_CELL_WRITABLE(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* "evil macro" [1] */ \
        if ( \
            (FIRST_BYTE(&(c)->header.bits) & ( \
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

  #if (! CPLUSPLUS_11)
    INLINE const Cell* READABLE(NoQuote(const Cell*) c) {
        ASSERT_CELL_READABLE(c);
        return c_cast(Cell*, c);
    }
  #else
    INLINE NoQuote(const Cell*) READABLE(NoQuote(const Cell*) c) {
        ASSERT_CELL_READABLE(c);
        return c;
    }

    INLINE const Cell* READABLE(const Cell* c) {
        ASSERT_CELL_READABLE(c);
        return c;
    }
  #endif

    INLINE Cell* WRITABLE(Cell* c) {
        ASSERT_CELL_WRITABLE(c);
        return c;
    }

#else
    #define ASSERT_CELL_READABLE(c)    NOOP
    #define ASSERT_CELL_WRITABLE(c)    NOOP

    #define READABLE(c) (c)
    #define WRITABLE(c) (c)
#endif


// Note: If incoming p is mutable, we currently assume that's allowed by the
// flag bits of the node.  This could have a runtime check in debug build
// with a C++ variation that only takes mutable pointers.
//
INLINE void Init_Cell_Node1(Cell* v, Option(const Node*) node) {
    assert(v->header.bits & CELL_FLAG_FIRST_IS_NODE);
    PAYLOAD(Any, v).first.node = try_unwrap(node);
}

INLINE void Init_Cell_Node2(Cell* v, Option(const Node*) node) {
    assert(v->header.bits & CELL_FLAG_SECOND_IS_NODE);
    PAYLOAD(Any, v).second.node = try_unwrap(node);
}

#define Cell_Node1(v) \
    m_cast(Node*, PAYLOAD(Any, (v)).first.node)

#define Cell_Node2(v) \
    m_cast(Node*, PAYLOAD(Any, (v)).second.node)


#define Cell_Heart_Unchecked(cell) \
    u_cast(enum Reb_Kind, HEART_BYTE(cell))

#define Cell_Heart(cell) \
    Cell_Heart_Unchecked(READABLE(cell))


//=//// VALUE TYPE (always REB_XXX <= REB_MAX) ////////////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...though it's a WORD! underneath.
//
// (Instead of VAL_TYPE(), use Cell_Heart() if you wish to know that the cell
// pointer you pass in is carrying a word payload.  It disregards the quotes.)
//

INLINE enum Reb_Kind VAL_TYPE_UNCHECKED(const Cell* v) {
    switch (QUOTE_BYTE(v)) {
      case ANTIFORM_0: {
        Byte heart = HEART_BYTE(v);
        assert(  // can't answer VAL_TYPE() for unstable isotopes
            heart != REB_BLOCK
            and heart != REB_COMMA
            and heart != REB_ERROR
            and heart != REB_OBJECT
        );
        UNUSED(heart);
        return REB_ANTIFORM; }

      case NOQUOTE_1:
        return u_cast(enum Reb_Kind, HEART_BYTE(v));

      case QUASIFORM_2:
        return REB_QUASIFORM;

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
// 1. Avoid cost that inline functions (even constexpr) add to debug builds
//    by "typechecking" via finding the name ->header.bits in (c).
//
// 2. Cell flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.  We do this on the HeaderUnion
//    vs. x_cast() on the (c) to get the typechecking of [1]

#define Get_Cell_Flag(c,name) /* [1] */ \
    ((READABLE(c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag(c,name) \
    ((READABLE(c)->header.bits & CELL_FLAG_##name) == 0)

#define Get_Cell_Flag_Unchecked(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag_Unchecked(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) == 0)

#define Set_Cell_Flag(c,name) /* [2] */ \
    m_cast(union HeaderUnion*, &READABLE(c)->header)->bits |= CELL_FLAG_##name

#define Clear_Cell_Flag(c,name) \
    m_cast(union HeaderUnion*, &READABLE(c)->header)->bits &= ~CELL_FLAG_##name


// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
// This is another case where the debug build doesn't inline functions.
// Run the risk of repeating macro args to speed up this critical check.
//
#if (! DEBUG_MEMORY_ALIGN)
    #define ALIGN_CHECK_CELL(c)    NOOP
#else
    #define ALIGN_CHECK_CELL(c) \
        STATIC_ASSERT_LVALUE(c);  /* "evil macro", repeats arguments */ \
        if (i_cast(uintptr_t, (c)) % ALIGN_SIZE != 0) { \
            printf( \
                "Cell address %p not aligned to %d bytes\n", \
                c_cast(void*, (c)), \
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
// Note that an erased cell Is_Fresh(), but not READABLE() or WRITABLE().

INLINE Cell* Erase_Cell_Untracked(Cell* c) {
    ALIGN_CHECK_CELL(c);
    c->header.bits = CELL_MASK_0;
    return c;
}

#if CPLUSPLUS_11
    INLINE Atom(*) Erase_Cell_Untracked(Atom(*) atom) {
        ALIGN_CHECK_CELL(atom);
        atom->header.bits = CELL_MASK_0;
        return atom;
    }
#endif

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
// * To stop reading but not stop writing, use "unreadable" cells instead.
//
// * This will defeat Detect_Rebol_Pointer(), so it will not realize the value
//   is a cell any longer.  Hence poisoned cells should (perhaps obviously) not
//   be passed to API functions--as they'd appear to be UTF-8 strings.

#define Poison_Cell(v) \
    (TRACK(Erase_Cell(v))->header.bits = CELL_MASK_POISON)

#define Is_Cell_Poisoned(v) \
    ((v)->header.bits == CELL_MASK_POISON)


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//

// 1. "evil macros" for debug build performance, see STATIC_ASSERT_LVALUE()
//
// 2. In order to avoid the accidental ignoring of raised errors, they must
//    be deliberately suppressed vs. overwritten.
//
// 3. The requirement for suppression does not apply to a cell that is being
//    erased after having been moved, because it's the new cell that takes
//    over the "hot potato" of the error.

#define FRESHEN_CELL(v) do { \
    STATIC_ASSERT_LVALUE(v);  /* evil macro [1] */ \
    if (HEART_BYTE(v) == REB_ERROR)  /* must suppress [2] */ \
        assert(QUOTE_BYTE(v) != ANTIFORM_0);\
    assert(not ((v)->header.bits & CELL_FLAG_PROTECTED)); \
    (v)->header.bits &= CELL_MASK_PERSIST;  /* Note: no CELL or NODE flags */ \
} while (0)

#define FRESHEN_MOVED_CELL(v) do {  /* no suppress [3] */ \
    STATIC_ASSERT_LVALUE(v);  /* evil macro [1] */ \
    assert(not ((v)->header.bits & CELL_FLAG_PROTECTED)); \
    (v)->header.bits &= CELL_MASK_PERSIST;  /* Note: no CELL or NODE flags */ \
} while (0)


INLINE void Reset_Antiform_Header_Untracked(Cell* v, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE(ANTIFORM_0));
    FRESHEN_CELL(v);
    v->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // must ensure NODE+CELL
        | flags | FLAG_QUOTE_BYTE(ANTIFORM_0));
}

INLINE void Reset_Unquoted_Header_Untracked(Cell* v, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE(ANTIFORM_0));
    FRESHEN_CELL(v);
    v->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // must ensure NODE+CELL
        | flags | FLAG_QUOTE_BYTE(NOQUOTE_1));
}

INLINE REBVAL* Freshen_Cell_Untracked(Cell* v) {
    FRESHEN_CELL(v);
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

#if DEBUG
    #define Assert_Cell_Binding_Valid(v) \
        Assert_Cell_Binding_Valid_Core(v)
#else
    #define Assert_Cell_Binding_Valid(v) NOOP
#endif

#define Cell_Binding(v) \
    x_cast(Stub*, (v)->extra.Any.node)

#if (! CPLUSPLUS_11)
    #define BINDING(v) \
        *x_cast(Stub**, m_cast(Node**, &(v)->extra.Any.node))
#else
    struct BindingHolder {
        NoQuote(Cell*) & ref;

        BindingHolder(NoQuote(const Cell*) const& ref)
            : ref (const_cast<NoQuote(Cell*) &>(ref))
          {}

        void operator=(Stub* right) {
            ref->extra.Any.node = right;
            Assert_Cell_Binding_Valid(ref);
        }
        void operator=(BindingHolder const& right) {
            ref->extra.Any.node = right.ref->extra.Any.node;
            Assert_Cell_Binding_Valid(ref);
        }
        void operator=(nullptr_t)
          { ref->extra.Any.node = nullptr; }

        template<typename T>
        void operator=(Option(T) right) {
            ref->extra.Any.node = try_unwrap(right);
            Assert_Cell_Binding_Valid(ref);
        }

        Stub* operator-> () const
          { return x_cast(Stub*, ref->extra.Any.node); }

        operator Stub* () const
          { return x_cast(Stub*, ref->extra.Any.node); }
    };

    #define BINDING(v) \
        BindingHolder{v}

    template<typename T>
    struct cast_helper<BindingHolder,T> {
        static constexpr T convert(BindingHolder const& holder) {
            return cast(T, x_cast(Stub*, holder.ref->extra.Any.node));
        }
    };

  #if DEBUG
    INLINE void Corrupt_Pointer_If_Debug(BindingHolder const& bh)
      { bh.ref->extra.Any.node = p_cast(Stub*, cast(uintptr_t, 0xDECAFBAD)); }
  #endif
#endif



// An ANY-WORD! is relative if it refers to a local or argument of a function,
// and has its bits resident in the deep copy of that function's body.
//
// An ANY-ARRAY! in the deep copy of a function body must be relative also to
// the same function if it contains any instances of such relative words.
//
INLINE bool Is_Relative(const Cell* v) {
    if (not Is_Bindable(v))
        return false;  // may use extra for non-GC-marked uintptr_t-size data

    Series* binding = BINDING(v);
    if (not binding)
        return false;  // INTEGER! and other types are inherently "specific"

    if (not Is_Series_Array(binding))
        return false;

    return IS_DETAILS(binding);  // action
}

#if CPLUSPLUS_11
    bool Is_Relative(Value(const*) v) = delete;  // error on superfluous check
#endif

#define Is_Specific(v) \
    (not Is_Relative(v))


// When you have a Cell* (e.g. from a array) that you KNOW to be specific,
// you might be bothered by an error like:
//
//     "invalid conversion from 'Cell*' to 'ValueT*'"
//
// You can use SPECIFIC to cast it if you are *sure* that it has been
// derelativized -or- is a value type that doesn't have a specifier (e.g. an
// integer).  If the value is actually relative, this will assert at runtime!
//
// Because SPECIFIC has cost in the debug build, there may be situations where
// one is sure that the value is specific, and `cast(ValueT*, v)` is a better
// choice for efficiency.  This applies to things like `Copy_Cell()`, which
// is called often and already knew its input was a Value(*) to start with.
//
// Also, if you are enumerating an array of items you "know to be specific"
// then you have to worry about if the array is empty:
//
//     Value(*) head = SPECIFIC(Array_Head(a));  // a might be at tail !!!
//

INLINE Value(*) SPECIFIC(const_if_c Cell* v) {
   /* assert(Is_Specific(v)); */
    return x_cast(ValueT*, v);
}

#if CPLUSPLUS_11
    INLINE Value(const*) SPECIFIC(const Cell* v) {
/*        assert(Is_Specific(v)); */
        return c_cast(ValueT*, v);
    }

    INLINE void SPECIFIC(Value(const*) v) = delete;
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
// NOTE: Instead of using null for UNBOUND, a special global Stub struct was
// experimented with.  It was at a location in memory known at compile time,
// and it had its ->header and ->info bits set in such a way as to avoid the
// need for some conditional checks.  e.g. instead of writing:
//
//     if (binding and binding->header.bits & NODE_FLAG_MANAGED) {...}
//
// The special UNBOUND stub set some bits, such as to pretend to be managed:
//
//     if (binding->header.bits & NODE_FLAG_MANAGED) {...}  // incl. UNBOUND
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
    x_cast(Specifier*, nullptr)  // x_cast (don't want DEBUG_CHECK_CASTS)

#define UNBOUND nullptr  // not always a Node* (sometimes Context*)
#define UNSPECIFIED nullptr



// Use large indices to avoid confusion with 0 (reserved for unbound) and
// to avoid confusing with actual indices into objects.
//
#define INDEX_PATCHED (INT32_MAX - 1)  // directly points at variable patch
#define INDEX_ATTACHED INT32_MAX  // lazy creation of module variables

#define VAL_WORD_INDEX_I32(v)         PAYLOAD(Any, (v)).second.i32


INLINE void Copy_Cell_Header(
    Cell* out,
    const Cell* v
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    ASSERT_CELL_READABLE(v);  // allow copy void object vars

    FRESHEN_CELL(out);
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
// 1. If you write `Erase_Cell(dest)` followed by `Copy_Cell(dest, src)` the
//    optimizer seems to notice it doesn't need the masking of FRESHEN_CELL().
//    This was discovered by trying to force callers to pass in an already
//    freshened cell and seeing things get more complicated for no benefit.
//
// 2. Once upon a time binding init depended on the payload (when quoteds
//    could forward to a different cell), so this needed to be done first.
//    That's not true anymore, but some future INIT_BINDING() may need to
//    be able to study to the cell to do the initialization?
//
INLINE Cell* Copy_Cell_Untracked(
    Cell* out,
    const Cell* v,
    Flags copy_mask  // typically you don't copy UNEVALUATED, PROTECTED, etc
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    ASSERT_CELL_READABLE(v);  // allow copy void object vars

    FRESHEN_CELL(out);  // optimizer seems to skip this mask after erasure [1]
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & copy_mask));

    out->payload = v->payload;  // before init binding anachronism [1]

    out->extra = v->extra;  // binding or inert bits

    return out;
}

INLINE bool Is_Stable(Atom(const*) v);

#if CPLUSPLUS_11  // REBVAL and Cell are checked distinctly
    INLINE Value(*) Copy_Cell_Untracked(
        Cell* out,
        Value(const*) v,
        Flags copy_mask
    ){
        return cast(Value(*), Copy_Cell_Untracked(
            out,
            c_cast(Cell*, v),
            copy_mask
        ));
    }

    INLINE Value(*) Copy_Cell_Untracked(
        Value(*) out,
        Value(const*) v,
        Flags copy_mask
    ){
        return cast(Value(*), Copy_Cell_Untracked(
            cast(Cell*, out),
            c_cast(Cell*, v),
            copy_mask
        ));
    }

    INLINE Value(*) Copy_Cell_Untracked(
        Value(*) out,
        Atom(const*) v,
        Flags copy_mask
    ){
        assert(Is_Stable(v));
        return cast(Value(*), Copy_Cell_Untracked(
            cast(Cell*, out),
            c_cast(Cell*, v),
            copy_mask
        ));
    }

    INLINE void Copy_Cell_Untracked(
        Value(*) out,
        const Cell* v,
        Flags copy_mask
    ) = delete;
#endif

#define Copy_Cell(out,v) \
    TRACK(Copy_Cell_Untracked((out), (v), CELL_MASK_COPY))

#define Copy_Cell_Core(out,v,copy_mask) \
    TRACK(Copy_Cell_Untracked((out), (v), (copy_mask)))

INLINE Cell* Copy_Relative_internal(Cell* out, const Cell* in) {  // dangerous!
    Copy_Cell_Header(out, in);
    out->payload = in->payload;
    out->extra = in->extra;
    return out;
}


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//

// Moving a cell invalidates the old location.  This idea is a potential
// prelude to being able to do some sort of reference counting on series based
// on the cells that refer to them tracking when they are overwritten.  One
// advantage would be being able to leave the reference counting as-is.
//
// In the meantime, this just does a Copy + RESET.

INLINE REBVAL *Move_Cell_Untracked(
    Cell* out,
    Atom(*) v,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, v, copy_mask);  // Move_Cell() adds track to `out`
    FRESHEN_MOVED_CELL(v);  // track to here not useful

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
INLINE Atom(*) Inherit_Const(Atom(*) out, const Cell* influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}
#define Trust_Const(value) \
    (value) // just a marking to say the const is accounted for already

INLINE REBVAL *Constify(REBVAL *v) {
    Set_Cell_Flag(v, CONST);
    return v;
}


//
// Rather than allow Cell storage to be declared plainly as a local variable in
// a C function, this macro provides a generic "constructor-like" hook.
//
// Note: This runs an Erase_Cell(), which is cheap.  But still something, so
// DECLARE_LOCAL() during a loop should be avoided.  It should be at the
// outermost scope of the function.
//
// !!! Cells on the C stack can't be preserved across stackless continuations.
// Rather than using DECLARE_LOCAL(), natives should use <local> in their spec
// to define cells that are part of the frame, and access them via LOCAL().
//
#define DECLARE_LOCAL(name) \
    Cell name##_cell; \
    Erase_Cell(&name##_cell); \
    Atom(*) name = cast(Atom(*), &name##_cell)

#define DECLARE_STABLE(name) \
    Cell name##_cell; \
    Erase_Cell(&name##_cell); \
    Value(*) name = cast(Value(*), &name##_cell)
