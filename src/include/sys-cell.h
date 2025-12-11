//
//  file: %sys-cell.h
//  summary: "Cell Definitions AFTER %tmp-internals.h}
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2025 Ren-C Open Source Contributors
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
// that also does not have CELL_FLAG_PROTECTED.
//
// Note that this code asserts about CELL_FLAG_PROTECTED just to be safe.
// But the idea is that a cell which is protected should never be writable
// at runtime, enforced by the `const Cell*` convention.  You can't get a
// non-const Cell reference without going through a runtime check that
// makes sure the cell is not protected.
//
// [INITABILITY]
//
// A special exception for writability is made for initialization, that
// allows cells with headers initialized to zero.  See Freshen_Cell() for why
// this is done and how it is taken advantage of.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// 1. These macros are "evil", because in the checked build, functions aren't
//    inlined, and the overhead actually adds up very quickly.  We repeat
//    arguments to speed up these critical tests, then wrap them in
//    Ensure_Readable() and Ensure_Writable() functions for callers that
//    don't mind the cost.  The STATIC_ASSERT_LVALUE() macro catches any
//    potential violators...though narrow need for the evil macros exist.
//
// 2. One might think that because you're asking if a cell is writable that
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
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        if ( \
            (((c)->header.bits) & ( \
                BASE_FLAG_BASE | BASE_FLAG_CELL | BASE_FLAG_UNREADABLE \
            )) != (BASE_FLAG_BASE | BASE_FLAG_CELL) \
        ){ \
            Crash_On_Unreadable_Cell(c); \
        } \
    } while (0)

    #define Assert_Cell_Writable_Evil_Macro(c) do { \
        /* don't STATIC_ASSERT_LVALUE(out), it's evil on purpose */ \
        if ( \
            (((c)->header.bits) & ( \
                BASE_FLAG_BASE | BASE_FLAG_CELL | CELL_FLAG_PROTECTED \
            )) != (BASE_FLAG_BASE | BASE_FLAG_CELL) \
        ){ \
            Crash_On_Unwritable_Cell(c);  /* despite write, pass const [2] */ \
        } \
    } while (0)

    #define Assert_Cell_Writable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        Assert_Cell_Writable_Evil_Macro(c); \
    } while (0)

    #define Assert_Cell_Initable_Evil_Macro(c) do { \
        /* don't STATIC_ASSERT_LVALUE(out), it's evil on purpose */ \
        if ((c)->header.bits != CELL_MASK_ERASED_0)  /* 0 is initable */ \
            Assert_Cell_Writable_Evil_Macro(c);  /* else need NODE and CELL */ \
    } while (0)

    #define Assert_Cell_Initable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
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
        STATIC_ASSERT_LVALUE(c);  /* ensure "evil macro" used safely [1] */ \
        if (i_cast(uintptr_t, (c)) % ALIGN_SIZE != 0) \
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

#define CELL_MASK_POISON \
    (BASE_FLAG_BASE | BASE_FLAG_CELL | \
        BASE_FLAG_UNREADABLE | CELL_FLAG_PROTECTED)  // no read or write [1]

#define Assert_Cell_Header_Overwritable(c) do {  /* conservative check [2] */ \
    STATIC_ASSERT_LVALUE(c); \
    assert( \
        (c)->header.bits == CELL_MASK_POISON \
        or (c)->header.bits == CELL_MASK_ERASED_0 \
        or (BASE_FLAG_BASE | BASE_FLAG_CELL) == ((c)->header.bits & \
            (BASE_FLAG_BASE | BASE_FLAG_CELL \
                | BASE_FLAG_ROOT | BASE_FLAG_MARKED \
                | BASE_FLAG_MANAGED | CELL_FLAG_PROTECTED) \
        ) \
    ); \
  } while (0)

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
    TRACK(Force_Poison_Cell_Untracked(c))

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
    TRACK(Erase_Cell_Untracked(c))  // not safe on all cells, e.g. API [1]

INLINE Cell* Force_Erase_Cell_Untracked(Cell* c) {
    Assert_Cell_Aligned(c);  // only have to check on first initialization
    c->header.bits = CELL_MASK_ERASED_0;
    return c;
}

#define Force_Erase_Cell(c)  /* unchecked version, use sparingly! [2] */ \
    TRACK(Force_Erase_Cell_Untracked(c))

#define Is_Cell_Erased(c) /* initable, not read/writable */ \
    (known(Cell*, (c))->header.bits == CELL_MASK_ERASED_0)

#define Not_Cell_Erased(c)  (not Is_Cell_Erased(c))


//=//// UNREADABLE CELLS //////////////////////////////////////////////////=//
//
// Unreadable cells are write-only cells.  They will give errors on attempts
// to read from them e.g. with Type_Of(), which is similar to erased cells.
// But with the advantage that they have BASE_FLAG_BASE and BASE_FLAG_CELL
// set in their header, hence they do not conflate with empty UTF-8 strings.
// The GC tolerates them in places where an erased cell would trigger an
// assertion indicating an element hadn't been initialized.
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
        /* don't STATIC_ASSERT_LVALUE(out), it's evil on purpose */ \
        Assert_Cell_Initable_Evil_Macro(out); \
        (out)->header.bits |= CELL_MASK_UNREADABLE;  /* bitwise OR [1] */ \
    } while (0)
#else
    #define Init_Unreadable_Untracked_Evil_Macro(out) do { \
        /* don't STATIC_ASSERT_LVALUE(out), it's evil on purpose */ \
        Assert_Cell_Initable_Evil_Macro(out); \
        (out)->header.bits |= CELL_MASK_UNREADABLE;  /* bitwise OR [1] */ \
        Corrupt_If_Needful((out)->extra.corrupt); \
        Corrupt_If_Needful((out)->payload); /* split.one/two slower [2] */ \
    } while (0)
#endif

#define Init_Unreadable_Untracked(out) do { \
    STATIC_ASSERT_LVALUE(out);  /* evil macro: make it safe */ \
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
  // 1. This only runs in checked builds, but it is performance sensitive.
  //    So we want to avoid making a temporary, which would be needed to
  //    subvert the usual STATIC_ASSERT_LVALUE() check:
  //
  //        Cell* cell = &ref;
  //        Init_Unreadable_Untracked(cell);
  //
  //    In the C++ model there's no way to tell the difference between &ref
  //    and an expression with side effects that synthsized a reference.
  //    There would be ways to make a macro that "approved" a reference:
  //
  //       Init_Unredable_Untracked(NO_SIDE_EFFECTS(&ref));
  //
  //    However, even a constexpr which did this would add cost in the
  //    checked build (functions aren't inlined).  So for this case, we
  //    just use a macro variant with no STATIC_ASSERT_LVALUE() check.
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
            Init_Unreadable_Untracked_Evil_Macro(&ref);  // &ref needs evil [1]
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
            // no-op for const Cell-derived types
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
// Note if CELL_FLAG_PROTECTED is set on a cell, it will not be considered
// fresh for initialization.  So the flag must be cleared or the cell "hard"
// erased (with Force_Erase_Cell()) in order to overwrite it.
//
// 1. "evil macros" for checked build performance, see STATIC_ASSERT_LVALUE()
//
// 2. Slots have more use for persistent flags than most Cells do.  e.g. if
//    a Slot represents a place where a loop variable is being stored, it
//    may want to remember CELL_FLAG_LOOP_SLOT_NOTE_TIE so it can know that
//    the variable was named by $var, hence needs to be bound.  Rather than
//    store this information in a side-structure, it can be stored on the
//    Slot itself...but it can't be overwritten or it would be forgotten on
//    each loop iteration.
//
// 3. Things are proceeding in a hacky way to start making use of the "sub
//    band" of things that are not lifted, e.g. "true unset".  True unset
//    indicated by having DUAL_0 set in the lift byte of an *unset* WORD!.
//    You usually have to go through the mainline slot reading machinery
//    to deal with it.  But FRAME! machinery currently has some exceptions.

#define CELL_MASK_PERSIST \
    (BASE_FLAG_MANAGED | BASE_FLAG_ROOT | BASE_FLAG_MARKED)

#define Freshen_Cell_Header(c) do { \
    STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
    Assert_Cell_Initable(c);  /* if CELL_MASK_ERASED_0, no node+cell flags */ \
    (c)->header.bits &= CELL_MASK_PERSIST;  /* won't add node+cell flags */ \
} while (0)

STATIC_ASSERT(not (CELL_MASK_PERSIST & CELL_FLAG_NOTE));

#define CELL_MASK_PERSIST_SLOT \
    (CELL_MASK_PERSIST | CELL_FLAG_NOTE)  // special persistence for slots [2]


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

#define Unchecked_Cell_Has_Lift_Sigil_Heart(lift,sigil,heart,cell) \
    (((cell)->header.bits & CELL_MASK_HEART_AND_SIGIL_AND_LIFT) \
        == (FLAG_SIGIL(sigil) | FLAG_HEART(heart) | FLAG_LIFT_BYTE(lift)))

#define Cell_Has_Lift_Sigil_Heart(lift,sigil,heart,cell) \
    Unchecked_Cell_Has_Lift_Sigil_Heart((lift), (sigil), (heart), \
        Ensure_Readable(cell))

#define Unchecked_Cell_Has_Lift_Heart_No_Sigil(lift,heart,cell) \
    Unchecked_Cell_Has_Lift_Sigil_Heart((lift), SIGIL_0, (heart), (cell))

#define Cell_Has_Lift_Heart_No_Sigil(lift,heart,cell) \
    Unchecked_Cell_Has_Lift_Heart_No_Sigil((lift), (heart), \
        Ensure_Readable(cell))


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

        operator KindByte() const {  // implicit cast, add read checks here
            return KIND_BYTE_RAW(cell);
        }

        void operator=(KindByte right) {  // add write checks you want here
            KIND_BYTE_RAW(cell) = right;
        }

        void operator=(const KindHolder& right)  // must write explicitly
          { *this = u_cast(KindByte, right); }

        ENABLE_IF_EXACT_ARG_TYPE(HeartEnum, KindHolder, Heart)
        void operator=(T right)
          { *this = u_cast(KindByte, right); }  // inherit operator= checks

        ENABLE_IF_EXACT_ARG_TYPE(HeartEnum, Heart)
        explicit operator T() const   // inherit Byte() cast extraction checks
          { return u_cast(T, u_cast(Byte, *this)); }
    };

    INLINE bool operator==(const KindHolder& holder, HeartEnum h)
      { return KIND_BYTE_RAW(holder.cell) == cast(Byte, h); }

    INLINE bool operator==(HeartEnum h, const KindHolder& holder)
      { return cast(Byte, h) == KIND_BYTE_RAW(holder.cell); }

    INLINE bool operator!=(const KindHolder& holder, HeartEnum h)
      { return KIND_BYTE_RAW(holder.cell) != cast(Byte, h); }

    INLINE bool operator!=(HeartEnum h, const KindHolder& holder)
      { return cast(Byte, h) != KIND_BYTE_RAW(holder.cell); }

    #define KIND_BYTE(cell) \
        KindHolder{cell}
#endif

#define Unchecked_Heart_Of(c) \
    u_cast(Option(Heart), u_cast(HeartEnum, KIND_BYTE_RAW(c) % MOD_HEART_64))

#define Heart_Of(c) \
    Unchecked_Heart_Of(Ensure_Readable(c))

INLINE Option(Heart) Heart_Of_Fundamental(const Cell* c) {
    assert(LIFT_BYTE_RAW(c) == NOQUOTE_2);
    return Heart_Of(c);
}

INLINE Heart Heart_Of_Builtin(const Cell* c) {
    Option(Heart) heart = Heart_Of(c);
    assert(heart != TYPE_0);
    return opt heart;  // faster than unwrap, we already checked for 0
}

INLINE Heart Heart_Of_Builtin_Fundamental(const Element* c) {
    assert(LIFT_BYTE_RAW(c) == NOQUOTE_2);
    Option(Heart) heart = Heart_Of(c);
    assert(heart != TYPE_0);
    return opt heart;  // faster than unwrap, we already checked for 0
}

#define Heart_Of_Is_0(cell) \
    (TYPE_0 == opt Heart_Of(cell))

INLINE bool Type_Of_Is_0(const Cell* cell) {
    return Heart_Of_Is_0(cell) and LIFT_BYTE_RAW(cell) == NOQUOTE_2;
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

        operator LiftByte() const {  // implicit cast, add read checks here
            return LIFT_BYTE_RAW(cell);
        }

        void operator=(int right) {  // add write checks you want here
            assert(right >= 0 and right <= 255);

            Option(Heart) heart = Unchecked_Heart_Of(cell);
            if (right & QUASI_BIT)
                assert(Any_Isotopic_Type(heart));  // has quasiforms/antiforms

            LIFT_BYTE_RAW(cell) = right;
        }

        void operator=(const Antiform_1_Struct& right) = delete;
        void operator=(const Quasiform_3_Struct& right) = delete;

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


//=//// VALUE TYPE (always TYPE_XXX <= MAX_TYPE) ////////////////////////////=//
//
// When asking about a value's "type", you want to see something like a
// double-quoted WORD! as a QUOTED! value...though it's a WORD! underneath.
//
// (Instead of Type_Of(), use Heart_Of() if you wish to know that the cell
// pointer you pass in is carrying a word payload.  It disregards the quotes.)

INLINE Option(Type) Type_Of_Unchecked(const Value* atom) {
    switch (LIFT_BYTE(atom)) {
      case 1:  // ANTIFORM_1 (not constant in some debug builds)
        return cast(TypeEnum,
            (KIND_BYTE(atom) % MOD_HEART_64) + MAX_TYPE_BYTE_ELEMENT
        );

      case NOQUOTE_2:  // heart might be TYPE_0 to be extension type
        switch (cast(Sigil, KIND_BYTE(atom) >> KIND_SIGIL_SHIFT)) {
          case SIGIL_0:
            return cast(HeartEnum, (KIND_BYTE(atom) % MOD_HEART_64));

          case SIGIL_META:
            return TYPE_METAFORM;

          case SIGIL_PIN:
            return TYPE_PINNED;

          case SIGIL_TIE:
            break;  // compiler warns of fallthrough
        }
        return TYPE_TIED;  // workaround "not all control paths return a value"

      case 3:  // QUASIFORM_3 (not constant in some debug builds)
        return TYPE_QUASIFORM;

      default:
        return TYPE_QUOTED;
    }
}

#if NO_RUNTIME_CHECKS
    #define Type_Of  Type_Of_Unchecked
#else
    #define Type_Of(atom) \
        Type_Of_Unchecked(Ensure_Readable(atom))
#endif


INLINE Option(Type) Type_Of_Unquoted(const Element* elem) {
    if (LIFT_BYTE(elem) == QUASIFORM_3)
        return TYPE_QUASIFORM;

    assert(LIFT_BYTE(elem) != ANTIFORM_1);

    switch (u_cast(Sigil, KIND_BYTE(elem) >> KIND_SIGIL_SHIFT)) {
      case SIGIL_0:
        return u_cast(HeartEnum, (KIND_BYTE(elem) % MOD_HEART_64));

      case SIGIL_META:
        return TYPE_METAFORM;

      case SIGIL_PIN:
        return TYPE_PINNED;

      default:
        break;
    }
    return TYPE_TIED;  // work around "not all control paths return a value"
}


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//
//
// Assert_Cell_Initable() for the explanation of what "freshening" is, and why
// it tolerates CELL_MASK_ERASED_0 in a cell header.

INLINE void Reset_Cell_Header_Noquote(Cell* c, uintptr_t flags)
{
    assert((flags & CELL_MASK_LIFT) == FLAG_LIFT_BYTE(DUAL_0));
    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        BASE_FLAG_BASE | BASE_FLAG_CELL | flags | FLAG_LIFT_BYTE(NOQUOTE_2)
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
        BASE_FLAG_BASE | BASE_FLAG_CELL | flags | FLAG_LIFT_BYTE(NOQUOTE_2)
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
        Element* x = known(Element*, c);
        USED(x);
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


//=//// COPYING CELLS /////////////////////////////////////////////////////=//
//
// Because you cannot assign cells to one another (e.g. `*dest = *src`), a
// function is used.  This provides an opportunity to check things like moving
// data into protected locations, and to mask out bits that should not be
// propagated.  We can also enforce that you can't copy an Value into a Stable
// or Element, and that you can't copy a Stable into an Element...keeping
// antiforms and unstable antiforms out of places they should not be.
//
// Interface designed to line up with Derelativize()
//
// 1. If you write `Erase_Cell(dest)` followed by `Copy_Cell(dest, src)` the
//    optimizer notices it doesn't need the masking of Freshen_Cell_Header().
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
//    will compete with one as (Init(Stable), const Stable*) when the second
//    argument is Element*, since Element can be passed where Stable is taken.
//    Template magic lets an overload exclude itself to break the contention.

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | CELL_FLAG_PROTECTED \
        | CELL_FLAG_NOTE | CELL_FLAG_HINT)

#define CELL_MASK_ALL  ~cast(Flags, 0)  // use with caution!

INLINE void Copy_Cell_Header(
    Cell* out,
    const Cell* v
){
    assert(out != v);  // usually a sign of a mistake; not worth supporting
    Assert_Cell_Readable(v);

  #if DEBUG_TRACK_EXTEND_CELLS
    assert(out->tick == TICK);  // should TRACK(out) before call, not after
  #endif

    Freshen_Cell_Header(out);
    out->header.bits |= (BASE_FLAG_BASE | BASE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & CELL_MASK_COPY));

  #if DEBUG_TRACK_COPY_PRESERVES
    out->file = v->file;
    out->line = v->line;
    out->tick = v->tick;
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

  #if DEBUG_TRACK_EXTEND_CELLS
    assert(out->tick == TICK);  // should TRACK(out) before call, not after
  #endif

    Freshen_Cell_Header(out);  // optimizer elides this after erasure [1]
    out->header.bits |= (BASE_FLAG_BASE | BASE_FLAG_CELL  // ensure NODE+CELL
        | (v->header.bits & copy_mask));

    out->payload = v->payload;  // before init binding anachronism [2]

    out->extra = v->extra;  // binding or inert bits

  #if DEBUG_TRACK_COPY_PRESERVES
    out->file = v->file;
    out->line = v->line;
    out->tick = v->tick;
    out->touch = v->touch;
  #endif

    return out;
}

#if DONT_CHECK_CELL_SUBCLASSES
    #define Copy_Cell(out,v) \
        Copy_Cell_Untracked(TRACK(out), (v), CELL_MASK_COPY)
#else
    INLINE Element* Copy_Cell_Overload(Init(Element) out, const Element* v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    template<  // avoid conflict when Element* coerces to Stable* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T, const Stable*>::value
            and not std::is_convertible<T, const Element*>::value
        >::type* = nullptr
    >
    INLINE Stable* Copy_Cell_Overload(Init(Stable) out, const T& v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    template<  // avoid conflict when Element*/Stable* coerces to Value* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T, const Value*>::value
            and not std::is_convertible<T, const Stable*>::value
            and not std::is_convertible<T, const Element*>::value
        >::type* = nullptr
    >
    INLINE Value* Copy_Cell_Overload(Init(Value) out, const T& v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Copy_Cell(out,v) \
        Copy_Cell_Overload(TRACK(out), (v))
#endif

#define Copy_Cell_Core(out,v,copy_mask) \
    Copy_Cell_Untracked(TRACK(out), (v), (copy_mask))

#define Copy_Lifted_Cell(out,v) \
    cast(Element*, Liftify(Copy_Cell(u_cast(Value*, (out)), (v))))

INLINE Element* Copy_Plain_Cell(Init(Element) out, const Value* v) {
    Copy_Cell(u_cast(Value*, (out)), v);
    LIFT_BYTE(out) = NOQUOTE_2;
    return out;
}


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//
//
// Cell movement is distinct from cell copying, because it invalidates the
// old location (which must be mutable).  The old location is erased if it's
// an Value and can legally hold CELL_MASK_ERASED_0 for GC, or it's set
// to be quasar (quasiform SPACE) if it can't hold that state.
//
// Currently the advantage to moving vs. copying is that if the old location
// held GC nodes live, it doesn't anymore.  So it speeds up the GC and also
// increases the likelihood of stale nodes being collected.  But the advantage
// would go away if you were going to immediately overwrite the moved-from
// cell with something else.
//
// A theoretical longer-term advantage would be if cells were incrementing
// some kind of reference count in the series they pointed to.  The AddRef()
// and Release() mechanics that would be required would be painful to write
// in C, so this is not likely to happen.  Hence moving a cell out of a
// data stack slot and then dropping it is technically wasteful.  But it
// only costs one platform-pointer-sized write operation more than a cell
// copy, so future-proofing for that scenario has some value.
//
// Note: Not being willing to disrupt flags currently means that Move_Cell()
// doesn't work on API cells.  Review.

INLINE Element* Init_Quasar_Untracked(Init(Element) out);

INLINE Cell* Move_Cell_Untracked(
    Cell* out,
    Cell* c,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, c, copy_mask);  // Move_Cell() adds track to `out`
    Assert_Cell_Header_Overwritable(c);
    Init_Quasar_Untracked(c);  // !!! slower than we'd like it to be, review

    return out;
}

#if DONT_CHECK_CELL_SUBCLASSES
    #define Move_Cell(out,v) \
        Move_Cell_Untracked(TRACK(out), (v), CELL_MASK_COPY)
#else
    INLINE Element* Move_Cell_Overload(Init(Element) out, Element* v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    template<  // avoid overload conflict when Element* coerces to Stable* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T,Stable*>::value
            && !std::is_convertible<T,Element*>::value
        >::type* = nullptr
    >
    INLINE Stable* Move_Cell_Overload(Init(Stable) out, const T& v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Move_Cell(out,v) \
        Move_Cell_Overload(TRACK(out), (v))
#endif

#define Move_Cell_Core(out,v,cell_mask) \
    Move_Cell_Untracked(TRACK(out), (v), (cell_mask))

#define Move_Lifted_Cell(out,v) \
    cast(Element*, Liftify(Move_Cell_Core((out), (v), CELL_MASK_COPY)))

INLINE Value* Move_Atom_Untracked(
    Value* out,
    Value* a
){
    Assert_Cell_Header_Overwritable(out);  // atoms can't have persistent bits
    Assert_Cell_Header_Overwritable(a);  // atoms can't have persistent bits

    Assert_Cell_Readable(a);

  #if DEBUG_TRACK_EXTEND_CELLS
    assert(out->tick == TICK);  // should TRACK(out) before call, not after
  #endif

    out->header = a->header;
    out->extra = a->extra;
    out->payload = a->payload;

    a->header.bits = CELL_MASK_ERASED_0;  // legal state for atoms

    Corrupt_If_Needful(a->extra.corrupt);
    Corrupt_If_Needful(a->payload.split.one.corrupt);
    Corrupt_If_Needful(a->payload.split.two.corrupt);

  #if DEBUG_TRACK_COPY_PRESERVES
    out->file = v->file;
    out->line = v->line;
    out->tick = v->tick;
    out->touch = v->touch;  // see also arbitrary debug use via Touch_Cell()
  #endif

    return out;
}

#define Move_Value(out,a) \
    Move_Atom_Untracked(TRACK(out), (a))

#define Move_Lifted_Atom(out,a) \
    cast(Element*, Liftify(Move_Atom_Untracked(TRACK(out), (a))))


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
    return out;
}

INLINE Cell* Blit_Cell_Untracked(Cell* out, const Cell* c) {
  #if DEBUG_POISON_UNINITIALIZED_CELLS
    assert(Is_Cell_Poisoned(out) or Is_Cell_Erased(out));
  #endif
   return Force_Blit_Cell_Untracked(out, c);
}

#define Blit_Cell(out,c)   TRACK(Blit_Cell_Untracked(out, c))

#define Force_Blit_Cell(out,c) \
    TRACK(Force_Blit_Cell_Untracked(out, c))


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
    (value)  // just a marking to say the const is accounted for already

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
    Value* const name = TRACK(&name##_atom); \
    Force_Erase_Cell_Untracked(name)  // single assignment of 0 to header

#define DECLARE_STABLE(name) \
    Stable name##_value; \
    Stable* const name = TRACK(&name##_value); \
    Force_Erase_Cell_Untracked(name)  // single assignment of 0 to header

#define DECLARE_ELEMENT(name) \
    Element name##_element; \
    Element* const name = TRACK(&name##_element); \
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


//=//// PASS SINK()/NEED() TO API VARIADICS ///////////////////////////////=//
//
// C doesn't have any type checking of variadic parameters, but when compiling
// with C++ we can recursively break down the variadics and do typechecking
// (as well as do interesting type conversions).  Here we enable Sink() and
// Need() to handle Cell subclasses.
//
// Note that a similar converter should NOT be made for OnStack(...), as you
// should not be passing values on the data stack to API functions.
//

#if NEEDFUL_SINK_USES_WRAPPER
    template<typename T>
    inline const void* to_rebarg(const Sink(T)& val)
        { return u_cast(Value*, val); }

    template<typename T>
    inline const void* to_rebarg(const Need(T)& val)
        { return u_cast(Value*, val); }
#endif
