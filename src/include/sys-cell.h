//
//  file: %sys-cell.h
//  summary: "Cell Definitions AFTER %tmp-internals.h}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2026 Ren-C Open Source Contributors
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// A. Many macros in this file are "evil" and repeat their arguments, because
//    in the checked build, functions aren't inlined--and the overhead adds up
//    very quickly.  Dodging a function speeds up these critical parts, and
//    they are wrapped up in inline functions for callers that don't mind
//    the cost.  The STATIC_ASSERT_LVALUE() macro catches potential violators.
//
// B. Some callers have things that aren't LValues, but nevertheless are ok
//    to pass to "evil macros", such as using addresses of local variables.
//    To allow these calls without an inline function, we offer explicitly
//    named "Xxx_Evil_Macro()" versions, that just don't have the check of
//    STATIC_ASSERT_LVALUE().
//
//    (Note that in C++ there's no way to tell the difference between &ref
//    and an expression with side effects that synthesized a reference.  A
//    macro like NO_SIDE_EFFECTS(&ref) could be designed to "approve" a
//    reference, but would have runtime cost in debug builds).
//


//=/// known() AND known_not() WRAPPERS ///////////////////////////////////=//
//
// known() and known_not() are helpful tools for writing macros that can do
// const-correct typechecks at compile-time, with no runtime cost.
//
// e.g. you can block the passing of Element* to routines that expect Stable*
// by using `known_not(Element*, v)` in a macro.  But that only works for when
// CHECK_CELL_SUBCLASSES is on.  If it's not, then Value* will be the same as
// Element*, and the check will fail.
//
// These wrappers can be no-ops in DONT_CHECK_CELL_SUBCLASSES builds.
//

#if DONT_CHECK_CELL_SUBCLASSES
    #define Known_Dual(v)         (v)
    #define Known_Element(v)      (v)
    #define Possibly_Antiform(v)  (v)
    #define Known_Stable(v)       (v)
    #define Possibly_Unstable(v)  (v)
    #define Known_Value(v)        (v)
    #define Possibly_Bedrock(v)   (v)
#else
    #define Known_Dual(v)         known(Dual*, (v))
    #define Known_Element(v)      known(Element*, (v))
    #define Possibly_Antiform(v)  known_not(Element*, (v))
    #define Known_Stable(v)       known(Stable*, (v))
    #define Possibly_Unstable(v)  known_not(Stable*, (v))
    #define Known_Value(v)        known(Value*, (v))
    #define Possibly_Bedrock(v)   known_not(Value*, (v))
#endif


//=//// As_Xxx() SELF-CAST PROTECTED CASTS ////////////////////////////////=//
//
// Generally speaking, cast() has to allow casting from a type to itself... or
// else higher-level macros become too unweildy.  But we'd like to catch any
// case of people trying to cast Element* to Element* (for instance) because
// that's noise in the code, and also may reprsent some misunderstanding.
//
// These macros provide the protection of making sure you're not doing a
// self-cast.  They also look a little cleaner at the callsite.
//
// Note: due to the CastHook<> mechanism, some builds instrument cast() to
// validate that the bits actually match the type (e.g. that you don't cast
// a Cell with an antiform LIFT_BYTE() to an Element*).
//

#define As_Dual(v)      cast(Dual*, (v))
#define As_Element(v)   cast(Element*, Possibly_Antiform(v))
#define As_Stable(v)    cast(Stable*, Possibly_Unstable(v))
#define As_Value(v)     cast(Value*, Possibly_Bedrock(v))


//=//// CELL READABLE + WRITABLE + INITABLE CHECKS ////////////////////////=//
//
// [READABILITY]
//
// Readable cells have BASE_FLAG_BASE and BASE_FLAG_CELL set.  It's important
// that they do, because if they don't then the first byte of the header
// could be mistaken for valid UTF-8.
//
// See Detect_Rebol_Pointer() for the machinery that relies upon this for
// mixing UTF-8, Cells, and Stubs in variadic API calls.
//
// [WRITABILITY]
//
// A writable cell is one that has BASE_FLAG_BASE and BASE_FLAG_CELL set, but
// that also does not have TRACK_FLAG_SHIELD_FROM_WRITES if debug tracking on.
//
// (Note that this is a distinct concept of writability from that coming from
// CELL_FLAG_CONST or CELL_FLAG_FINAL.  Much of that is enforced at compile
// time by the `const Cell*` convention, where you can't get a non-const Cell
// reference without going through a runtime check that makes sure the cell is
// not protected.)
//
// [INITABILITY]
//
// A special exception for writability is made for initialization, that
// allows cells with headers initialized to zero.  See Freshen_Cell() for why
// this is done and how it is taken advantage of.
//
// 1. One might think that because you're asking if a cell is writable that
//    the function should only take non-const Cells, but the question is
//    abstract and doesn't mean you're going to write it in the moment.
//    You might just be asking if it could be written if someone had non
//    const access to it.
//

#define CELL_MASK_ERASED_0  0  // "initable", but not readable or writable

#if (! DEBUG_CELL_READ_WRITE)  // these are all no-ops in release builds!
    #define Assert_Cell_Initable_Evil_Macro(c)  NOOP
    #define Assert_Cell_Writable_Evil_Macro(c)  NOOP

    #define Assert_Cell_Readable(c)    NOOP
    #define Assert_Cell_Writable(c)    NOOP
    #define Assert_Cell_Initable(c)    NOOP

    #define Ensure_Readable(c) (c)
    #define Ensure_Writable(c) (c)
#else
    #define Assert_Cell_Readable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        if ( \
            (((c)->header.bits) & ( \
                BASE_FLAG_BASE | BASE_FLAG_CELL | BASE_FLAG_UNREADABLE \
            )) != (BASE_FLAG_BASE | BASE_FLAG_CELL) \
        ){ \
            Crash_On_Unreadable_Cell(c); \
        } \
    } while (0)

  #if DEBUG_TRACK_EXTEND_CELLS  // add TRACK_FLAG_SHIELD_FROM_WRITES check
    #define Assert_Cell_Writable_Evil_Macro(c) do { \
        DONT(STATIC_ASSERT_LVALUE(out));  /* evil on purpose [B] */ \
        if ( \
            ((((c)->header.bits) & (BASE_FLAG_BASE | BASE_FLAG_CELL)) \
                != (BASE_FLAG_BASE | BASE_FLAG_CELL)) \
            or (((c)->track_flags.bits) & TRACK_FLAG_SHIELD_FROM_WRITES) \
        ){ \
            Crash_On_Unwritable_Cell(c);  /* despite write, pass const [1] */ \
        } \
    } while (0)
  #else
    #define Assert_Cell_Writable_Evil_Macro(c) do { \
        DONT(STATIC_ASSERT_LVALUE(out));  /* evil on purpose [B] */ \
        if ( \
            (((c)->header.bits) & (BASE_FLAG_BASE | BASE_FLAG_CELL)) \
                != (BASE_FLAG_BASE | BASE_FLAG_CELL) \
        ){ \
            Crash_On_Unwritable_Cell(c);  /* despite write, pass const [1] */ \
        } \
    } while (0)
  #endif

    #define Assert_Cell_Writable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        Assert_Cell_Writable_Evil_Macro(c); \
    } while (0)

    #define Assert_Cell_Initable_Evil_Macro(c) do { \
        DONT(STATIC_ASSERT_LVALUE(out));  /* evil on purpose [B] */ \
        if ((c)->header.bits != CELL_MASK_ERASED_0)  /* 0 is initable */ \
            Assert_Cell_Writable_Evil_Macro(c);  /* else need NODE and CELL */ \
    } while (0)

    #define Assert_Cell_Initable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        Assert_Cell_Initable_Evil_Macro(c); \
    } while (0)

  #if NO_CPLUSPLUS_11
    #define Ensure_Readable(c) (c)
    #define Ensure_Writable(c) (c)
  #else
    template<typename T>
    const T& Ensure_Readable(const T& cell) {
        Assert_Cell_Readable(cell);
        return cell;
    }

    template<typename T>
    const T& Ensure_Writable(const T& cell) {
        Assert_Cell_Writable(cell);
        return cell;
    }
  #endif
#endif


//=//// CELL ALIGMENT CHECKING ////////////////////////////////////////////=//
//
// See notes on ALIGN_SIZE regarding why we check this, and when it does and
// does not apply (some platforms need this invariant for `double` to work).
//
#if (! CHECK_MEMORY_ALIGNMENT)
    #define Assert_Cell_Aligned(c)  NOOP
#else
    #define Assert_Cell_Aligned(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        if (p_cast(uintptr_t, (c)) % ALIGN_SIZE != 0) \
            Panic_Cell_Unaligned(c); \
    } while (0)
#endif


//=//// CELL "POISONING" //////////////////////////////////////////////////=//
//
// Poisoning is used in the spirit of things like Address Sanitizer to block
// reading or writing locations such as beyond the allocated memory of an
// Array Flex.  It leverages the checks done by Ensure_Readable() and by
// Ensure_Writable()
//
// 1. To stop reading but not writing, use Init_Unreadable() cells instead.
//
// 2. Poison cells are designed to be used in places where overwriting all
//    the header bits won't lose important information.  For instance: it's
//    used in the optimized array representation that fits 0 or 1 cells into
//    the Array Stub itself.  But if you were to poison an API handle it would
//    overwrite BASE_FLAG_ROOT, and a managed pairing would overwrite
//    BASE_FLAG_MANAGED.  This check helps make sure you're not losing
//    important information.
//
// 3. Sometimes you want to set a cell in uninitialized memory to poison,
//    in which case the checks in [2] simply can't be used.
//
// 4. A key use of poison cells in the release build is to denote when an
//    array flex is empty in the optimized state.  But if it's not empty,
//    a lot of states are valid when checking the length.  It's not clear
//    what assert (if any) should be here.

#define CELL_MASK_POISON /* no read or write [1] */ \
    ((not BASE_FLAG_BASE) | BASE_FLAG_UNREADABLE | (not BASE_FLAG_CELL))

#if DEBUG_TRACK_EXTEND_CELLS
    #define Assert_Cell_Header_Overwritable(c) do {  /* conservative [2] */ \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        assert( \
            ((c)->header.bits == CELL_MASK_POISON \
                or (c)->header.bits == CELL_MASK_ERASED_0 \
                or (BASE_FLAG_BASE | BASE_FLAG_CELL) == ((c)->header.bits & \
                (BASE_FLAG_BASE | BASE_FLAG_CELL \
                    | BASE_FLAG_ROOT | BASE_FLAG_MARKED \
                    | BASE_FLAG_MANAGED) \
                )) \
            and not ((c)->track_flags.bits & TRACK_FLAG_SHIELD_FROM_WRITES) \
        ); \
    } while (0)
#else
    #define Assert_Cell_Header_Overwritable(c) do {  /* conservative [2] */ \
        STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
        assert( \
            (c)->header.bits == CELL_MASK_POISON \
            or (c)->header.bits == CELL_MASK_ERASED_0 \
            or (BASE_FLAG_BASE | BASE_FLAG_CELL) == ((c)->header.bits & \
                (BASE_FLAG_BASE | BASE_FLAG_CELL \
                    | BASE_FLAG_ROOT | BASE_FLAG_MARKED \
                    | BASE_FLAG_MANAGED) \
            ) \
        ); \
    } while (0)
#endif

INLINE Cell* Poison_Cell_Untracked(Cell* c) {
  #if DEBUG_POISON_UNINITIALIZED_CELLS
    Assert_Cell_Header_Overwritable(c);
  #endif
    c->header.bits = CELL_MASK_POISON;
    return c;
}

#define Poison_Cell(c)  /* checked version [2] */ \
    TRACK(Poison_Cell_Untracked(c))

INLINE Cell* Force_Poison_Cell_Untracked(Cell* c) {  // for random bits [3]
    Assert_Cell_Aligned(c);  // only have to check on first initialization
    c->header.bits = CELL_MASK_POISON;
    return c;
}

#define Force_Poison_Cell(c)  /* unchecked version, use sparingly! [3] */ \
    FORCE_TRACK_0(Force_Poison_Cell_Untracked(c))

#define Is_Cell_Poisoned(c)  /* non-poison state not always readable [4] */ \
    (known(Cell*, (c))->header.bits == CELL_MASK_POISON)


//=//// CELL "ERASING" ////////////////////////////////////////////////////=//
//
// To help be robust, the code ensures that BASE_FLAG_BASE and BASE_FLAG_CELL
// are set in the header of a memory slot before reading or writing info for
// a cell.  But an exception is made for efficiency that allows initialization
// in the case of a header that is all zeros.  This pattern is efficiently
// produced by memset(), and global memory for a C program is initialized to
// all zeros to protect leakage from other processes...so it's good to be
// able to take advantage of it *where possible*  (see [1]).
//
// 1. If you do not fully control the location you are writing, Erase_Cell()
//    is NOT what you want to use to make a cell writable.  You could be
//    overwriting persistent cell bits such as BASE_FLAG_ROOT that indicates
//    an API handle, or BASE_FLAG_MANAGED that indicates a Pairing.  This is
//    to be used for evaluator-controlled cells (OUT, SPARE, SCRATCH), or
//    restoring 0-initialized global variables back to the 0-init state, or
//    things like that.
//
// 2. In cases where you are trying to erase a cell in uninitialized memory,
//    you can't do the checks for [1].

INLINE Cell* Erase_Cell_Untracked(Cell* c) {
  #if DEBUG_POISON_UNINITIALIZED_CELLS
    Assert_Cell_Header_Overwritable(c);
  #endif
    c->header.bits = CELL_MASK_ERASED_0;
    return c;
}

#define Erase_Cell(c) \
    TRACK(Erase_Cell_Untracked(c))  // not safe on API cells [1]

INLINE Cell* Force_Erase_Cell_Untracked(Cell* c) {
    Assert_Cell_Aligned(c);  // only have to check on first initialization
    c->header.bits = CELL_MASK_ERASED_0;
    return c;
}

#define Force_Erase_Cell(c)  /* unchecked version, use sparingly! [2] */ \
    FORCE_TRACK_0(Force_Erase_Cell_Untracked(c))

#define Is_Cell_Erased(c) /* initable, not read/writable */ \
    (known(Cell*, (c))->header.bits == CELL_MASK_ERASED_0)

#define Not_Cell_Erased(c)  (not Is_Cell_Erased(c))

#if DEBUG_POISON_UNINITIALIZED_CELLS
    INLINE Init(Value) Force_Init_Cell(Cell* out) {
        assert(Is_Cell_Poisoned(out) or Is_Cell_Erased(out));
        return Force_Erase_Cell(out);
    }
#else
    #define Force_Init_Cell(out) (out)
#endif


//=//// UNREADABLE CELLS //////////////////////////////////////////////////=//
//
// Unreadable cells are write-only cells.  They will give errors on attempts
// to read from them e.g. with Type_Of(), which is similar to erased cells.
// But with the advantage that they have BASE_FLAG_BASE and BASE_FLAG_CELL
// set in their header, hence they do not conflate with empty UTF-8 strings.
//
// Erased cells (with all zero bits in their header) cannot be used in places
// where format bits have to be set, like NODE_FLAG_ROOT for API handles.  The
// GC also tolerates them in places where an erased cell would trigger an
// assertion indicating an element hadn't been initialized.  So they have
// important applications despite some similiarities to erased cells.
//
// They're not legal in Source arrays exposed to users.  But are found in
// other places, such as in MAP! to denote "zombie" slots.
//
// 1. Setting a cell unreadable does not affect bits like BASE_FLAG_ROOT
//    or BASE_FLAG_MARKED, so it's "safe" to use them with cells that need
//    these persistent bits preserved.
//
// 2. Strange as it may seem, the memset()-based corruption of the payload
//    is faster than two pointer-optimized corruptions of split.one and
//    split.two...at least in Callgrind's accounting.
//
// 3. If you're going to set uninitialized memory to an unreadable cell,
//    then the unchecked Force_Unreadable_Cell() has to be used, because
//    you can't Assert_Cell_Initable() on random bits.

#define CELL_MASK_UNREADABLE \
    (BASE_FLAG_BASE | BASE_FLAG_CELL | BASE_FLAG_UNREADABLE \
        | CELL_FLAG_DONT_MARK_PAYLOAD_1 | CELL_FLAG_DONT_MARK_PAYLOAD_2 \
        | FLAG_KIND_BYTE(255) | FLAG_LIFT_BYTE(255))

#if CORRUPT_CELL_HEADERS_ONLY
    #define Init_Unreadable_Untracked_Evil_Macro(out) do { \
        DONT(STATIC_ASSERT_LVALUE(out));  /* evil on purpose [B] */ \
        Assert_Cell_Initable_Evil_Macro(out); \
        (out)->header.bits |= CELL_MASK_UNREADABLE;  /* bitwise OR [1] */ \
    } while (0)
#else
    #define Init_Unreadable_Untracked_Evil_Macro(out) do { \
        DONT(STATIC_ASSERT_LVALUE(out));  /* evil on purpose [B]*/ \
        Assert_Cell_Initable_Evil_Macro(out); \
        (out)->header.bits |= CELL_MASK_UNREADABLE;  /* bitwise OR [1] */ \
        Corrupt_If_Needful((out)->extra.corrupt); \
        Corrupt_If_Needful((out)->payload);  /* split.one/two slower [2] */ \
    } while (0)
#endif

#define Init_Unreadable_Untracked(out) do { \
    STATIC_ASSERT_LVALUE(out);  /* see [A] */ \
    Init_Unreadable_Untracked_Evil_Macro(out); \
} while (0)

INLINE Cell* Init_Unreadable_Untracked_Inline(Cell* out) {
    Init_Unreadable_Untracked(out);
    return out;
}

#define Force_Unreadable_Cell_Untracked(out) \
    ((out)->header.bits = CELL_MASK_UNREADABLE)

INLINE Cell* Force_Unreadable_Cell_Untracked_Inline(Cell* out) {
    Force_Unreadable_Cell_Untracked(out);
    return out;
}

#define Force_Unreadable_Cell(out)  /* unchecked, use sparingly! [3] */ \
    Force_Unreadable_Cell_Untracked_Inline(TRACK(out))

INLINE bool Is_Cell_Readable(const Cell* c) {
    if (Is_Base_Readable(c)) {
        Assert_Cell_Readable(c);  // also needs BASE_FLAG_BASE, BASE_FLAG_CELL
        return true;
    }
    assert((c->header.bits & CELL_MASK_UNREADABLE) == CELL_MASK_UNREADABLE);
    return false;
}

#define Not_Cell_Readable(c)  (not Is_Cell_Readable(c))

#define Init_Unreadable(out) \
    TRACK(Init_Unreadable_Untracked_Inline((out)))

#if NEEDFUL_DOES_CORRUPTIONS
    #define Corrupt_Cell_If_Needful(c)  USED(Init_Unreadable(c))
#else
    #define Corrupt_Cell_If_Needful(c)  NOOP
#endif

#if NEEDFUL_USES_CORRUPT_HELPER
  //
  // We don't actually want things like Sink(Stable) to set a cell's bits to
  // a corrupt pattern, as we need to be able to call Init_Xxx() routines
  // and can't do that on garbage.  But we don't want to Erase_Cell() either
  // because that would lose header bits like whether the cell is an API
  // value.  We use the Init_Unreadable_Untracked().
  //
  // 1. For const Cell subclasses (e.g. const Stable), use no-op corruption.
  //    This avoids instantiating the generic CorruptHelper<T> in
  //    needful-corruption.hpp, which would try to memset() a const object
  //    and trigger GCC errors (and UB).  The reason we need to silently
  //    accept attempts to corrupt const Cells is due to how generic cast()
  //    works: it makes a const type out of whatever it got, and may or may
  //    not turn it back mutable ("lenient" constness semantics).

  namespace needful {
    template<typename T>
    struct CorruptHelper<
        T,
        typename std::enable_if<
            std::is_base_of<Cell, typename std::remove_const<T>::type>::value
            and not std::is_const<T>::value
        >::type
    >{
        static void corrupt(T& ref) {
            Init_Unreadable_Untracked_Evil_Macro(&ref);  // &ref needs evil [B]
        }
    };

    template<typename T>
    struct CorruptHelper<
        T,
        typename std::enable_if<
            std::is_base_of<Cell, typename std::remove_const<T>::type>::value
            and std::is_const<T>::value
        >::type
    >{
        static void corrupt(T&) {
            // no-op for const Cell-derived types [1]
        }
    };
  }
#endif


//=//// CELL "FRESHNESS" //////////////////////////////////////////////////=//
//
// Most read and write operations of cells assert that the header has both
// BASE_FLAG_BASE and BASE_FLAG_CELL set.  But there is an exception made when
// it comes to initialization: a cell is allowed to have a header that is all
// 0 bits (e.g. CELL_MASK_ERASED_0).  Ranges of cells can be memset() to 0
// quickly, and the OS sets C globals to all 0 bytes when the process starts
// for security reasons.
//
// So a "fresh" cell is one that does not need to have its CELL_MASK_PERSIST
// portions masked out.  An initialization routine can just bitwise OR the
// flags it wants overlaid on the persisted flags (if any).  However, it
// should include BASE_FLAG_BASE and BASE_FLAG_CELL in that masking in case
// they weren't there.
//
// Fresh cells can occur "naturally" (from memset() or other 0 memory), be
// made manually with Erase_Cell(), or an already initialized cell can have
// its non-CELL_MASK_PERSIST portions wiped out with Freshen_Cell().
//
// Note if TRACK_FLAG_SHIELD_FROM_WRITES is set on a cell, it's not considered
// fresh for initialization.  So the flag must be cleared or the cell "hard"
// erased (with Force_Erase_Cell()) in order to overwrite it.
//
// 1. "evil macros" for checked build performance, see STATIC_ASSERT_LVALUE()
//

#define CELL_MASK_PERSIST \
    (BASE_FLAG_MANAGED | BASE_FLAG_ROOT | BASE_FLAG_MARKED | CELL_FLAG_FORMAT)

#define Freshen_Cell_Header(c) do { \
    STATIC_ASSERT_LVALUE(c);  /* see [A] */ \
    Assert_Cell_Initable(c);  /* if CELL_MASK_ERASED_0, no node+cell flags */ \
    (c)->header.bits &= CELL_MASK_PERSIST;  /* won't add node+cell flags */ \
} while (0)

STATIC_ASSERT(not (CELL_MASK_PERSIST & CELL_FLAG_NOTE));


//=//// GETTING, SETTING, and CLEARING VALUE FLAGS ////////////////////////=//
//
// The header of a cell contains information about what kind of cell it is,
// as well as some flags that are reserved for system purposes.  These are
// the BASE_FLAG_XXX and CELL_FLAG_XXX flags, that work on any cell.
//
// 1. Cell flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.

#define Get_Cell_Flag(c,name) \
    ((Ensure_Readable(c)->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag(c,name) \
    ((Ensure_Readable(c)->header.bits & CELL_FLAG_##name) == 0)

#define Get_Cell_Flag_Unchecked(c,name) \
    ((known(Cell*, (c))->header.bits & CELL_FLAG_##name) != 0)

#define Not_Cell_Flag_Unchecked(c,name) \
    ((known(Cell*, (c))->header.bits & CELL_FLAG_##name) == 0)

#define Set_Cell_Flag(c,name) /* cast away const [1] */ \
    m_cast(HeaderUnion*, &Ensure_Readable(c)->header)->bits \
        |= CELL_FLAG_##name

#define Clear_Cell_Flag(c,name) /* cast away const [1] */ \
    m_cast(HeaderUnion*, &Ensure_Readable(c)->header)->bits \
        &= ~CELL_FLAG_##name


//=//// CELL TYPE-SPECIFIC "CRUMB" ////////////////////////////////////////=//
//
// The cell flags are structured so that the top two bits of the byte are
// "type specific", so that you can just take the last 2 bits.  This 2-bit
// state (called a "crumb") holds the one of four possible infix states for
// actions--for example.
//
// THEY ARE THE LAST TWO BITS ON PURPOSE.  If they needed to be shifted, the
// fact that there's no unit smaller than a byte means static analyzers
// will warn you about overflow if any shifting is involved, e.g.:
//
//     (((crumb << 6)) << 24)  <-- generates uintptr_t overflow warning
//

STATIC_ASSERT(
    CELL_FLAG_TYPE_SPECIFIC_A == FLAG_LEFT_BIT(30)
    and CELL_FLAG_TYPE_SPECIFIC_B == FLAG_LEFT_BIT(31)
);

#define CELL_MASK_CRUMB \
    (CELL_FLAG_TYPE_SPECIFIC_A | CELL_FLAG_TYPE_SPECIFIC_B)

#define Get_Cell_Crumb(c) \
    (FOURTH_BYTE(&(c)->header.bits) & 0x3)

#define FLAG_CELL_CRUMB(crumb) \
    FLAG_FOURTH_BYTE(crumb)

INLINE void Set_Cell_Crumb(Cell* c, Crumb crumb) {
    c->header.bits &= ~(CELL_MASK_CRUMB);
    c->header.bits |= FLAG_CELL_CRUMB(crumb);
}


//=//// FAST JOINT HEART AND LIFT CHECK ///////////////////////////////////=//
//
// This macro is used to check if a cell has a particular heart and lift
// combination, and does so by testing the header bits against a mask which
// can be calculated at compile-time.
//
// Note that Ensure_Readable() is a no-op in the release build.
//

#define Unchecked_Cell_Has_Lift_Sigil_Heart(cell,lift,sigil,heart) \
    (((cell)->header.bits & CELL_MASK_HEART_AND_SIGIL_AND_LIFT) \
        == (FLAG_SIGIL(sigil) | FLAG_HEART(heart) | FLAG_LIFT_BYTE(lift)))

#define Cell_Has_Lift_Sigil_Heart(cell,lift,sigil,heart) \
    Unchecked_Cell_Has_Lift_Sigil_Heart(Ensure_Readable(cell), \
        (lift), (sigil), (heart))

#define Unchecked_Cell_Has_Lift_Heart_No_Sigil(cell,lift,heart) \
    Unchecked_Cell_Has_Lift_Sigil_Heart((cell), (lift), SIGIL_0, (heart))

#define Cell_Has_Lift_Heart_No_Sigil(cell,lift,heart) \
    Unchecked_Cell_Has_Lift_Heart_No_Sigil(Ensure_Readable(cell), \
        (lift), (heart))


//=//// HOOKABLE KIND_BYTE() ACCESSOR ////////////////////////////////////=//
//
// It can often be helpful to inject code to when the KIND_BYTE() is being
// assigned.  This mechanism also intercepts reads of the KIND_BYTE() too,
// which is done pervasively.  It slows down the code in checked builds by
// a noticeable amount, so we don't put it in all checked builds...only
// special situations.
//
// 1. We don't bother with const correctness in this debugging aid, as the
//    regular build will enforce that.  Cast away constness for simplicity.

#if (! DEBUG_HOOK_KIND_BYTE)
    #define KIND_BYTE(cell) \
        KIND_BYTE_RAW(cell)
#else
    struct KindHolder {  // class for intercepting heart assignments
        Cell* cell;

        KindHolder(const Cell* cell)
            : cell (const_cast<Cell*>(cell))
          {}

        operator KindByte() const {
            /* add read checks you want here */
            return KIND_BYTE_RAW(cell);
        }

        void operator=(KindByte right) {
            /* add write checks you want here */
            KIND_BYTE_RAW(cell) = right;
        }

        void operator=(const KindHolder& right)  // must write explicitly
          { *this = u_cast(KindByte, right); }

        ENABLE_IF_EXACT_ARG_TYPE(HeartEnum, KindHolder, Heart)
        void operator=(T right)
          { *this = i_cast(KindByte, right); }  // inherit operator= checks

        ENABLE_IF_EXACT_ARG_TYPE(HeartEnum, Heart)
        explicit operator T() const   // inherit Byte() cast extraction checks
          { return i_cast(T, i_cast(Byte, *this)); }
    };

    INLINE bool operator==(const KindHolder& holder, HeartEnum h)
      { return KIND_BYTE_RAW(holder.cell) == i_cast(Byte, h); }

    INLINE bool operator==(HeartEnum h, const KindHolder& holder)
      { return i_cast(Byte, h) == KIND_BYTE_RAW(holder.cell); }

    INLINE bool operator!=(const KindHolder& holder, HeartEnum h)
      { return KIND_BYTE_RAW(holder.cell) != i_cast(Byte, h); }

    INLINE bool operator!=(HeartEnum h, const KindHolder& holder)
      { return i_cast(Byte, h) != KIND_BYTE_RAW(holder.cell); }

    #define KIND_BYTE(cell) \
        KindHolder{cell}
#endif

#define Unchecked_Heart_Of(c) \
    i_cast(Option(Heart), \
        i_cast(HeartEnum, KIND_BYTE_RAW(c) & KIND_BYTEMASK_HEART_0x3F))

#define Heart_Of(c) \
    Unchecked_Heart_Of(Ensure_Readable(c))

INLINE Heart Heart_Of_Unsigiled_Isotopic(const Cell* c) {
    KindByte kind_byte = KIND_BYTE(c);
    assert(0 == kind_byte >> KIND_SIGIL_SHIFT);
    return i_cast(Heart, kind_byte);
}

INLINE Option(Heart) Heart_Of_Fundamental(const Cell* c) {
    assert(LIFT_BYTE_RAW(c) == NOQUOTE_3);
    return Heart_Of(c);
}

INLINE Heart Heart_Of_Builtin(const Cell* c) {
    Option(Heart) heart = Heart_Of(c);
    assert(heart != TYPE_0);
    return opt heart;  // faster than unwrap, we already checked for 0
}

INLINE Heart Heart_Of_Builtin_Fundamental(const Element* c) {
    assert(LIFT_BYTE_RAW(c) == NOQUOTE_3);
    Option(Heart) heart = Heart_Of(c);
    assert(heart != TYPE_0);
    return opt heart;  // faster than unwrap, we already checked for 0
}

#define Heart_Of_Is_0(cell) \
    (TYPE_0 == opt Heart_Of(cell))

INLINE bool Type_Of_Is_0(const Cell* cell) {
    return Heart_Of_Is_0(cell) and LIFT_BYTE_RAW(cell) == NOQUOTE_3;
}


//=//// HOOKABLE LIFT_BYTE() ACCESSOR /////////////////////////////////////=//
//
// While all datatypes have quoted forms, only some have quasiforms/antiforms.
// For instance: paths don't have them, because ~/foo/~ is a 3-element path
// with quasi-blanks at the head and tail, so no quasiform exists).
//
// This mechanism captures manipulations of the LIFT_BYTE() to be sure the
// bad forms don't get made.
//
// 1. We don't bother with const correctness in this debugging aid, as the
//    regular build will enforce that.  Cast away constness for simplicity.

#if (! DEBUG_HOOK_LIFT_BYTE)
    #define LIFT_BYTE(cell) \
        LIFT_BYTE_RAW(cell)
#else
    struct LiftHolder {  // class for intercepting lift assignments
        Cell* cell;

        template<typename T>
        LiftHolder(T&& wrapper)
            : cell (m_cast(Cell*, std::forward<T>(wrapper)))
        {}       // ^-- m_cast const Cell* for simplicity [1]

        operator LiftByte() const {
            /* add read checks you want here */
            return LIFT_BYTE_RAW(cell);
        }

        void operator=(int right) {
            assert(right >= 0 and right <= 255);

            Option(Heart) heart = Unchecked_Heart_Of(cell);
            if (right != BEDROCK_0 and not (right & NONQUASI_BIT))
                assert(Any_Isotopic_Type(heart));  // has quasiforms/antiforms

            /* add write checks you want here */

            LIFT_BYTE_RAW(cell) = right;
        }

        void operator=(const Lift_1_Struct& right) = delete;
        void operator=(const Lift_2_Struct& right) = delete;
        void operator=(const Lift_4_Struct& right) = delete;

        void operator=(const LiftHolder& right)  // must write explicitly
          { *this = u_cast(LiftByte, right); }

        void operator-=(int shift)  // must write explicitly
          { LIFT_BYTE_RAW(cell) -= shift; }

        void operator+=(int shift)  // must write explicitly
          { LIFT_BYTE_RAW(cell) += shift; }
    };

    #define LIFT_BYTE(cell) \
        LiftHolder{cell}
#endif


//=//// VALUE TYPE (always TYPE_XXX <= MAX_TYPEBYTE) //////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...though it's a WORD! underneath.
//
// (Instead of Type_Of(), use Heart_Of() if you wish to know that the cell
// pointer you pass in is carrying a word payload.  It disregards the quotes.)
//
// Note that these functions return Option(Type) because TYPE_0 is how
// "extension types" are reported (things not in the 63 builtin-heart range).
//
// 1. Type_Of() is called *a lot*, so it's worth it to manually inline the
//    logic for Underlying_Type_Of() inside of Type_Of(), because C/C++
//    compilers do not honor `inline` in debug builds.
//
// 2. KIND_BYTE() and LIFT_BYTE() in certain checked builds have overhead
//    (creating actual wrapper objects to monitor reads/writes of the byte
//    to check invariants).  We don't want to pay that overhead on every
//    Type_Of() call.  Use KIND_BYTE_RAW() and LIFT_BYTE_RAW().
//
//    (However, if one were trying to catch certain bugs, it might be worth
//    it to change these to non-raw calls temporarily.)
//

INLINE Option(Type) Underlying_Type_Of_Unchecked(  // inlined in Type_Of() [1]
    const Stable* v
){
    assert(LIFT_BYTE_RAW(v) != BEDROCK_0);

    if (KIND_BYTE_RAW(v) <= MAX_HEARTBYTE)  // raw [2]
        return i_cast(HeartEnum, KIND_BYTE_RAW(v));

    return Type_Enum_For_Sigil_Unchecked(
        i_cast(Sigil, KIND_BYTE_RAW(v) >> KIND_SIGIL_SHIFT)
    );
}

INLINE Option(Type) Type_Of_Unchecked(const Value* v) {
    switch (  // branches are in order of commonality (nonquoted first)
        LIFT_BYTE_RAW(v)  // raw [2]
    ){
      case NOQUOTE_3: {  // inlining of Underlying_Type_Of_Unchecked() [1]
        if (KIND_BYTE_RAW(v) <= MAX_HEARTBYTE)  // raw [2]
            return i_cast(HeartEnum, KIND_BYTE_RAW(v));

        return Type_Enum_For_Sigil_Unchecked(
            i_cast(Sigil, KIND_BYTE_RAW(v) >> KIND_SIGIL_SHIFT)
        ); }

      case STABLE_ANTIFORM_2:
        assert(KIND_BYTE_RAW(v) <= MAX_HEARTBYTE);  // raw [2]
        return i_cast(TypeEnum, KIND_BYTE_RAW(v) + MAX_TYPEBYTE_ELEMENT);

      case QUASIFORM_4:
        return TYPE_QUASIFORM;

      case UNSTABLE_ANTIFORM_1:
        assert(KIND_BYTE_RAW(v) <= MAX_HEARTBYTE);  // raw [2]
        return i_cast(TypeEnum, KIND_BYTE_RAW(v) + MAX_TYPEBYTE_ELEMENT);

    #if RUNTIME_CHECKS
      case BEDROCK_0:
        crash ("Unexpected lift byte value BEDROCK_0 for Value* (not Slot*)");
    #endif

      default:
        return TYPE_QUOTED;
    }
}

#if NO_RUNTIME_CHECKS
    #define Underlying_Type_Of  Underlying_Type_Of_Unchecked
    #define Type_Of  Type_Of_Unchecked
    #define Type_Of_Maybe_Unstable  Type_Of_Unchecked
#else
    #define Underlying_Type_Of(v) \
        Underlying_Type_Of_Unchecked(Ensure_Readable(Known_Stable(v)))

    #define Type_Of(v) \
        Type_Of_Unchecked(Ensure_Readable(Known_Stable(v)))

    #define Type_Of_Maybe_Unstable(v) \
        Type_Of_Unchecked(Ensure_Readable(v))
#endif

#define Datatype_Of(v) \
    Datatype_Of_Possibly_Unstable(Known_Stable(v))

INLINE Option(Type) Type_Of_When_Unquoted(const Element* elem) {
    if (LIFT_BYTE(elem) == QUASIFORM_4)
        return TYPE_QUASIFORM;

    assert(LIFT_BYTE(elem) > STABLE_ANTIFORM_2);
    return Underlying_Type_Of(elem);
}


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//
//
// Assert_Cell_Initable() for the explanation of what "freshening" is, and why
// it tolerates CELL_MASK_ERASED_0 in a cell header.

INLINE void Reset_Cell_Header_Noquote(Cell* c, uintptr_t flags)
{
    assert((flags & CELL_MASK_LIFT) == FLAG_LIFT_BYTE(BEDROCK_0));
    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        BASE_FLAG_BASE | BASE_FLAG_CELL | flags | FLAG_LIFT_BYTE(NOQUOTE_3)
    );
}

INLINE void Reset_Cell_Header(Cell* c, uintptr_t flags)
{
    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        BASE_FLAG_BASE | BASE_FLAG_CELL | flags
    );
}

INLINE void Reset_Extended_Cell_Header_Noquote(
    Cell* c,
    const ExtraHeart* extra_heart,
    uintptr_t flags
){
    assert((flags & CELL_MASK_HEART_AND_SIGIL_AND_LIFT) == 0);

    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        BASE_FLAG_BASE | BASE_FLAG_CELL | flags | FLAG_LIFT_BYTE(NOQUOTE_3)
    );
    c->extra.base = m_cast(ExtraHeart*, extra_heart);
}


//=//// CELL PAYLOAD ACCESS ///////////////////////////////////////////////=//

#define Cell_Payload_1_Needs_Mark(c) \
    Not_Cell_Flag_Unchecked((c), DONT_MARK_PAYLOAD_1)

#define Cell_Payload_2_Needs_Mark(c) \
    Not_Cell_Flag_Unchecked((c), DONT_MARK_PAYLOAD_2)

#define Stringlike_Has_Stub(c) /* make findable */ \
    Cell_Payload_1_Needs_Mark(c)

#define Sequence_Has_Pointer(c) /* make findable */ \
    Cell_Payload_1_Needs_Mark(c)


//=//// CELL NODE EXTRACTORS FOR CLARIFYING SLOT USAGE ////////////////////=//
//
// There was a general decision against "trickery" which makes higher level
// checked operations look like assignments, favoring Cell_Xxx() and
// Tweak_Cell_Xxx() operations:
//
//     https://forum.rebol.info/t/c-magic-for-lvalue-checking/2350
//
// However, there is value in making it possible to map out slots in a cell
// with a single define that can be used by those functions.  This way, you
// can do:
//
//     #define CELL_SOMETHING_PROPERTY_A(c)  CELL_EXTRA(c)
//     #define CELL_SOMETHING_PROPERTY_B(c)  CELL_PAYLOAD_1(c)
//     #define CELL_SOMETHING_PROPERTY_C(c)  CELL_PAYLOAD_2(c)
//
// Then, don't use CELL_PAYLOAD_1/NODE2/EXTRA in any of the implementation.  This
// makes it much easier to see up front what a certain cell's use of its
// slots is, and a lot easier to adjust when there are changes.
//
#if (! DEBUG_CHECK_GC_HEADER_FLAGS)
    #define Ensure_Cell_Extra_Needs_Mark(c)      (c)
    #define Ensure_Cell_Payload_1_Needs_Mark(c)  (c)
    #define Ensure_Cell_Payload_2_Needs_Mark(c)  (c)
#else
    MUTABLE_IF_C(Cell*, INLINE) Ensure_Cell_Extra_Needs_Mark(
        CONST_IF_C(Cell*) cell
    ){
        CONSTABLE(Cell*) c = m_cast(Cell*, cell);
        assert(Heart_Implies_Extra_Needs_Mark(Unchecked_Heart_Of(c)));
        return c;
    }

    MUTABLE_IF_C(Cell*, INLINE) Ensure_Cell_Payload_1_Needs_Mark(
        CONST_IF_C(Cell*) cell
    ){
        CONSTABLE(Cell*) c = m_cast(Cell*, cell);
        assert(Not_Cell_Flag(c, DONT_MARK_PAYLOAD_1));
        return c;
    }

    MUTABLE_IF_C(Cell*, INLINE) Ensure_Cell_Payload_2_Needs_Mark(
        CONST_IF_C(Cell*) cell
    ){
        CONSTABLE(Cell*) c = m_cast(Cell*, cell);
        assert(Not_Cell_Flag(c, DONT_MARK_PAYLOAD_2));
        return c;
    }
#endif

#define CELL_EXTRA(c) \
    Ensure_Cell_Extra_Needs_Mark(c)->extra.base

#define CELL_PAYLOAD_1(c) \
    Ensure_Cell_Payload_1_Needs_Mark(c)->payload.split.one.base

#define CELL_PAYLOAD_2(c) \
    Ensure_Cell_Payload_2_Needs_Mark(c)->payload.split.two.base


//=///// BINDING //////////////////////////////////////////////////////////=//
//
// Some value types use their `->extra` field in order to store a pointer to
// a Base which constitutes their notion of "binding".
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
//         if (binding and binding->header.bits & BASE_FLAG_MANAGED) {...}
//
//     The special UNBOUND stub set some bits, e.g. pretend to be managed:
//
//        if (binding->header.bits & BASE_FLAG_MANAGED) {...}  // UNBOUND ok
//
//     Question was whether avoiding the branching involved from the extra
//     test for null would be worth it for consistent dereferencing ability.
//     At least on x86/x64, the answer was: No. Maybe even a little slower.
//     Testing for null pointers the processor has in its hand is very common
//     and seemed to outweigh the need to dereference all the time.  The
//     increased clarity of having unbound be nullptr is also in its benefit.
//

#if (! DEBUG_CHECK_BINDING)
    #define Assert_Cell_Binding_Valid(v)  NOOP
#else
    #define Assert_Cell_Binding_Valid(v) \
        Assert_Cell_Binding_Valid_Core(v)
#endif

#define Cell_Binding(v) \
    u_cast(Context*, (v)->extra.base)

#if (! DEBUG_CHECK_BINDING)
    #define Tweak_Cell_Binding(c,binding) \
        ((c)->extra.base = binding)

    #define Tweak_Cell_Relative_Binding(c,details) \
        ((c)->extra.base = details)
#else
    INLINE void Tweak_Cell_Binding(Element* c, Option(Context*) binding) {
        Assert_Cell_Writable(c);
        assert(Is_Cell_Bindable(c));
        c->extra.base = opt binding;
        if (binding)
            Assert_Cell_Binding_Valid(c);
    }
    INLINE void Tweak_Cell_Relative_Binding(Element* c, Details* details) {
        c->extra.base = details;  // !!! relative binding may be deprecated
    }
#endif

#define SPECIFIED \
    u_cast(Context*, nullptr)  // u_cast (don't want DEBUG_CHECK_CASTS)

#define UNBOUND \
    u_cast(Context*, nullptr)  // using a stub did not improve performance [1]


//=//// CELL TRANSFER TYPE VALIDATION (COMPILE-TIME ONLY) /////////////////=//
//
// This is a shared utility for Cell Copying, Moving, and Blitting.  It does
// compile-time validation of passed-in Cell subclasses via static_assert().
// It does all its work at compile-time, meaning zero runtime overhead--even
// in debug builds (compared to a function-template-based approach).
//
// For example: consider you are trying to copy a `const Element*` into a
// `Value*` cell.  That's legal--because any state an Element can hold can be
// put into a Value.  So we don't want the static_assert() to fail, and also
// want to calculate the return type as a NON-CONST `Element*`, which is the
// view we give back on the filled Value* cell (since we know we just put an
// Element into it).
//
// On the other hand: if you tried to do the reverse (e.g. copy a Value* into
// an Element*) we would want the static_assert() to fire and stop that from
// being able to compile.
//
// 1. The actual specific rule we use is that if you can pass the destination
//    type to an Init(T) of the unwrapped source type, the copy is legal.  So
//    if your Src is `const Element*` and your Dst is `Init(Value)`, it asks
//    "could I pass Init(Value) to an Init(Element)".  If the answer is yes,
//    the copy is allowed.
//
// 2. Originally Gemini put `typename` inside the C-style cast, which GCC did
//    not like.  It was convinced you couldn't just use ::RetPtrType in the
//    macro without a typename, and so proposed this level of indirection.
//    It doesn't *hurt* to have it, but may be superfluous.
//

#if DONT_CHECK_CELL_SUBCLASSES
    #define TransferredCellType(out,v)  Cell*  // no subclasses, all is Cell!
#else
    template<typename Dst, typename Src>
    struct CellTransferValidator {
        using RetPtrType = needful_unconstify_t(
            needful_unwrapped_type(needful::remove_reference_t<Src>)
        );

        static_assert(
            needful_is_convertible_v(  // this is the actual rule [1]
                needful::remove_reference_t<Dst>,
                needful::InitWrapper<RetPtrType>
            ),
            "CellTransferValidator: Dst must be convertible to Init(Src)"
        );
    };

    template<typename Dst, typename Src>  // Gemini thinks we need this [2]
    using TransferredCellTypeHelper =
        typename CellTransferValidator<Dst, Src>::RetPtrType;

    #define TransferredCellType(out,v) \
        TransferredCellTypeHelper<decltype(out), decltype(v)>
#endif


//=//// COPYING CELLS /////////////////////////////////////////////////////=//
//
// Because you cannot assign cells to one another (e.g. `*dest = *src`), a
// function is used.  This provides an opportunity to check things like moving
// data into protected locations, and to mask out bits that should not be
// propagated.  We can also enforce that you can't copy a Value into a Stable
// or Element, and that you can't copy a Stable into an Element...keeping
// antiforms and unstable antiforms out of places they should not be.
//
// See Copy_Cell_May_Bind() for binding-aware copy.
//
// 1. If you write `Erase_Cell(dest)` followed by `Copy_Cell(dest, src)` the
//    optimizer notices it doesn't need the masking of Freshen_Cell_Header().
//    This was discovered by trying to force callers to pass in an already
//    freshened cell and seeing things get more complicated for no benefit.
//
// 2. Depending on the debug situation, you may be looking for where a Cell
//    was last copied to, or where the Cell was originally initialized.
//    The default is knowing the last point touched--but you can change this
//    using the DEBUG_TRACK_COPY_PRESERVES setting, in which case MAYBE_TRACK
//    is a no-op and the information inside the Cell is migrated.
//

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | CELL_FLAG_AURA | CELL_FLAG_NOTE)

#define CELL_MASK_ALL  (~ i_cast(Flags, 0))  // use with caution!

INLINE Cell* Copy_Cell_Core_Untracked(
    Cell* out,
    const Cell* v,
    Flags copy_mask  // typically you don't copy PROTECTED, etc
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    Assert_Cell_Readable(v);

    Freshen_Cell_Header(out);  // optimizer elides this after erasure [1]
    out->header.bits |= (BASE_FLAG_BASE | BASE_FLAG_CELL  // force BASE + CELL
        | (v->header.bits & copy_mask));

    out->extra = v->extra;  // extra may be a binding, or may be inert bits

    out->payload = v->payload;

  #if DEBUG_TRACK_COPY_PRESERVES
    dont(out->track_flags = v->track_flags);  // see definition
    out->file = v->file;
    out->line = v->line;
    out->tick = v->tick;
  #endif

    return out;
}

#define Copy_Cell_Untracked(out, v) \
    x_cast(TransferredCellType((out), (v)), \
        Copy_Cell_Core_Untracked((out), (v), CELL_MASK_COPY))

#define Copy_Cell_Core(out,v,copy_mask) \
    MAYBE_TRACK(Copy_Cell_Core_Untracked((out), (v), (copy_mask)))  // [2]

#define Copy_Cell(out,v) \
    MAYBE_TRACK(Copy_Cell_Untracked((out), (v)))  // [2]


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//
//
// Cell movement is distinct from cell copying, because it invalidates the
// old location (which must be mutable).  The old location is set to be
// a an "Unreadable" Cell...hence you shouldn't Move_Cell() out of a user
// visible Source array.
//
// (!!! REVIEW: Could we sense from some flag if it was okay to move a Cell?
// This would be something that could in general control Init_Unreadable()).
//
// Currently the advantage to moving vs. copying is that if the old location
// held GC nodes live, it doesn't anymore.  So it speeds up the GC and also
// increases the likelihood of stale nodes being collected.
//
// A theoretical longer-term advantage would be if we GC-managed Stubs in
// Cells on demand when Copy_Cell() duplicated them, and if eliding Cells
// noticed unmanaged Stubs and freed them.  This would mean Move_Cell() would
// be different from Copy_Cell() because it wouldn't add management to Stubs
// that weren't yet managed, thus decreasing GC load...potentially allowing
// a Cell to be moved many times, elided and have its Stubs freed, all without
// ever being seen by a GC mark and sweep cycle.
//
// 1. We'd generally like to know who erased a Cell, so having the callsite
//    implicate a Move_Cell() source cite is a good default.  But if you are
//    following a hot-potato with DEBUG_TRACK_COPY_PRESERVES, then we don't
//    want to blow away the moved-from-Cell's original tracking before we
//    can cpoy it.  Hence use MAYBE_TRACK()

INLINE Cell* Move_Cell_Core_Untracked(Cell* out, Cell* c, Flags copy_mask)
{
    Copy_Cell_Core_Untracked(out, c, copy_mask);
    Init_Unreadable_Untracked(c);  // gets MAYBE_TRACK() applied by caller [1]
    return out;
}

#define Move_Cell_Untracked(out, v) \
    x_cast(TransferredCellType((out), (v)), \
        Move_Cell_Core_Untracked((out), (v), CELL_MASK_COPY))

#define Move_Cell_Core(out,v,copy_mask) \
    MAYBE_TRACK(Move_Cell_Core_Untracked( \
        (out), MAYBE_TRACK(v), (copy_mask)))  // may track input [1]

#define Move_Cell(out,v) \
    MAYBE_TRACK(Move_Cell_Untracked((out), MAYBE_TRACK(v)))  // [1]


//=//// CELL "BLITTING" (COMPLETE OVERWRITE) //////////////////////////////=//
//
// The term "blitting" originates from "BLock Transfer", and it means you
// are blindly overwriting the bits of the target location.  The debug build
// makes sure you're not ovewriting anything important by requiring the
// target cell to be poisoned or erased.
//

INLINE Cell* Force_Blit_Cell_Untracked(Cell* out, const Cell* c) {
    out->header = c->header;
    out->extra = c->extra;
    out->payload = c->payload;

  #if DEBUG_TRACK_COPY_PRESERVES
    dont(out->track_flags = c->track_flags);  // see definition
    out->file = c->file;
    out->line = c->line;
    out->tick = c->tick;
  #endif

    return out;
}

INLINE Cell* Blit_Cell_Untracked(Cell* out, const Cell* c) {
  #if DEBUG_POISON_UNINITIALIZED_CELLS
    assert(
      Is_Cell_Poisoned(out) or Is_Cell_Erased(out) or Not_Cell_Readable(out)
    );
  #endif
   return Force_Blit_Cell_Untracked(out, c);
}

#define Blit_Cell(out,c) \
    MAYBE_TRACK(Blit_Cell_Untracked(out, c))

#define Force_Blit_Cell(out,c) \
    MAYBE_TRACK(Force_Blit_Cell_Untracked(out, c))


//=//// CELL CONST INHERITANCE ////////////////////////////////////////////=//
//
// Various operations are complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.
//
// (See CELL_FLAG_CONST for more information.)

INLINE Value* Inherit_Const(Value* out, const Cell* influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}

#define Trust_Const(value) \
    PASSTHRU(value)  // just mark to say the const is accounted for already

INLINE Stable* Constify(Stable* v) {
    Set_Cell_Flag(v, CONST);
    return v;
}


//=//// DECLARATION HELPERS FOR CELLS ON THE C STACK //////////////////////=//
//
// Cells can't hold random bits when you initialize them:.
//
//     Element element;               // cell contains random bits
//     Init_Integer(&element, 1020);  // invalid, as init checks protect bits
//
// The process of initialization checks to see if the cell is protected, and
// also masks in some bits to preserve with CELL_MASK_PERSIST.  You have to
// do something to format the cell, for instance Force_Erase_Cell():
//
//     Element element;
//     Force_Erase_Cell(&element);
//     Init_Integer(&element, 1020);
//
// We can abstract this with a macro, that can also remove the need to use &,
// by making the passed-in name an alias for the address of the cell:
//
//     DECLARE_ELEMENT (element)
//     Init_Integer(element, 1020);
//
// * These cells are not protected from having their insides GC'd unless
//   you guard them with Push_Lifeguard(), or if a routine you call protects
//   the cell implicitly (as stackful evaluations will do on cells used
//   as an output).
//
// * You can't use a cell on the C stack as the output target for the eval
//   of a stackless continuation, because the function where the cell lives
//   has to return control to the trampoline...destroying that stack memory.
//   The OUT, SPARE, and SCRATCH are available for continuations to use
//   as targets...and sometimes it's possible to use the spare/scratch of
//   child or parent levels as well.
//
// * Although writing CELL_MASK_ERASED_0 to the header is very cheap, it
//   still costs *something*.  In checked builds it can cost more to declare
//   the cell, because DEBUG_TRACK_EXTEND_CELLS makes TRACK() write
//   the file, line, and tick where the cell was initialized in the extended
//   space.  So it should generally be favored to put these declarations at
//   the outermost scope of a function, vs. inside a loop.
//

#define DECLARE_VALUE(name) \
    Value name##_atom; \
    Value* const name = FORCE_TRACK_VALID_EVAL_TARGET(&name##_atom); \
    Force_Erase_Cell_Untracked(name)  // single assignment of 0 to header

#define DECLARE_STABLE(name) \
    Stable name##_value; \
    Stable* const name = FORCE_TRACK_0(&name##_value); \
    Force_Erase_Cell_Untracked(name)  // single assignment of 0 to header

#define DECLARE_ELEMENT(name) \
    Element name##_element; \
    Element* const name = FORCE_TRACK_0(&name##_element); \
    Force_Erase_Cell_Untracked(name)  // single assignment of 0 to header


//=//// rebReleaseAndNull overload ////////////////////////////////////////=//
//
// rebReleaseAndNull is in the API, but because the API doesn't make
// distinctions between Element and Stable the double pointer trips it up
// in the C++ build.  Add an overload.
//
#if CHECK_CELL_SUBCLASSES
    static inline void rebReleaseAndNull(Stable** v) {
        rebRelease(*v);
        *v = nullptr;
    }

    static inline void rebReleaseAndNull(Element** v) {
        rebRelease(*v);
        *v = nullptr;
    }
#endif
