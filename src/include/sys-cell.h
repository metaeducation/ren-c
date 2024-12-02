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


//=//// CELL READABLE + WRITABLE + INITABLE CHECKS ////////////////////////=//
//
// [READABILITY]
//
// Readable cells have NODE_FLAG_NODE and NODE_FLAG_CELL set.  It's important
// that they do, because if they don't then the first byte of the header
// could be mistaken for valid UTF-8.
//
// See Detect_Rebol_Pointer() for the machinery that relies upon this for
// mixing UTF-8, Cells, and Stubs in variadic API calls.
//
// [WRITABILITY]
//
// A writable cell is one that has NODE_FLAG_NODE and NODE_FLAG_CELL set, but
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
//    potential violators.
//
// 2. One might think that because you're asking if a cell is writable that
//    the function should only take non-const Cells, but the question is
//    abstract and doesn't mean you're going to write it in the moment.
//    You might just be asking if it could be written if someone had non
//    const access to it.
//

#define CELL_MASK_ERASED_0  0  // "initable", but not readable or writable

#if (! DEBUG_CELL_READ_WRITE)  // these are all no-ops in release builds!
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
            Panic_Cell_Unwritable(c);  /* despite write, passed const [2] */ \
        } \
    } while (0)

    #define Assert_Cell_Initable(c) do { \
        STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
        if ((c)->header.bits != CELL_MASK_ERASED_0)  /* 0 is initable */ \
            Assert_Cell_Writable(c);  /* else need NODE and CELL flags */ \
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
//    overwrite NODE_FLAG_ROOT, and a managed pairing would overwrite
//    NODE_FLAG_MANAGED.  This check helps make sure you're not losing
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
    (NODE_FLAG_NODE | NODE_FLAG_CELL | \
        NODE_FLAG_UNREADABLE | CELL_FLAG_PROTECTED)  // no read or write [1]

#define Assert_Cell_Header_Overwritable(c) do {  /* conservative check [2] */ \
    STATIC_ASSERT_LVALUE(c); \
    assert( \
        (c)->header.bits == CELL_MASK_POISON \
        or (c)->header.bits == CELL_MASK_ERASED_0 \
        or (NODE_FLAG_NODE | NODE_FLAG_CELL) == ((c)->header.bits & \
            (NODE_FLAG_NODE | NODE_FLAG_CELL \
                | NODE_FLAG_ROOT | NODE_FLAG_MARKED \
                | NODE_FLAG_MANAGED | CELL_FLAG_PROTECTED) \
        ) \
    ); \
  } while (0)

INLINE Cell* Poison_Cell_Untracked(Cell* c) {
    Assert_Cell_Header_Overwritable(c);
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

INLINE bool Is_Cell_Poisoned(const Cell* c) {
    if (c->header.bits == CELL_MASK_POISON)
        return true;
    /* Assert_Cell_Initable(c); */  // not always initable/readable [4]
    return false;
}


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
// 1. If you do not fully control the location you are writing, Erase_Cell()
//    is NOT what you want to use to make a cell writable.  You could be
//    overwriting persistent cell bits such as NODE_FLAG_ROOT that indicates
//    an API handle, or NODE_FLAG_MANAGED that indicates a Pairing.  This is
//    to be used for evaluator-controlled cells (OUT, SPARE, SCRATCH), or
//    restoring 0-initialized global variables back to the 0-init state, or
//    things like that.
//
// 2. In cases where you are trying to erase a cell in uninitialized memory,
//    you can't do the checks for [1].

INLINE Cell* Erase_Cell_Untracked(Cell* c) {
    Assert_Cell_Header_Overwritable(c);
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

#define Is_Cell_Erased(c) \
    ((c)->header.bits == CELL_MASK_ERASED_0)  // initable, not read/writable

#define Not_Cell_Erased(c)  (not Is_Cell_Erased(c))


//=//// UNREADABLE CELLS //////////////////////////////////////////////////=//
//
// Unreadable cells are write-only cells.  They will give errors on attempts
// to read from them e.g. with VAL_TYPE(), which is similar to erased cells.
// But with the advantage that they have NODE_FLAG_NODE and NODE_FLAG_CELL
// set in their header, hence they do not conflate with empty UTF-8 strings,
// and functions like Push_Lifeguard() can detect that they are cells.
//
// They are used to initialize stack variables e.g. DECLARE_ELEMENT(), and
// are used in MAP! to denote "zombie" slots.
//
// 1. Setting a cell unreadable does not affect bits like NODE_FLAG_ROOT
//    or NODE_FLAG_MARKED, so it's "safe" to use them with cells that need
//    these persistent bits preserved.
//
// 2. If you're going to set uninitialized memory to an unreadable cell,
//    then the unchecked Force_Unreadable_Cell() has to be used, because
//    you can't Assert_Cell_Initable() on random bits.

#define CELL_MASK_UNREADABLE \
    (NODE_FLAG_NODE | NODE_FLAG_CELL | NODE_FLAG_UNREADABLE \
        | CELL_FLAG_DONT_MARK_NODE1 | CELL_FLAG_DONT_MARK_NODE2 \
        | FLAG_HEART_BYTE(255) | FLAG_QUOTE_BYTE(255))

#define Init_Unreadable_Untracked(out) do { \
    STATIC_ASSERT_LVALUE(out);  /* evil macro: make it safe */ \
    Assert_Cell_Initable(out); \
    (out)->header.bits |= CELL_MASK_UNREADABLE;  /* note: bitwise OR [1] */ \
} while (0)

INLINE Element* Init_Unreadable_Untracked_Inline(Init(Element) out) {
    Init_Unreadable_Untracked(out);
    return out;
}

#define Force_Unreadable_Cell_Untracked(out) \
    ((out)->header.bits = CELL_MASK_UNREADABLE)

INLINE Element* Force_Unreadable_Cell_Untracked_Inline(Init(Element) out) {
    Force_Unreadable_Cell_Untracked(out);
    return out;
}

#define Force_Unreadable_Cell(out)  /* unchecked, use sparingly! [2] */ \
    Force_Unreadable_Cell_Untracked_Inline(TRACK(out))

INLINE bool Is_Cell_Readable(const Cell* c) {
    if (Is_Node_Readable(c)) {
        Assert_Cell_Readable(c);  // also needs NODE_FLAG_NODE, NODE_FLAG_CELL
        return true;
    }
    assert((c->header.bits & CELL_MASK_UNREADABLE) == CELL_MASK_UNREADABLE);
    return false;
}

#define Not_Cell_Readable(c)  (not Is_Cell_Readable(c))

#define Init_Unreadable(out) \
    TRACK(Init_Unreadable_Untracked_Inline((out)))

#if RUNTIME_CHECKS && CPLUSPLUS_11 && (! DEBUG_STATIC_ANALYZING)
    //
    // We don't actually want things like Sink(Value) to set a cell's bits to
    // a corrupt pattern, as we need to be able to call Init_Xxx() routines
    // and can't do that on garbage.  But we don't want to Erase_Cell() either
    // because that would lose header bits like whether the cell is an API
    // value.  We use the Init_Unreadable_Untracked().
    //
    // Note that Init_Unreadable_Untracked() is an "evil macro" that checks
    // to be sure that its argument is an LVALUE, so we have to take an
    // address locally...but there's no function call.

    INLINE void Corrupt_If_Debug(Cell& ref)
      { Cell* c = &ref; Init_Unreadable_Untracked(c); }

  #if CHECK_CELL_SUBCLASSES
    INLINE void Corrupt_If_Debug(Atom& ref)
      { Atom* a = &ref; Init_Unreadable_Untracked(a); }

    INLINE void Corrupt_If_Debug(Value& ref)
      { Value* v = &ref; Init_Unreadable_Untracked(v); }

    INLINE void Corrupt_If_Debug(Element& ref)
      { Element* e = &ref; Init_Unreadable_Untracked(e); }
  #endif
#endif


//=//// CELL "FRESHNESS" //////////////////////////////////////////////////=//
//
// Most read and write operations of cells assert that the header has both
// NODE_FLAG_NODE and NODE_FLAG_CELL set.  But there is an exception made when
// it comes to initialization: a cell is allowed to have a header that is all
// 0 bits (e.g. CELL_MASK_ERASED_0).  Ranges of cells can be memset() to 0
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
// Note if CELL_FLAG_PROTECTED is set on a cell, it will not be considered
// fresh for initialization.  So the flag must be cleared or the cell "hard"
// erased (with Force_Erase_Cell()) in order to overwrite it.
//
// 1. "evil macros" for checked build performance, see STATIC_ASSERT_LVALUE()
//

#define CELL_MASK_PERSIST \
    (NODE_FLAG_MANAGED | NODE_FLAG_ROOT | NODE_FLAG_MARKED)

#define Freshen_Cell_Header(c) do { \
    STATIC_ASSERT_LVALUE(c);  /* evil macro [1] */ \
    Assert_Cell_Initable(c);  /* if CELL_MASK_ERASED_0, no node+cell flags */ \
    (c)->header.bits &= CELL_MASK_PERSIST;  /* won't add node+cell flags */ \
} while (0)


//=//// HOOKABLE HEART_BYTE() ACCESSOR ////////////////////////////////////=//
//
// This has to be defined after `Cell` is fully defined.
//
// 1. In lieu of typechecking cell is-a cell, we assume the macro finding
//    a field called ->header with .bits in it is good enough.  All methods of
//    checking seem to add overhead in the RUNTIME_CHECKS build that isn't
//    worth it.  To help avoid accidentally passing stubs, the HeaderUnion in
//    a Stub is named "leader" instead of "header".
//
// 2. It can often be helpful to inject code to when the HEART_BYTE() is being
//    assigned.  This mechanism also intercepts reads of the HEART_BYTE() too,
//    which is done pervasively.  It slows down the code in checked builds by
//    a noticeable amount, so we don't put it in all checked builds...only
//    special situations.
//
#if (! DEBUG_HOOK_HEART_BYTE)
    #define HEART_BYTE(cell) \
        SECOND_BYTE(&(cell)->header.bits)  // don't use ensure() [1]
#else
    struct HeartHolder {  // class for intercepting heart assignments [2]
        Cell* & ref;

        HeartHolder(const Cell* const& ref)
            : ref (const_cast<Cell* &>(ref))
          {}

        void operator=(Byte right) {
            SECOND_BYTE(&(ref)->header.bits) = right;
        }

        operator Heart () const
          { return static_cast<Heart>(SECOND_BYTE(&(ref)->header.bits)); }
    };
    #define HEART_BYTE(cell) \
        HeartHolder {cell}
#endif

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


//=//// CELL HEADERS AND PREPARATION //////////////////////////////////////=//
//
// Assert_Cell_Initable() for the explanation of what "freshening" is, and why
// it tolerates CELL_MASK_ERASED_0 in a cell header.

INLINE void Reset_Cell_Header_Noquote(Cell* c, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE_ANTIFORM_0);
    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        NODE_FLAG_NODE | NODE_FLAG_CELL | flags | FLAG_QUOTE_BYTE(NOQUOTE_1)
    );
}

INLINE void Reset_Cell_Header(Cell* c, Byte quote_byte, uintptr_t flags)
{
    assert((flags & FLAG_QUOTE_BYTE(255)) == FLAG_QUOTE_BYTE_ANTIFORM_0);
    Freshen_Cell_Header(c);  // if CELL_MASK_ERASED_0, node+cell flags not set
    c->header.bits |= (  // need to ensure node+cell flag get set
        NODE_FLAG_NODE | NODE_FLAG_CELL | flags | FLAG_QUOTE_BYTE(quote_byte)
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

#if (! DEBUG_CHECK_BINDING)
    #define Assert_Cell_Binding_Valid(v)  NOOP
#else
    #define Assert_Cell_Binding_Valid(v) \
        Assert_Cell_Binding_Valid_Core(v)
#endif

#define Cell_Binding(v) \
    x_cast(Context*, (v)->extra.Any.node)

#if (! DEBUG_CHECK_BINDING)
    #define BINDING(v) \
        *x_cast(Context**, m_cast(Node**, &(v)->extra.Any.node))
#else
    struct BindingHolder {
        Cell* & ref;

        BindingHolder(const Cell* const& ref)
            : ref (const_cast<Cell* &>(ref))
        {
            assert(Is_Bindable_Heart(Cell_Heart(ref)));
        }

        void operator=(Stub* right) {
            Assert_Cell_Writable(ref);
            ref->extra.Any.node = right;
            Assert_Cell_Binding_Valid(ref);
        }
        void operator=(BindingHolder const& right) {
            Assert_Cell_Writable(ref);
            ref->extra.Any.node = right.ref->extra.Any.node;
            Assert_Cell_Binding_Valid(ref);
        }
        void operator=(nullptr_t) {
            Assert_Cell_Writable(ref);
            ref->extra.Any.node = nullptr;
        }
        template<typename T>
        void operator=(Option(T) right) {
            Assert_Cell_Writable(ref);
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
//    will compete with one as (Init(Value), const Value*) when the second
//    argument is Element*, since Element can be passed where Value is taken.
//    Template magic lets an overload exclude itself to break the contention.

#define CELL_MASK_COPY \
    ~(CELL_MASK_PERSIST | CELL_FLAG_NOTE | CELL_FLAG_PROTECTED)

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
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
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
    out->header.bits |= (NODE_FLAG_NODE | NODE_FLAG_CELL  // ensure NODE+CELL
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

    template<  // avoid overload conflict when Element* coerces to Value* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T,const Value*>::value
            && !std::is_convertible<T,const Element*>::value
        >::type* = nullptr
    >
    INLINE Value* Copy_Cell_Overload(Init(Value) out, const T& v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    INLINE Atom* Copy_Cell_Overload(Init(Atom) out, const Atom* v) {
        Copy_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Copy_Cell(out,v) \
        Copy_Cell_Overload(TRACK(out), (v))
#endif

#define Copy_Cell_Core(out,v,copy_mask) \
    Copy_Cell_Untracked(TRACK(out), (v), (copy_mask))

#define Copy_Meta_Cell(out,v) \
    cast(Element*, Meta_Quotify(Copy_Cell(u_cast(Atom*, (out)), (v))))


//=//// CELL MOVEMENT //////////////////////////////////////////////////////=//
//
// Cell movement is distinct from cell copying, because it invalidates the
// old location (which must be mutable).  The old location is erased if it's
// an Atom and can legally hold CELL_MASK_ERASED_0 for GC, or it's set
// to be trash (quasiform BLANK!) if it can't hold that state.
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

#define CELL_MASK_TRASH \
    (NODE_FLAG_NODE | NODE_FLAG_CELL \
        | FLAG_HEART_BYTE(REB_BLANK) | FLAG_QUOTE_BYTE(NOQUOTE_1) \
        | CELL_MASK_NO_NODES)

INLINE Cell* Move_Cell_Untracked(
    Cell* out,
    Cell* c,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, c, copy_mask);  // Move_Cell() adds track to `out`
    Assert_Cell_Header_Overwritable(c);
    c->header.bits = CELL_MASK_TRASH;  // fast overwrite

    Corrupt_Pointer_If_Debug(c->extra.Any.corrupt);
    Corrupt_Pointer_If_Debug(c->payload.Any.first.corrupt);
    Corrupt_Pointer_If_Debug(c->payload.Any.second.corrupt);

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

    template<  // avoid overload conflict when Element* coerces to Value* [3]
        typename T,
        typename std::enable_if<
            std::is_convertible<T,Value*>::value
            && !std::is_convertible<T,Element*>::value
        >::type* = nullptr
    >
    INLINE Value* Move_Cell_Overload(Init(Value) out, const T& v) {
        Move_Cell_Untracked(out, v, CELL_MASK_COPY);
        return out;
    }

    #define Move_Cell(out,v) \
        Move_Cell_Overload(TRACK(out), (v))
#endif

#define Move_Cell_Core(out,v,cell_mask) \
    Move_Cell_Untracked(TRACK(out), (v), (cell_mask))

#define Move_Meta_Cell(out,v) \
    cast(Element*, Meta_Quotify(Move_Cell_Core((out), (v), CELL_MASK_COPY)))

INLINE Atom* Move_Atom_Untracked(
    Atom* out,
    Atom* a,
    Flags copy_mask
){
    Copy_Cell_Untracked(out, a, copy_mask);  // Move_Cell() adds track to `out`
    Assert_Cell_Header_Overwritable(a);  // atoms can't have persistent bits
    a->header.bits = CELL_MASK_ERASED_0;  // legal state for atoms

    Corrupt_Pointer_If_Debug(a->extra.Any.corrupt);
    Corrupt_Pointer_If_Debug(a->payload.Any.first.corrupt);
    Corrupt_Pointer_If_Debug(a->payload.Any.second.corrupt);

    return out;
}

#define Move_Atom(out,a) \
    Move_Atom_Untracked(TRACK(out), (a), CELL_MASK_COPY)

#define Move_Meta_Atom(out,a) \
    cast(Element*, Meta_Quotify( \
        Move_Atom_Untracked(TRACK(out), (a), CELL_MASK_COPY)))


//=//// CELL CONST INHERITANCE ////////////////////////////////////////////=//
//
// Various operations are complicit (e.g. SELECT or FIND) in propagating the
// constness from the input series to the output value.
//
// (See CELL_FLAG_CONST for more information.)

INLINE Atom* Inherit_Const(Atom* out, const Cell* influencer) {
    out->header.bits |= (influencer->header.bits & CELL_FLAG_CONST);
    return out;
}

#define Trust_Const(value) \
    (value)  // just a marking to say the const is accounted for already

INLINE Value* Constify(Value* v) {
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
//     Force_Erase_Cell(&element);  // one possibility for making inits valid
//     Init_Integer(&element, 1020);
//
// We can abstract this with a macro, that can also remove the need to use &,
// by making the passed-in name an alias for the address of the cell:
//
//     DECLARE_ELEMENT (element)
//     Init_Integer(element, 1020);
//
// However, Force_Erase_Cell() has a header that's all 0 bits, hence it does
// not carry NODE_FLAG_NODE or NODE_FLAG_CELL.  We would like to be able to
// protect the lifetimes of these cells without giving them content:
//
//     DECLARE_ELEMENT (element);
//     Push_Lifeguard(element);
//
// But Push_Lifeguard() shouldn't be tolerant of erased cells.  So we assign
// the header with CELL_MASK_UNREADABLE instead of CELL_MASK_ERASED_0.
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
    Force_Unreadable_Cell_Untracked(name)  // macro, not a function (fast)

#define DECLARE_VALUE(name) \
    Value name##_value; \
    Value* name = TRACK(&name##_value); \
    Force_Unreadable_Cell_Untracked(name)  // macro, not a function (fast)

#define DECLARE_ELEMENT(name) \
    Element name##_element; \
    Element* name = TRACK(&name##_element); \
    Force_Unreadable_Cell_Untracked(name)  // macro, not a function (fast)


//=//// rebReleaseAndNull overload ////////////////////////////////////////=//
//
// rebReleaseAndNull is in the API, but because the API doesn't make
// distinctions between Element and Value the double pointer trips it up
// in the C++ build.  Add an overload.
//
#if CHECK_CELL_SUBCLASSES
    static inline void rebReleaseAndNull(Element** e) {
        rebRelease(*e);
        *e = nullptr;
    }
#endif
