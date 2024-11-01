//
//  File: %sys-cell.h
//  Summary: "Cell Definitions AFTER %tmp-internals.h}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2024 Ren-C Open Source Contributors
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
// See notes in %struct-cell.h for the definition of the Cell structure.
//
// While some Cells are in C stack variables, most reside in the allocated
// memory block for an Array Flex.  The underlying Flex memory can be
// resized and require a reallocation, or it may become invalid if the
// containing Array is garbage-collected.  This means that many pointers to
// Cells are movable, and could become invalid if arbitrary user code
// is run...this includes Cells on the data stack, which is implemented as
// an Array under the hood.  (See %sys-datastack.h)
//
// A Cell in a C stack variable does not have to worry about its memory
// address becoming invalid--but by default the garbage collector does not
// know that value exists.  So while the address may be stable, any Flexes
// it has in the Payload might go bad.  Use Push_Lifeguard() to protect a
// stack variable's Payload, and then Drop_Lifeguard() when the protection
// is not needed.  (You must always drop the most recently pushed guard.)
//
// Function invocations keep their arguments in FRAME!s, which can be accessed
// via ARG() and have stable addresses as long as the function is running.
//


//=//// CELL ALIGMENT CHECKING ////////////////////////////////////////////=//
//
// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
#if (! DEBUG_MEMORY_ALIGNMENT)
    #define Assert_Cell_Aligned(c)    NOOP
#else
    #define Assert_Cell_Aligned(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        if (i_cast(uintptr_t, (c)) % ALIGN_SIZE != 0) \
            Panic_Cell_Unaligned(c); \
    } while (0)
#endif


//=//// CELL "ERASING" ////////////////////////////////////////////////////=//
//
// To help be robust, the code ensures that NODE_FLAG_NODE and NODE_FLAG_CELL
// are set in the header of a memory slot before reading or writing info for
// a cell.  But an exception is made for efficiency that allows initialization
// in the case of a header that is all zeros.  This pattern is efficiently
// produced by memset(), and global memory for a C program is initialized to
// all zeros to protect leakage from other processes...so it's good to be
// able to take advantage of it *where possible*  (see [1]).
//
// 1. !!! WARNING: If you do not fully control the location you are writing,
//    Erase_Cell() is NOT what you want to use to make a cell writable.  You
//    could be overwriting persistent cell bits such as NODE_FLAG_ROOT that
//    indicates an API handle, or NODE_FLAG_MANAGED that indicates a Pairing.
//    This is strictly for things like quickly formatting array cells or
//    restoring 0-initialized global variables back to the 0-init state.
//
// 2.  Note that an erased cell Is_Fresh(), but Ensure_Readable() will fail,
//     and so will Ensure_Writable().

INLINE Cell* Erase_Cell_Untracked(Cell* c) {
    Assert_Cell_Aligned(c);
    c->header.bits = CELL_MASK_0;
    return c;
}

#define Erase_Cell(c) \
    TRACK(Erase_Cell_Untracked(c))  // !!! most cases need Freshen_Cell() [1]

#define Is_Cell_Erased(c) \
    ((c)->header.bits == CELL_MASK_0)  // Is_Fresh(), but not read/write [2]


//=//// CELL "POISONING" //////////////////////////////////////////////////=//
//
// Poisoning is used in the spirit of things like Address Sanitizer to block
// reading or writing locations such as beyond the allocated memory of an
// Array Flex.  It leverages the checks done by Ensure_Readable(),
// Ensure_Writable() and Is_Fresh()
//
// * To stop reading but not writing, use Init_Unreadable() cells instead.
//
// * This will defeat Detect_Rebol_Pointer(), so it will not realize the value
//   is a cell any longer.  Hence poisoned Cells should (perhaps obviously) not
//   be passed to API functions--as they'd appear to be UTF-8 strings.
//
// 1. The mask has NODE_FLAG_CELL but no NODE_FLAG_NODE, so Ensure_Readable()
//    will fail, and it is CELL_FLAG_PROTECTED so Ensure_Writable() will fail.
//    It can't be freshened with Freshen_Cell().  You have to Erase_Cell().
//
// 2. Poison cells are designed to be used in places where Erase_Cell() would
//    not lose information.  For instance: it's used in the optimized array
//    representation that fits 0 or 1 cells into the Array Stub itself.  But
//    if you were to poison an API handle it would overwrite NODE_FLAG_ROOT,
//    and a managed pairing would overwrite NODE_FLAG_MANAGED.

#define CELL_MASK_POISON \
    (NODE_FLAG_CELL | CELL_FLAG_PROTECTED)  // not readable or writable [1]

#define Poison_Cell(c) \
    (TRACK(c)->header.bits = CELL_MASK_POISON)

INLINE bool Is_Cell_Poisoned(const Cell* c) {
    assert(Is_Cell_Erased(c) or (c->header.bits & NODE_FLAG_CELL));
    return c->header.bits == CELL_MASK_POISON;
}


//=//// CELL READABLE/WRITABLE CHECKS (don't apply in release builds) /////=//
//
// [READABILITY]
//
// Readable cells have NODE_FLAG_NODE and NODE_FLAG_CELL set.  It's important
// that they do, because if they don't then the first byte of the header
// could be mistaken for valid UTF-8 (see Detect_Rebol_Pointer() for the
// machinery that relies upon this for mixing UTF-8, Cells, and Stubs in
// variadic API calls).
//
// Also, readable cells don't have NODE_FLAG_UNREADABLE set.  At one time the
// evaluator would start off by marking all cells with this bit in order to
// track that the output had not been assigned.  This helped avoid spurious
// reads and differentiated `(void) else [...]` from `(else [...])`.  But
// it required a bit being added and removed, so it was replaced with the
// concept of Is_Fresh(), removing NODE_FLAG_NODE and NODE_FLAG_CELL to get
// the effect with less overhead.  So NODE_FLAG_UNREADABLE is now used in a more
// limited sense to get "unreadables"--a cell you can write, but not read.
//
// [WRITABILITY]
//
// A writable cell is one that has NODE_FLAG_NODE and NODE_FLAG_CELL set, but
// that also does not have NODE_FLAG_PROTECTED.  While the Init_XXX() routines
// generally want to test for freshness, things like Set_Cell_Flag() are
// based on writability...e.g. a cell that's already been initialized and can
// have its properties manipulated.
//
// Note that this code asserts about NODE_FLAG_PROTECTED just to be safe,
// but general idea is that a cell which is protected should never be writable
// at runtime, enforced by the `const Cell*` convention.  You can't get a
// non-const Cell reference without going through a runtime check that
// makes sure the cell is not protected.
//
// 1. These macros are "evil", because in the checked build, functions aren't
//    inlined, and the overhead actually adds up very quickly.  We repeat
//    arguments to speed up these critical tests, then wrap them in
//    Ensure_Readable() and Ensure_Writable() functions for callers that
//    don't mind the cost.  The STATIC_ASSERT_LVALUE() macro catches any
//    potential violators.

#if DEBUG_CELL_READ_WRITE
    #define Assert_Cell_Readable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        if ( \
            (((c)->header.bits) & ( \
                NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_UNREADABLE \
            )) != (NODE_FLAG_NODE | NODE_FLAG_CELL) \
        ){ \
            Panic_Cell_Unreadable(c); \
        } \
    } while (0)

    #define Assert_Cell_Writable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        if ( \
            (((c)->header.bits) & ( \
                NODE_FLAG_NODE | NODE_FLAG_CELL | CELL_FLAG_PROTECTED \
            )) != (NODE_FLAG_NODE | NODE_FLAG_CELL) \
        ){ \
            Panic_Cell_Unwritable(c); \
        } \
    } while (0)

  #if (! CPLUSPLUS_11)
    #define Ensure_Readable(c) (c)
    #define Ensure_Writable(c) (c)
  #else
    template<typename T>
    T Ensure_Readable(T cell) {
        Assert_Cell_Readable(cell);
        return cell;
    }

    template<typename T>
    T Ensure_Writable(T cell) {
        Assert_Cell_Writable(cell);
        return cell;
    }
  #endif
#else
    #define Assert_Cell_Readable(c)    NOOP
    #define Assert_Cell_Writable(c)    NOOP

    #define Ensure_Readable(c) (c)
    #define Ensure_Writable(c) (c)
#endif


//=//// CELL "FRESHNESS" //////////////////////////////////////////////////=//
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
// its non-CELL_MASK_PERSIST portions wiped out with Freshen_Cell().
//
// Note that if CELL_FLAG_PROTECTED is set on a cell, it will not be considered
// fresh for initialization.  So the flag must be cleared or the cell erased
// in order to overwrite it.
//
// 1. "evil macros" for checked build performance, see STATIC_ASSERT_LVALUE()
//
// 2. In order to avoid accidentally overlooking raised errors, they must
//    be deliberately suppressed vs. overwritten.  e.g. the requirement for
//    suppression does not apply to a cell that is being erased after having
//    been moved, as the new cell takes over the "hot potato" of the error.
//

#if DEBUG_CELL_READ_WRITE
    #define Assert_Cell_Initable(out) do { \
        STATIC_ASSERT_LVALUE(out);  /* evil macro [1] */ \
        if (not Is_Cell_Erased(out))  /* CELL_MASK_0 considered initable */ \
            Assert_Cell_Writable(out);  /* else need NODE and CELL flags */ \
    } while (0)
#else
    #define Assert_Cell_Initable(c)    NOOP
#endif

#define Is_Fresh(c) \
    (((c)->header.bits & (~ CELL_MASK_PERSIST) & \
        (~ NODE_FLAG_NODE) & (~ NODE_FLAG_CELL)) == 0)

#define Freshen_Cell_Suppress_Raised_Untracked(c) do {  /* [2] */ \
    STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
    Assert_Cell_Initable(c);  /* if CELL_MASK_0, node + cell flags not set */ \
    (c)->header.bits &= CELL_MASK_PERSIST;  /* won't add cell + node flags */ \
} while (0)

#define Freshen_Cell_Untracked(c) do { \
    STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
    if (HEART_BYTE(c) == REB_ERROR)  /* warn on overwrite if raised [2] */ \
        assert(QUOTE_BYTE(c) != ANTIFORM_0);\
    Freshen_Cell_Suppress_Raised_Untracked(c);  /* already warned */ \
} while (0)

INLINE void Inline_Freshen_Cell_Untracked(Cell* c)
  { Freshen_Cell_Untracked(c); }

#define Freshen_Cell(v) \
    Inline_Freshen_Cell_Untracked(TRACK(v))

INLINE void Inline_Freshen_Cell_Suppress_Raised_Untracked(Cell* c)
  { Freshen_Cell_Suppress_Raised_Untracked(c); }

#define Freshen_Cell_Suppress_Raised(v)  /* [2] */ \
    Inline_Freshen_Cell_Suppress_Raised_Untracked(TRACK(v))




#define Cell_Heart_Unchecked(c) \
    u_cast(Heart, HEART_BYTE(c))

#define Cell_Heart(c) \
    Cell_Heart_Unchecked(Ensure_Readable(c))

INLINE Heart Cell_Heart_Ensure_Noquote(const Cell* c) {
    assert(QUOTE_BYTE(c) == NOQUOTE_1);
    return Cell_Heart_Unchecked(c);
}


//=//// VALUE TYPE (always REB_XXX <= REB_MAX) ////////////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...though it's a WORD! underneath.
//
// (Instead of VAL_TYPE(), use Cell_Heart() if you wish to know that the cell
// pointer you pass in is carrying a word payload.  It disregards the quotes.)
//

INLINE Kind VAL_TYPE_UNCHECKED(const Atom* v) {
    switch (QUOTE_BYTE(v)) {
      case ANTIFORM_0_COERCE_ONLY: {  // use this constant rarely!
        Byte heart = HEART_BYTE(v);
        assert(  // can't answer VAL_TYPE() for unstable isotopes
            heart != REB_BLOCK
            and heart != REB_COMMA
            and heart != REB_ERROR
            and heart != REB_OBJECT
        );
        UNUSED(heart);
        return REB_ANTIFORM; }

      case NOQUOTE_1: {
        return u_cast(Kind, HEART_BYTE(v)); }

      case QUASIFORM_2_COERCE_ONLY:  // use this constant rarely!
        return REB_QUASIFORM;

      default:
        return REB_QUOTED;
    }
}

#if NO_RUNTIME_CHECKS
    #define VAL_TYPE VAL_TYPE_UNCHECKED
#else
    #define VAL_TYPE(v) \
        VAL_TYPE_UNCHECKED(Ensure_Readable(v))
#endif


//=//// GETTING, SETTING, and CLEARING VALUE FLAGS ////////////////////////=//
//
// The header of a cell contains information about what kind of cell it is,
// as well as some flags that are reserved for system purposes.  These are
// the NODE_FLAG_XXX and CELL_FLAG_XXX flags, that work on any cell.
//
// 1. Avoid cost that inline functions (even constexpr) add to checked builds
//    by "typechecking" via finding the name ->header.bits in (c).
//
// 2. Cell flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.  We do this on the HeaderUnion
//    vs. x_cast() on the (c) to get the typechecking of [1]

#define Get_Cell_Flag(c,name) /* [1] */ \
    ((Ensure_Readable(c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag(c,name) \
    ((Ensure_Readable(c)->header.bits & CELL_FLAG_##name) == 0)

#define Get_Cell_Flag_Unchecked(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag_Unchecked(c,name) \
    (((c)->header.bits & CELL_FLAG_##name) == 0)

#define Set_Cell_Flag(c,name) /* [2] */ \
    m_cast(union HeaderUnion*, &Ensure_Readable(c)->header)->bits \
        |= CELL_FLAG_##name

#define Clear_Cell_Flag(c,name) \
    m_cast(union HeaderUnion*, &Ensure_Readable(c)->header)->bits \
        &= ~CELL_FLAG_##name



//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//
//
// See Is_Fresh() and Assert_Cell_Initable() for the explanation of what
// "freshening" is, and why it tolerates CELL_MASK_0 in a cell header.

INLINE void Reset_Cell_Header_Untracked(Cell* c, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE_ANTIFORM_0);
    Freshen_Cell_Untracked(c);  // if CELL_MASK_0, node + cell flags not set
    c->header.bits |= (  // need to ensure node and cell flag get set
        NODE_FLAG_NODE | NODE_FLAG_CELL | flags | FLAG_QUOTE_BYTE(NOQUOTE_1)
    );
}


//=//// CELL PAYLOAD ACCESS ///////////////////////////////////////////////=//

#define PAYLOAD(Type,cell) \
    (cell)->payload.Type

#define Cell_Has_Node1(c) \
    Not_Cell_Flag_Unchecked((c), DONT_MARK_NODE1)

#define Cell_Has_Node2(c) \
    Not_Cell_Flag_Unchecked((c), DONT_MARK_NODE2)

#define Stringlike_Has_Node(c) /* make findable */ \
    Cell_Has_Node1(c)

#define Sequence_Has_Node(c) /* make findable */ \
    Cell_Has_Node1(c)

// Note: If incoming p is mutable, we currently assume that's allowed by the
// flag bits of the node.  This could have RUNTIME_CHECKS with a C++ variation
// that only takes mutable pointers.
//
#define Tweak_Cell_Node1(c,n) do { \
    STATIC_ASSERT_LVALUE(c);  /* macro repeats c, make sure calls are safe */ \
    assert(Cell_Has_Node1(c)); \
    PAYLOAD(Any, (c)).first.node = (n); \
} while (0)

#define Tweak_Cell_Node2(c,n) do { \
    STATIC_ASSERT_LVALUE(c);  /* macro repeats c, make sure calls are safe */ \
    assert(Cell_Has_Node2(c)); \
    PAYLOAD(Any, (c)).second.node = (n); \
} while (0)

#define Cell_Node1(c) \
    m_cast(Node*, PAYLOAD(Any, (c)).first.node)

#define Cell_Node2(c) \
    m_cast(Node*, PAYLOAD(Any, (c)).second.node)


//=///// BINDING //////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a Node which constitutes their notion of "binding".
//
// This can either be null (a.k.a. UNBOUND), or to a function's paramlist
// (indicates a relative binding), or to a context's varlist (which indicates
// a specific binding.)
//
//  1. Instead of using null for UNBOUND, a special global Stub struct was
//     experimented with.  It was at a memory location known at compile-time,
//     and had its ->header and ->info bits set in such a way as to avoid the
//     need for some conditional checks.  e.g. instead of writing:
//
//         if (binding and binding->header.bits & NODE_FLAG_MANAGED) {...}
//
//     The special UNBOUND stub set some bits, e.g. pretend to be managed:
//
//        if (binding->header.bits & NODE_FLAG_MANAGED) {...}  // UNBOUND ok
//
//     Question was whether avoiding the branching involved from the extra
//     test for null would be worth it for consistent dereferencing ability.
//     At least on x86/x64, the answer was: No. Maybe even a little slower.
//     Testing for null pointers the processor has in its hand is very common
//     and seemed to outweigh the need to dereference all the time.  The
//     increased clarity of having unbound be nullptr is also in its benefit.
//

#define EXTRA(Type,cell) \
    (cell)->extra.Type

#if NO_RUNTIME_CHECKS
    #define Assert_Cell_Binding_Valid(v) NOOP
#else
    #define Assert_Cell_Binding_Valid(v) \
        Assert_Cell_Binding_Valid_Core(v)
#endif

#define Cell_Binding(v) \
    x_cast(Context*, (v)->extra.Any.node)

#if NO_RUNTIME_CHECKS || NO_CPLUSPLUS_11
    #define BINDING(v) \
        *x_cast(Context**, m_cast(Node**, &(v)->extra.Any.node))
#else
    struct BindingHolder {
        Cell* & ref;

        BindingHolder(const Cell* const& ref)
            : ref (const_cast<Cell* &>(ref))
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
            ref->extra.Any.node = maybe right;
            Assert_Cell_Binding_Valid(ref);
        }

        Context* operator-> () const
          { return x_cast(Context*, ref->extra.Any.node); }

        operator Context* () const
          { return x_cast(Context*, ref->extra.Any.node); }
    };

    #define BINDING(v) \
        BindingHolder{v}

    template<typename T>
    struct cast_helper<BindingHolder,T> {
        static constexpr T convert(BindingHolder const& holder) {
            return cast(T, x_cast(Context*, holder.ref->extra.Any.node));
        }
    };

    INLINE void Corrupt_Pointer_If_Debug(BindingHolder const& bh) {
        bh.ref->extra.Any.node = p_cast(Context*, cast(uintptr_t, 0xDECAFBAD));
    }
#endif

#define SPECIFIED \
    x_cast(Context*, nullptr)  // x_cast (don't want DEBUG_CHECK_CASTS)

#define UNBOUND nullptr  // making this a stub did not improve performance [1]
#define UNSPECIFIED nullptr


//=//// COPYING CELLS /////////////////////////////////////////////////////=//
//
// Because you cannot assign cells to one another (e.g. `*dest = *src`), a
// function is used.  This provides an opportunity to check things like moving
// data into protected locations, and to mask out bits that should not be
// propagated.  We can also enforce that you can't copy an Atom into a Value
// or Element, and that you can't copy a Value into an Element...keeping
// antiforms and unstable antiforms out of places they should not be.
//
// Interface designed to line up with Derelativize()
//
// 1. If you write `Erase_Cell(dest)` followed by `Copy_Cell(dest, src)` the
//    optimizer seems to notice it doesn't need the masking of Freshen_Cell().
//    This was discovered by trying to force callers to pass in an already
//    freshened cell and seeing things get more complicated for no benefit.
//
// 2. Once upon a time binding init depended on the payload (when quoteds
//    could forward to a different cell), so this needed to be done first.
//    That's not true anymore, but some future INIT_BINDING() may need to
//    be able to study to the cell to do the initialization?
//
// 3. These overloads are the best I could come up with...but they conflict
//    if written naively.  The variant for (Init(Element), const Element*)
//    will compete with one as (Init(Value), const Value*) when the second
//    argument is Element*, since Element can be passed where Value is taken.
//    Template magic lets an overload exclude itself to break the contention.

INLINE void Copy_Cell_Header(
    Cell* out,
    const Cell* v
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    Assert_Cell_Readable(v);

    Freshen_Cell_Untracked(out);
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & CELL_MASK_COPY));

  #if DEBUG_TRACK_EXTEND_CELLS
    out->file = v->file;
    out->line = v->line;
    out->tick = TICK;
    out->touch = v->touch;  // see also arbitrary debug use via Touch_Cell()
  #endif
}

INLINE Cell* Copy_Cell_Untracked(
    Cell* out,
    const Cell* v,
    Flags copy_mask  // typically you don't copy PROTECTED, etc
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    Assert_Cell_Readable(v);

    Freshen_Cell_Untracked(out);  // optimizer elides this after erasure [1]
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & copy_mask));

    out->payload = v->payload;  // before init binding anachronism [2]

    out->extra = v->extra;  // binding or inert bits

  #if DEBUG_TRACK_EXTEND_CELLS
    out->file = v->file;
    out->line = v->line;
    out->tick = v->tick;
    out->touch = v->touch;
  #endif

    return out;
}

#if (! DEBUG_USE_CELL_SUBCLASSES)
    #define Copy_Cell(out,v) \
        TRACK(Copy_Cell_Untracked((out), (v), CELL_MASK_COPY))
#else
    INLINE Element* Copy_Cell_Overload(Init(Element) out, const Element* v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    template<  // avoid overload conflict when Element* coerces to Value* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T,const Value*>::value
            && !std::is_convertible<T,const Element*>::value
        >::type* = nullptr
    >
    INLINE Value* Copy_Cell_Overload(Init(Value) out, T v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    INLINE Atom* Copy_Cell_Overload(Init(Atom) out, const Atom* v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Copy_Cell(out,v) \
        Copy_Cell_Overload((out), (v))
#endif

#define Copy_Cell_Core(out,v,copy_mask) \
    Copy_Cell_Untracked((out), (v), (copy_mask))

#define Copy_Meta_Cell(out,v) \
    cast(Element*, \
        Meta_Quotify(Copy_Cell_Untracked((out), (v), CELL_MASK_COPY)))


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//
//
// Moving a cell invalidates the old location.  This idea is a potential
// prelude to being able to do some sort of reference counting on Arrays based
// on the cells that refer to them tracking when they are overwritten.  One
// advantage would be being able to leave the reference counting as-is.
//
// In the meantime, this just does a Copy + Freshen, where the freshening
// doesn't have to worry about overwriting raised errors.

INLINE Cell* Move_Cell_Untracked(
    Cell* out,
    Atom* v,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, v, copy_mask);  // Move_Cell() adds track to `out`
    Freshen_Cell_Suppress_Raised_Untracked(v);  // moved error, didn't erase it

  #if DEBUG_TRACK_EXTEND_CELLS  // `out` has tracking info we can use
    v->file = out->file;
    v->line = out->line;
    v->tick = TICK;
  #endif

    return out;
}

#if (! DEBUG_USE_CELL_SUBCLASSES)
    #define Move_Cell(out,v) \
        TRACK(Move_Cell_Untracked((out), (v), CELL_MASK_COPY))
#else
    INLINE Element* Move_Cell_Overload(Init(Element) out, Element* v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    template<  // avoid overload conflict when Element* coerces to Value* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T,Value*>::value
            && !std::is_convertible<T,Element*>::value
        >::type* = nullptr
    >
    INLINE Value* Move_Cell_Overload(Init(Value) out, T v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    INLINE Atom* Move_Cell_Overload(Init(Atom) out, Atom* v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Move_Cell(out,v) \
        TRACK(Move_Cell_Overload((out), (v)))
#endif

#define Move_Cell_Core(out,v,cell_mask) \
    TRACK(Move_Cell_Untracked((out), (v), (cell_mask)))

#define Move_Meta_Cell(out,v) \
    cast(Element*, Meta_Quotify(Move_Cell_Core((out), (v), CELL_MASK_COPY)))


// !!! Super primordial experimental `const` feature.  Concept is that various
// operations have to be complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.  const input always
// gets you const output, but mutable input will get you const output if
// the value itself is const (so it inherits).
//
INLINE Atom* Inherit_Const(Atom* out, const Cell* influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}
#define Trust_Const(value) \
    (value) // just a marking to say the const is accounted for already

INLINE Value* Constify(Value* v) {
    Set_Cell_Flag(v, CONST);
    return v;
}


//=//// DECLARATION HELPERS FOR ERASED CELLS ON THE C STACK ///////////////=//
//
// Cells can't hold random bits when you initialize them:.
//
//     Element element;               // cell contains random bits
//     Init_Integer(&element, 1020);  // invalid, as init checks protect bits
//
// The process of initialization checks to see if the cell is protected, and
// also masks in some bits to preserve with CELL_MASK_PERSIST.  You have to
// do something, for instance Erase_Cell():
//
//     Element element;
//     Erase_Cell(&element);  // one possibility for making inits valid
//     Init_Integer(&element, 1020);
//
// We can abstract this with a macro, that can also remove the need to use &,
// by making the passed-in name an alias for the address of the cell:
//
//     DECLARE_ELEMENT (element)
//     Init_Integer(element, 1020);
//
// However, Erase_Cell() has a header that's all 0 bits, which means it does
// not carry NODE_FLAG_NODE or NODE_FLAG_CELL.  We would like to be able to
// protect the lifetimes of these cells without giving them content:
//
//     DECLARE_ELEMENT (element);
//     Push_Lifeguard(element);
//
// But Push_Lifeguard() shouldn't be tolerant of erased cells.  So we assign
// the header with CELL_MASK_UNREADABLE instead of CELL_MASK_0.
//
// * These cells are not protected from having their insides GC'd unless
//   you guard them with Push_Lifeguard(), or if a routine you call protects
//   the cell implicitly (as stackful evaluations will do on cells used
//   as an output).
//
// * You can't use a cell on the C stack as the output target for the eval
//   of a stackless continuation, because the function where the cell lives
//   has to return control to the trampoline...destroying that stack memory.
//   If a native needs a cell besides OUT or SPARE to do evaluations into,
//   it should declare `<local>`s in its spec, and access them with the
//   LOCAL() macro.  These are GC safe, and are initialized to nothing.
//
// * Although writing CELL_MASK_UNREADABLE to the header is very cheap, it
//   still costs *something*.  In checked builds it can cost more to declare
//   the cell, because DEBUG_TRACK_EXTEND_CELLS makes TRACK() write
//   the file, line, and tick where the cell was initialized in the extended
//   space.  So it should generally be favored to put these declarations at
//   the outermost scope of a function, vs. inside a loop.
//

#define DECLARE_ATOM(name) \
    Atom name##_atom; \
    Atom* name = TRACK(&name##_atom); \
    name->header.bits = CELL_MASK_UNREADABLE

#define DECLARE_VALUE(name) \
    Value name##_value; \
    Value* name = TRACK(&name##_value); \
    name->header.bits = CELL_MASK_UNREADABLE

#define DECLARE_ELEMENT(name) \
    Element name##_element; \
    Element* name = TRACK(&name##_element); \
    name->header.bits = CELL_MASK_UNREADABLE
