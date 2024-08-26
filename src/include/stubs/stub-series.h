//
//  File: %stub-series.h
//  Summary: "any-series? defs AFTER %tmp-internals.h (see: %struct-stub.h)"
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
// Flex is a low-level implementation of something similar to a resizable
// vector or array in other languages...though its implementation is currently
// more like a "deque" (double-ended-queue):
//
//   https://en.wikipedia.org/wiki/Double-ended_queue
//
// In any case, it is an abstraction which represents a contiguous region of
// memory containing equally-sized elements...but with several additional
// features that are specific to the needs of Rebol.  These features allow
// storing of a byte representing the "Flavor" of a Flex, as well as several
// hidden pointers (Misc, Link) and many Flags (Leader, Info).
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * In R3-Alpha, Flex was called "REBSER".  Ren-C avoids calling this data
//   structure "Series" because the usermode concept of ANY-SERIES? bundles
//   added information (an Index and a Binding), and using the same term
//   would cause confusion for those trying to delve into the implementation:
//
//     https://forum.rebol.info/t/2221
//
// * Flex subclasses Array, Context, Action, Map are defined which are
//   explained where they are defined in separate header files.
//
// * It is desirable to have Flex subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a Flex* would actually be handled in the same
//   way for a Array*.  Plus, just because a Context* is implemented as a
//   Array* with a link to another Array* doesn't mean most clients should
//   be accessing the Array.  In a C++ build, very simple inheritance is used
//   to implement these type safeties--but in a C build, all the sublcass
//   names are just aliases for Flex, so there's less checking.
//


//=//// FLEX ACCESSIBILITY ////////////////////////////////////////////////=//
//
// An inaccessible Flex is one which may still have extant references, but
// the data is no longer available.  Some internal mechanics can create this
// situation, such as EVAL of a FRAME! which steals the memory of the frame
// to execute it...leaving the old stub as inaccessible.  There is also a
// FREE operation that users can use to manually throw away data.
//
// It would be costly if all Flex access operations had to check the
// accessibility bit.  Instead, the general pattern is that code that extracts
// Flex from a cell, e.g. Cell_Array(), performs a check to make sure that
// the Flex is accessible at the time of extraction.  Subsequent access of
// the extracted Flex is then unchecked.
//
// When the GC runs, it canonizes all inaccessible Flexes to a single canon
// inaccessible stub.  This compacts memory of references that have expired.
//

#define Not_Node_Accessible(n)          Is_Node_Free(n)
#define Is_Node_Accessible(n)           Not_Node_Free(n)

#define Assert_Node_Accessible(n) \
    assert(Is_Node_Accessible(n))

#define Set_Flex_Inaccessible(f) \
    Set_Node_Free_Bit(f)


//=//// FLEX "FLAG" BITS //////////////////////////////////////////////////=//
//
// See definitions of FLEX_FLAG_XXX.
//
// Using token pasting macros achieves some brevity, but also helps to avoid
// mixups with FLEX_INFO_XXX!
//
// 1. Avoid cost that inline functions (even constexpr) add to debug builds
//    by "typechecking" via finding the name ->leader.bits in (f).  (The name
//    "leader" is chosen to prevent calls with cells, which use "header".)
//
// 2. Flex flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.  We do this on the HeaderUnion
//    vs. x_cast() on the (f) to get the typechecking of [1]

#define Get_Flex_Flag(f,name) \
    (((f)->leader.bits & FLEX_FLAG_##name) != 0)

#define Not_Flex_Flag(f,name) \
    (((f)->leader.bits & FLEX_FLAG_##name) == 0)

#define Set_Flex_Flag(f,name) \
    m_cast(union HeaderUnion*, &(f)->leader)->bits |= FLEX_FLAG_##name

#define Clear_Flex_Flag(f,name) \
    m_cast(union HeaderUnion*, &(f)->leader)->bits &= ~FLEX_FLAG_##name


//=//// FLEX SUBCLASS FLAGS ///////////////////////////////////////////////=//
//
// In the debug build, ensure_flavor() checks if a Flex Stub matches the
// expected FLAVOR_XXX, and panics if it does not.  This is used by the
// subclass testing macros as a check that you are testing the flag for the
// Flavor that you expect.
//
// 1. See Set_Flex_Flag()/Clear_Flex_Flag() for why implicit mutability.

#if (! CPLUSPLUS_11) || (! DEBUG)
    #define ensure_flavor(flavor,flex) \
        (flex)  // no-op in release build
#else
    template<typename T>
    INLINE T ensure_flavor(Flavor flavor, T flex) {
        if (Flex_Flavor(flex) != flavor) {
            Flavor actual_flavor = Flex_Flavor(flex);
            USED(actual_flavor);
            assert(!"Flex Flavor did not match what caller expected");
        }
        return flex;
    }
#endif

#define Get_Subclass_Flag(subclass,flex,name) \
    ((ensure_flavor(FLAVOR_##subclass, (flex))->leader.bits \
        & subclass##_FLAG_##name) != 0)

#define Not_Subclass_Flag(subclass,flex,name) \
    ((ensure_flavor(FLAVOR_##subclass, (flex))->leader.bits \
        & subclass##_FLAG_##name) == 0)

#define Set_Subclass_Flag(subclass,flex,name) \
    m_cast(union HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (flex))->leader)->bits \
        |= subclass##_FLAG_##name

#define Clear_Subclass_Flag(subclass,flex,name)\
    m_cast(union HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (flex))->leader)->bits \
        &= ~subclass##_FLAG_##name


//=//// LINK AND MISC HELPERS /////////////////////////////////////////////=//
//
// Every Flex node has two generic platform-pointer-sized slots, called LINK
// and MISC, that can store arbitrary information.  How that is interpreted
// depends on the Flex subtype (its FLAVOR_XXX byte).
//
// Some of these slots hold other Node pointers that need to be GC marked.  But
// rather than a switch() statement based on subtype to decide what to mark
// or not, the GC is guided by generic flags in the Flex header called
// LINK_NEEDS_MARKED and MISC_NEEDS_MARKED.
//
// Yet the link and misc actually mean different things for different subtypes.
// A FLAVOR_STRING node's LINK points to a list that maps byte positions to
// UTF-8 codepoint boundaries.  But a FLAVOR_SYMBOL Flex uses the LINK for a
// pointer to another symbol's synonym.
//
// A C program could typically deal with this using a union, to name the same
// memory offset in different ways.  Here `link` would be a union {}:
//
//      BookmarkList* books = string.link.bookmarks;
//      string.link.bookmarks = books;
//
//      const Symbol* synonym = symbol.link.synonym;
//      symbol.link.synonym = synonym;
//
// The GC could then read a generic field like `flex->link.node` when doing
// its marking.  This would be fine in C so long as the types were compatible,
// it's called "type punning".
//
// But that's not legal in C++!
//
//  "It's undefined behavior to read from the member of the union that
//   wasn't most recently written."
//
//  https://en.cppreference.com/w/cpp/language/union
//
// We use a workaround that brings in some heavy debug build benefits.  The
// LINK() and MISC() macros force all assignments and reads through a common
// field.  e.g. the following assigns and reads the same field ("node"), but
// the instances document it is for "bookmarks" or "synonym":
//
//      BookmarkList* books = LINK(Bookmarks, string);  // reads `node`
//      LINK(Bookmarks, string) = books;
//
//      const Symbol* synonym = LINK(Synonym, symbol);  // also reads `node`
//      LINK(Synonym, symbol) = synonym;
//
// The syntax is *almost* as readable, but throws in benefits of offering some
// helpful debug runtime checks that you're accessing what the Flex holds.
// It has yet another advantage because it allows new "members" to be "added"
// by extension code that wouldn't be able to edit a union in a core header.
//
// To use the LINK() and MISC(), you must define two macros, like this:
//
//      #define LINK_Bookmarks_TYPE     BookmarkList*
//      #define HAS_LINK_Bookmarks      FLAVOR_STRING
//
// You get the desired properties of being easy to find cases of a particular
// interpretation of the field, along with type checking on the assignment,
// and a cast operation that does potentially heavy debug checks on the
// extraction.
//

#if (! CPLUSPLUS_11) || (! DEBUG)
    #define LINK(Field, flex) \
        *x_cast(LINK_##Field##_TYPE*, m_cast(Node**, &(flex)->link.any.node))

    #define MISC(Field, flex) \
        *x_cast(MISC_##Field##_TYPE*, m_cast(Node**, &(flex)->misc.any.node))

#else
    #define LINK(Field, flex) \
        NodeHolder<LINK_##Field##_TYPE>( \
            ensure_flavor(HAS_LINK_##Field, (flex))->link.any.node)

    #define MISC(Field, flex) \
        NodeHolder<MISC_##Field##_TYPE>( \
            ensure_flavor(HAS_MISC_##Field, (flex))->misc.any.node)
#endif

#define node_LINK(Field, flex) \
    *m_cast(Node**, &(flex)->link.any.node)  // const ok for strict alias

#define node_MISC(Field, flex) \
    *m_cast(Node**, &(flex)->misc.any.node)  // const ok for strict alias


//=//// FLEX "INFO" BITS (or INODE) ///////////////////////////////////////=//
//
// See definitions of FLEX_INFO_XXX.
//
// Using token pasting macros helps avoid mixups with FLEX_FLAG_XXX!
//
// Not all Flex Stubs have info bits, as some use the space to store a GC
// markable Node.  This "INODE" is accessed via macros in the same way as the
// LINK() and MISC() macros (described in the section above):
//
// 1. See mutability notes on Set_Flex_Flag()/Get_Flex_Flag().  The same
//    applies to the info flags.
//
// 2. We check that the info is being used for bits, not an "INODE".
//    Assume Flavor has INFO_NODE_NEEDS_MARK right.
//
#if (! CPLUSPLUS_11)
    #define FLEX_INFO(f) \
        x_cast(Flex*, ensure(const Flex*, (f)))->info.any.flags  // [1]
#else
    INLINE uintptr_t &FLEX_INFO(const Flex* f) {
        assert(Not_Flex_Flag(f, INFO_NODE_NEEDS_MARK));  // [2]
        return m_cast(Flex*, f)->info.any.flags;  // [1]
    }
#endif

#define Get_Flex_Info(f,name) \
    ((FLEX_INFO(f) & FLEX_INFO_##name) != 0)

#define Not_Flex_Info(f,name) \
    ((FLEX_INFO(f) & FLEX_INFO_##name) == 0)

#define Set_Flex_Info(f,name) \
    FLEX_INFO(f) |= FLEX_INFO_##name

#define Clear_Flex_Info(f,name) \
    FLEX_INFO(f) &= ~FLEX_INFO_##name

#if (! DEBUG) || (! CPLUSPLUS_11)
    #define INODE(Field, flex) \
        *x_cast(INODE_##Field##_TYPE*, m_cast(Node**, &(flex)->info.any.node))
#else
    #define INODE(Field,flex) \
        NodeHolder<INODE_##Field##_TYPE>( \
            ensure_flavor(HAS_INODE_##Field, (flex))->info.any.node)
#endif

#define node_INODE(Field, flex) \
    *m_cast(Node**, &(flex)->info.any.node)  // const ok for strict alias


//=//// FLEX CAPACITY AND TOTAL SIZE //////////////////////////////////////=//
//
// See documentation of `bias` and `rest` in %struct-stub.h
//

INLINE bool Is_Flex_Biased(const Flex* f) {
    assert(Get_Flex_Flag(f, DYNAMIC));
    return not IS_VARLIST(f);
}

INLINE REBLEN Flex_Bias(const Flex* f) {
    if (not Is_Flex_Biased(f))
        return 0;
    return cast(REBLEN, ((f)->content.dynamic.bonus.bias >> 16) & 0xffff);
}

#define MAX_FLEX_BIAS 0x1000

INLINE void Set_Flex_Bias(Flex* f, REBLEN bias) {
    assert(Is_Flex_Biased(f));
    f->content.dynamic.bonus.bias =
        (f->content.dynamic.bonus.bias & 0xffff) | (bias << 16);
}

INLINE void Add_Flex_Bias(Flex* f, REBLEN b) {
    assert(Is_Flex_Biased(f));
    f->content.dynamic.bonus.bias += b << 16;
}

INLINE void Subtract_Flex_Bias(Flex* f, REBLEN b) {
    assert(Is_Flex_Biased(f));
    f->content.dynamic.bonus.bias -= b << 16;
}

INLINE Length Flex_Rest(const Flex* f) {
    if (Get_Flex_Flag(f, DYNAMIC))
        return f->content.dynamic.rest;

    if (Is_Flex_Array(f))
        return 1;  // capacity of singular non-dynamic arrays is exactly 1

    assert(sizeof(f->content) % Flex_Wide(f) == 0);
    return sizeof(f->content) / Flex_Wide(f);
}

INLINE size_t Flex_Total(const Flex* f)
  { return (Flex_Rest(f) + Flex_Bias(f)) * Flex_Wide(f); }


//=//// FLEX "BONUS" //////////////////////////////////////////////////////=//
//
// If a dynamic Flex isn't modified in ways that can leave extra capacity at
// the head, it might want to use the bias slot for something else.  This usage
// is called the "bonus".
//

#if (! CPLUSPLUS_11)
    #define FLEX_BONUS(f) \
        (f)->content.dynamic.bonus.node
#else
    INLINE const Node* const &FLEX_BONUS(const Flex* f) {
        assert(Get_Flex_Flag(f, DYNAMIC));
        return f->content.dynamic.bonus.node;
    }
    INLINE const Node* &FLEX_BONUS(Flex* f) {
        assert(Get_Flex_Flag(f, DYNAMIC));
        return f->content.dynamic.bonus.node;
    }
#endif

#if (! DEBUG) || (! CPLUSPLUS_11)
    #define BONUS(Field, s) \
        *x_cast(BONUS_##Field##_TYPE*, m_cast(Node**, &FLEX_BONUS(s)))
#else
    #define BONUS(Field, s) \
        NodeHolder<BONUS_##Field##_TYPE>( \
            FLEX_BONUS(ensure_flavor(HAS_BONUS_##Field, (s))))
#endif

#define node_BONUS(Field, s) \
    *m_cast(Node**, &FLEX_BONUS(s))  // const ok for strict alias


//=//// FLEX "TOUCH" FOR DEBUGGING ////////////////////////////////////////=//
//
// **IMPORTANT** - This is defined early before code that does manipulation
// on Flex, because it can be very useful in debugging the low-level code.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's nice to be able to trigger a debug_break() after-the-fact on some kind
// of guard which can show the stack where it was set.  Generally, Flex get
// this guard put on it at allocation time.  But if you want to mark a moment
// later as notable to trace back to, you can.
//
// This works with Address Sanitizer or with Valgrind, but the config flag to
// enable it only comes automatically with address sanitizer.
//
// 1. In the general case, you can't assume the incoming stub has valid data,
//    as the default is to call it after only the header bits are set.  But
//    in case it helps, the s->guard is set to nullptr by Alloc_Stub(), so
//    conditional instrumentation here can distinguish fresh from valid.
//

#if DEBUG_FLEX_ORIGINS || DEBUG_COUNT_TICKS
    INLINE void Touch_Stub(Stub *s)  // if alloc, only header valid [1]
    {
      #if DEBUG_FLEX_ORIGINS
        s->guard = cast(Byte*, malloc(sizeof(Byte)));  // smallest allocation
        *s->guard = FREE_POOLUNIT_BYTE;  // irrelevant, but disruptive choice
        free(s->guard);
      #endif

      #if DEBUG_COUNT_TICKS
        s->tick = TG_tick;
      #else
        s->tick = 0;
      #endif
    }

    #define Touch_Stub_If_Debug(s) Touch_Stub(s)
#else
    #define Touch_Stub_If_Debug(s) NOOP
#endif


//=//// NUMBER OF WIDTH-SIZED UNITS "USED" IN FLEX ////////////////////////=//
//
// There is an optimization based on FLEX_FLAG_DYNAMIC that allows data
// which is sizeof(Cell) or smaller to fit directly in the Flex Stub.
//
// 1. If a Flex is dynamically allocated out of the memory pools, then
//    without the data itself taking up the StubContent, there's room for a
//    full used count in the content.
//
// 2. A non-dynamic Array can store one or zero cells in the StubContent.
//    We report the units used as being 0 if it's the distinguished case of
//    a poisoned cell (added benefit: catches stray writes).
//
// 3. Other non-dynamic Flexes are short, and so they use a byte out of the
//    Flex Info to store the units used.  (This byte is currently free for
//    other purposes in cases [1] and [2].)
//

INLINE Length Flex_Used(const Flex* f) {
    if (Get_Flex_Flag(f, DYNAMIC))
        return f->content.dynamic.used;  // length stored in header [1]
    if (Is_Flex_Array(f)) {
        if (Is_Cell_Poisoned(&f->content.fixed.cell))  // empty singular [2]
            return 0;
        return 1;  // one-element singular array [2]
    }
    return USED_BYTE(f);  // small Flex length < sizeof(StubContent) [3]
}

INLINE Length Flex_Dynamic_Used(const Flex* f) {
    assert(Get_Flex_Flag(f, DYNAMIC));
    return f->content.dynamic.used;
}

#define Is_Flex_Full(f) \
    (Flex_Used(f) + 1 >= Flex_Rest(f))

#define Flex_Available_Space(f) \
    (Flex_Rest(f) - (Flex_Used(f) + 1))  // space minus a terminator

#define Flex_Fits(f,n) \
    ((Flex_Used(f) + (n) + 1) <= Flex_Rest(f))


//=//// FLEX DATA ACCESSORS ///////////////////////////////////////////////=//
//
// 1. Callers like Cell_String() or Cell_Array() are expected to test for
//    NODE_FLAG_FREE and fail before getting as far as calling these routines.
//
// 2. Because these inline functions are called so often, Flex_Data_At()
//    duplicates the code in Flex_Data() rather than call it.  Be sure
//    to change both routines if changing one of them.
//
// 3. The C++ build uses `const` pointers to enforce the notion of immutable
//    Flexes at compile time.  So a const Flex pointer should give a const
//    data pointer back.  Plain C would need two differently-named functions
//    to do this, which is deemed too ugly at callsites...so it's only done
//    with overloading in C++.  See %sys-protect.h for more information.
//
// 4. Note that Flex indexing in C is zero based.  So as far as Flex is
//    concerned, `Flex_Head(T, s)` is the same as `Flex_At(T, s, 0)`
//
// 5. The clever c_cast() macro is used here to avoid writing overloads just
//    to get a const vs. non-const response.  But it only works to avoid the
//    overload if you can write it as a macro, and asserting on the Flex
//    would repeat the argument twice in a macro body (bad mojo!)
//

INLINE Byte* Flex_Data(const_if_c Flex* f) {  // assume valid [1]
    return Get_Flex_Flag(f, DYNAMIC)  // inlined in Flex_Data_At() [2]
        ? u_cast(Byte*, f->content.dynamic.data)
        : u_cast(Byte*, &f->content);
}

INLINE Byte* Flex_Data_At(Byte w, const_if_c Flex* f, REBLEN i) {
  #if !defined(NDEBUG)
    if (w != Flex_Wide(f)) {  // will be "unusual" value if free
        if (Is_Node_Free(f))
            printf("Flex_Data_At() asked on freed Flex\n");
        else
            printf(
                "Flex_Data_At() asked %d on width=%d\n",
                w,
                cast(int, Flex_Wide(f))
            );
        panic (f);
    }
  #endif

    assert(i <= Flex_Used(f));

    return ((w) * (i)) + (  // v-- inlining of Flex_Data() [2]
        Get_Flex_Flag(f, DYNAMIC)
            ? cast(Byte*, f->content.dynamic.data)
            : cast(Byte*, &f->content)
        );
}

#if CPLUSPLUS_11  // give back const pointer on const Flex input [3]
    INLINE const Byte* Flex_Data(const Flex* s)
      { return Flex_Data(m_cast(Flex*, s)); }

    INLINE const Byte* Flex_Data_At(
        Byte w,
        const Flex* s,
        REBLEN i
    ){
        return Flex_Data_At(w, m_cast(Flex*, s), i);
    }
#endif

#define Flex_At(T,f,i) \
    c_cast(T*, Flex_Data_At(sizeof(T), (f), (i)))  // zero-based [4]

#if DEBUG
    #define Flex_Head(T,f) \
        Flex_At(T, (f), 0)  // Flex_Data() doesn't check width, _At() does
#else
    #define Flex_Head(T,f) \
        c_cast(T*, Flex_Data(f))  // slightly faster, but no width check
#endif

#define Flex_Data_Tail(w,f) \
    c_cast(Byte*, Flex_Data_At((w), (f), Flex_Used(f)))

#define Flex_Tail(T,f) \
    c_cast(T*, Flex_Data_Tail(sizeof(T), (f)))

INLINE Byte* Flex_Data_Last(size_t wide, const_if_c Flex* f) {
    assert(Flex_Used(f) != 0);
    return Flex_Data_At(wide, f, Flex_Used(f) - 1);
}

#if CPLUSPLUS_11  // can't use c_cast() to inherit const, must overload [5]
    INLINE const Byte* Flex_Data_Last(size_t wide, const Flex* f) {
        assert(Flex_Used(f) != 0);
        return Flex_Data_At(wide, f, Flex_Used(f) - 1);
    }
#endif

#define Flex_Last(T,f) \
    c_cast(T*, Flex_Data_Last(sizeof(T), (f)))


//=//// FLEX TERMINATION //////////////////////////////////////////////////=//
//
// R3-Alpha had a concept of termination which was that all Flexes had one
// full-sized unit at their tail which was set to zero bytes.  Ren-C moves
// away from this concept...it only has terminating '\0' on UTF-8 Strings,
// a reserved terminating *position* on Blobs (in case they become
// aliased as UTF-8 Strings), and the debug build terminates Arrays in order
// to catch out-of-bounds accesses more easily:
//
// https://forum.rebol.info/t/1445
//
// Under this strategy, most of the termination is handled by the functions
// that deal with their specific subclass (e.g. Make_String()).  But some
// generic routines that memcpy() data behind the scenes needs to be sure it
// maintains the invariant that the higher level routines want.
//
// 1. A Binary alias of a String must have all modifications keep it as valid
//    UTF-8, and it must maintain a `\0` terminator.  Because all Binary
//    are candidates for being aliased as String, they reserve a byte at
//    their tail.  This debug setting helps ensure that Blobs are setting
//    the '\0' tail intentionally when appropriate by poisoning the byte.
//
// 2. There's a difference with how byte buffers are handled vs. Array, in
//    that Arrays have to be expanded before they are written to, so that
//    the Cells are formatted.  Byte strings don't have that requirement,
//    so the code isn't stylized to set the used size first and then put
//    data into the buffer.  So it wouldn't do any good to put a poison
//    byte at the head of a Flex allocation and expect to be able to see
//    it before the termination.  Review if callers can/should be changed.
//

#if DEBUG_POISON_FLEX_TAILS
    #define ONE_IF_POISON_TAILS 1

    #define BINARY_BAD_UTF8_TAIL_BYTE 0xFE  // Blobs reserve tail byte [1]

    INLINE void Poison_Or_Unpoison_Tail_Debug(Flex* f, bool poison) {
        if (Flex_Wide(f) == 1) {  // presume BINARY! or ANY-STRING? (?)
            Byte* tail = Flex_Tail(Byte, f);
            if (poison)
                *tail = BINARY_BAD_UTF8_TAIL_BYTE;
            else {
                /* assert(  // doesn't have an invariant [2]
                    *tail == BINARY_BAD_UTF8_TAIL_BYTE or *tail == '\0'
                ); */
            }
        }
        else if (Is_Flex_Array(f) and Get_Flex_Flag(f, DYNAMIC)) {
            Cell* tail = Flex_At(Cell, f, f->content.dynamic.used);
            if (poison)
                Poison_Cell(tail);
            else {
                assert(Is_Cell_Poisoned(tail));
                Erase_Cell(tail);
            }
        }
    }

    #define Poison_Flex_Tail_If_Debug(f) \
        Poison_Or_Unpoison_Tail_Debug((f), true)

    #define Unpoison_Flex_Tail_If_Debug(s) \
        Poison_Or_Unpoison_Tail_Debug((f), false)
#else
    #define ONE_IF_POISON_TAILS 0

    #define Poison_Flex_Tail_If_Debug(f) NOOP
    #define Unpoison_Flex_Tail_If_Debug(f) NOOP
#endif

INLINE void Term_Flex_If_Necessary(Flex* f)
{
    if (Flex_Wide(f) == 1) {
        if (Is_Flex_UTF8(f))
            *Flex_Tail(Byte, f) = '\0';
        else {
          #if DEBUG_POISON_FLEX_TAILS
            *Flex_Tail(Byte, f) = BINARY_BAD_UTF8_TAIL_BYTE;
          #endif
        }
    }
    else if (Get_Flex_Flag(f, DYNAMIC) and Is_Flex_Array(f)) {
      #if DEBUG_POISON_FLEX_TAILS
        Poison_Cell(Flex_Tail(Cell, f));
      #endif
    }
}

#ifdef NDEBUG
    #define Assert_Flex_Term_If_Needed(f) \
        NOOP
#else
    #define Assert_Flex_Term_If_Needed(f) \
        Assert_Flex_Term_Core(f);
#endif

#define Note_Flex_Maybe_Term(f) NOOP  // use to annotate if may-or-may-not be


//=//// SETTING FLEX LENGTH/SIZE //////////////////////////////////////////=//
//
// 1. Right now FLEX_FLAG_FIXED_SIZE merely means they can't expand, but
//    they set the flag before initializing things like termination and the
//    length.  If this routine were to disallow it, then the flag wouldn't
//    be passed into Flex creation but could only be added afterward.
//
// 2. UTF-8 Strings maintain a length in codepoints (in misc.length), as well
//    as the size in bytes (as "used").  It's expected that both will be
//    updated together--see Term_String_Len_Size().  But sometimes the used
//    field is updated solo by a Binary-based routine in an intermediate step.
//    That's okay so long as the length is not consulted before the String
//    handling code finalizes it.  DEBUG_UTF8_EVERYWHERE makes violations
//    obvious by corrupting the length.

INLINE void Set_Flex_Used_Internal(Flex* f, Count used) {
    /* assert(Not_Flex_Flag(s, FIXED_SIZE)); */  // [1]
    if (Get_Flex_Flag(f, DYNAMIC))
        f->content.dynamic.used = used;
    else {
        assert(used < sizeof(f->content));

        if (Is_Flex_Array(f)) {  // content used by cell, no room for length
            if (used == 0)
                Poison_Cell(&f->content.fixed.cell);  // poison means 0 used
            else {
                assert(used == 1);  // any non-poison will mean length 1
                if (not Is_Cell_Poisoned(&f->content.fixed.cell)) {
                    // it was already length 1, leave the cell alone
                } else
                    Erase_Cell(&f->content.fixed.cell);
            }
        }
        else
            USED_BYTE(f) = used;
    }

  #if DEBUG_UTF8_EVERYWHERE
    if (Is_String_NonSymbol(f)) {
        Corrupt_If_Debug(f->misc.length);  // catch violators [2]
        Touch_Stub_If_Debug(f);
    }
  #endif
}

INLINE void Set_Flex_Used(Flex* f, Count used) {
    Unpoison_Flex_Tail_If_Debug(f);
    Set_Flex_Used_Internal(f, used);
    Poison_Flex_Tail_If_Debug(f);
}

INLINE void Set_Flex_Len(Flex* f, Length len) {
    assert(not Is_Flex_UTF8(f));  // use _Len_Size() instead [2]
    Set_Flex_Used(f, len);
}

#if CPLUSPLUS_11  // catch cases when calling on String* directly
    INLINE void Set_Flex_Len(String* s, Length len) = delete;
#endif

// Optimized expand when at tail (but, does not reterminate)
//
INLINE void Expand_Flex_Tail(Flex* f, REBLEN delta) {
    if (Flex_Fits(f, delta))
        Set_Flex_Used(f, Flex_Used(f) + delta);  // no termination implied
    else
        Expand_Flex(f, Flex_Used(f), delta);  // currently terminates
}



// Out of the 8 platform pointers that comprise a Flex Stub, only 3 actually
// need to be initialized to get a functional non-dynamic Flex or Array of
// length 0!  Only one is set here.  The info should be set by the caller.
//
INLINE Stub* Prep_Stub(void *preallocated, Flags flags) {
    assert(not (flags & NODE_FLAG_CELL));

    Stub *s = u_cast(Stub*, preallocated);

    s->leader.bits = NODE_FLAG_NODE | flags;  // #1

  #if !defined(NDEBUG)
    SafeCorrupt_Pointer_Debug(s->link.any.corrupt);  // #2
    Mem_Fill(&s->content.fixed, 0xBD, sizeof(s->content));  // #3 - #6
    SafeCorrupt_Pointer_Debug(s->info.any.corrupt);  // #7
    SafeCorrupt_Pointer_Debug(s->misc.any.corrupt);  // #8

  #if DEBUG_FLEX_ORIGINS
    s->guard = nullptr;  // so Touch_Stub() can tell data is invalid
  #endif

    Touch_Stub_If_Debug(s);  // tag current C stack as Flex origin in ASAN
  #endif

  #if DEBUG_COLLECT_STATS
    g_mem.num_flex_made += 1;
  #endif

    return s;
}

INLINE PoolId Pool_Id_For_Size(Size size) {
  #if DEBUG_ENABLE_ALWAYS_MALLOC
    if (g_mem.always_malloc)
        return SYSTEM_POOL;
  #endif

    if (size < POOLS_BY_SIZE_LEN)
        return g_mem.pools_by_size[size];

    return SYSTEM_POOL;
}


// If the data is tiny enough, it will be fit into the Flex Stub itself.
// A Small Flex will be allocated from a memory pool.
// A Large Flex will be allocated from system memory.
//
// 1. It is more efficient if you know a Flex is going to become managed to
//   create it in the managed state.  But be sure no evaluations are called
//   before it's made reachable by the GC, or use Push_GC_Guard().
//
INLINE Flex* Make_Flex_Into(
    void* preallocated,
    REBLEN capacity,
    Flags flags
){
    size_t wide = Wide_For_Flavor(Flavor_From_Flags(flags));
    if (cast(REBU64, capacity) * wide > INT32_MAX)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

    Stub* s = Prep_Stub(preallocated, flags);

  #if defined(NDEBUG)
    FLEX_INFO(s) = FLEX_INFO_MASK_NONE;
  #else
    if (flags & FLEX_FLAG_INFO_NODE_NEEDS_MARK)
        Corrupt_Pointer_If_Debug(s->info.any.node);
    else
        FLEX_INFO(s) = FLEX_INFO_MASK_NONE;
  #endif

    if (
        (flags & FLEX_FLAG_DYNAMIC)  // inlining will constant fold
        or (capacity * wide > sizeof(s->content))  // data won't fit in stub
    ){
        Set_Flex_Flag(s, DYNAMIC);

        if (not Did_Flex_Data_Alloc(s, capacity)) {
            Clear_Node_Managed_Bit(s);
            Set_Flex_Inaccessible(s);
            GC_Kill_Stub(s);

            fail (Error_No_Memory(capacity * wide));
        }

      #if DEBUG_COLLECT_STATS
        g_mem.flex_memory += capacity * wide;
      #endif
    }

    if (not (flags & NODE_FLAG_MANAGED)) {  // more efficient if managed [1]
        if (Is_Flex_Full(g_gc.manuals))
            Extend_Flex_If_Necessary(g_gc.manuals, 8);

        cast(Flex**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used++
        ] = s;  // will need to find/remove from this list later
    }

    return s;
}

#define Make_Flex_Core(capacity,flags) \
    Make_Flex_Into(Alloc_Pooled(STUB_POOL), (capacity), (flags))

#define Make_Flex(T,capacity,flags) \
    cast(T*, Make_Flex_Core((capacity), (flags)))


//=//// DEBUG FLEX MONITORING /////////////////////////////////////////////=//
//
// This once used a Flex flag in debug builds to tell whether a Flex was
// monitored or not.  But Flex flags are scarce, so the feature was scaled
// back to just monitoring a single node.  It could also track a list--but the
// point is just that stealing a flag is wasteful.
//
#if DEBUG_MONITOR_FLEX
    INLINE void Debug_Monitor_Flex(void *p) {
        printf("Adding monitor to %p on tick #%d\n", p, cast(int, TG_tick));
        fflush(stdout);
        g_mem.monitor_node = cast(Flex*, p);
    }
#endif
