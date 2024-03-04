//
//  File: %stub-series.h
//  Summary: "any-series? defs AFTER %tmp-internals.h (see: %struct-stub.h)"
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2023 Ren-C Open Source Contributors
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
// The word "Series" is overloaded in Rebol to refer to two related concepts:
//
// 1. The internal system type of Series.  It's a low-level implementation of
//    something similar to a vector or an array in other languages.  It is an
//    abstraction which represents a contiguous region of memory containing
//    equally-sized elements.
//
//   (For the struct definition of the series stub, see %struct-stub.h)
//
// 2. The user-level value type ANY-SERIES?.  This might be more accurately
//    called an iterator, because it includes both a pointer to a Series of
//    data and an index offset into that data.  An ANY-SERIES? cell contains
//    an `index` as the 0-based position into the series represented by this
//    ANY-VALUE? (so if it is 0 then that means a Rebol index of 1).
//
// Attempts to reconcile all the naming issues from historical Rebol have not
// yielded a satisfying alternative, so the ambiguity has stuck.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Series subclasses Array, Context, Action, Map are defined which are
//   explained where they are defined in separate header files.
//
// * It is desirable to have series subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a Series* would actually be handled in the same
//   way for a Array*.  Plus, just because a Context* is implemented as a
//   Array* with a link to another Array* doesn't mean most clients should
//   be accessing the array.  In a C++ build, very simple inheritance is used
//   to implement these type safeties--but in a C build, all the sublcass
//   names are just aliases for Series, so there's less checking.
//


//=//// SERIES ACCESSIBILITY ///////////////////////////////////////////////=//
//
// An inaccessible series is one which may still have extant references, but
// the data is no longer available.  Some internal mechanics can create this
// situation, such as EVAL of a FRAME! which steals the memory of the frame
// to execute it...leaving the old stub as inaccessible.  There is also a
// FREE operation that users can use to manually throw away data.
//
// It would be costly if all series access operations had to check the
// accessibility bit.  Instead, the general pattern is that code that extracts
// series from values, e.g. Cell_Array(), performs a check to make sure that
// the series is accessible at the time of extraction.  Subsequent access of
// the extracted series is then unchecked.
//
// When the GC runs, it canonizes all inaccessible series to a single canon
// inaccessible stub.  This compacts memory of references that have expired.
//

#define Not_Node_Accessible(n)          Is_Node_Free(n)
#define Is_Node_Accessible(n)           Not_Node_Free(n)

#define Assert_Node_Accessible(n) \
    assert(Is_Node_Accessible(n))

#define Set_Series_Inaccessible(s) \
    Set_Node_Free_Bit(s)


//=//// SERIES "FLAG" BITS /////////////////////////////////////////////////=//
//
// See definitions of SERIES_FLAG_XXX.
//
// Using token pasting macros achieves some brevity, but also helps to avoid
// mixups with SERIES_INFO_XXX!
//
// 1. Avoid cost that inline functions (even constexpr) add to debug builds
//    by "typechecking" via finding the name ->leader.bits in (s).  (The name
//    "leader" is chosen to prevent calls with cells, which use "header".)
//
// 2. Series flags are managed distinctly from conceptual immutability of their
//    data, and so we m_cast away constness.  We do this on the HeaderUnion
//    vs. x_cast() on the (s) to get the typechecking of [1]

#define Get_Series_Flag(s,name) \
    (((s)->leader.bits & SERIES_FLAG_##name) != 0)

#define Not_Series_Flag(s,name) \
    (((s)->leader.bits & SERIES_FLAG_##name) == 0)

#define Set_Series_Flag(s,name) \
    m_cast(union HeaderUnion*, &(s)->leader)->bits |= SERIES_FLAG_##name

#define Clear_Series_Flag(s,name) \
    m_cast(union HeaderUnion*, &(s)->leader)->bits &= ~SERIES_FLAG_##name


//=//// SERIES SUBCLASS FLAGS //////////////////////////////////////////////=//
//
// In the debug build, ensure_flavor() checks if a series node matches the
// expected FLAVOR_XXX, and panics if it does not.  This is used by the
// subclass testing macros as a check that you are testing the flag for the
// flavor that you expect.
//
// 1. See Set_Series_Flag()/Clear_Series_Flag() for why implicit mutability.

#if (! CPLUSPLUS_11) || (! DEBUG)
    #define ensure_flavor(flavor,s) \
        (s)  // no-op in release build
#else
    template<typename T>
    INLINE T ensure_flavor(Flavor flavor, T series) {
        if (Series_Flavor(series) != flavor) {
            Flavor actual_flavor = Series_Flavor(series);
            USED(actual_flavor);
            assert(!"series flavor did not match what caller expected");
        }
        return series;
    }
#endif

#define Get_Subclass_Flag(subclass,s,name) \
    ((ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        & subclass##_FLAG_##name) != 0)

#define Not_Subclass_Flag(subclass,s,name) \
    ((ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        & subclass##_FLAG_##name) == 0)

#define Set_Subclass_Flag(subclass,s,name) \
    m_cast(union HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (s))->leader)->bits \
        |= subclass##_FLAG_##name

#define Clear_Subclass_Flag(subclass,s,name)\
    m_cast(union HeaderUnion*, /* [1] */ \
        &ensure_flavor(FLAVOR_##subclass, (s))->leader)->bits \
        &= ~subclass##_FLAG_##name


//=//// LINK AND MISC HELPERS /////////////////////////////////////////////=//
//
// Every series node has two generic platform-pointer-sized slots, called LINK
// and MISC, that can store arbitrary information.  How that is interpreted
// depends on the series subtype (its FLAVOR_XXX byte).
//
// Some of these slots hold other node pointers that need to be GC marked.  But
// rather than a switch() statement based on subtype to decide what to mark
// or not, the GC is guided by generic flags in the series header called
// LINK_NEEDS_MARKED and MISC_NEEDS_MARKED.
//
// Yet the link and misc actually mean different things for different subtypes.
// A FLAVOR_STRING node's LINK points to a list that maps byte positions to
// UTF-8 codepoint boundaries.  But a FLAVOR_SYMBOL series uses the LINK for a
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
// The GC could then read a generic field like `series.link.node` when doing
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
// helpful debug runtime checks that you're accessing what the series holds.
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
    #define LINK(Field, s) \
        *x_cast(LINK_##Field##_TYPE*, m_cast(Node**, &(s)->link.any.node))

    #define MISC(Field, s) \
        *x_cast(MISC_##Field##_TYPE*, m_cast(Node**, &(s)->misc.any.node))

#else
    #define LINK(Field, s) \
        NodeHolder<LINK_##Field##_TYPE>( \
            ensure_flavor(HAS_LINK_##Field, (s))->link.any.node)

    #define MISC(Field, s) \
        NodeHolder<MISC_##Field##_TYPE>( \
            ensure_flavor(HAS_MISC_##Field, (s))->misc.any.node)
#endif

#define node_LINK(Field, s) \
    *m_cast(Node**, &(s)->link.any.node)  // const ok for strict alias

#define node_MISC(Field, s) \
    *m_cast(Node**, &(s)->misc.any.node)  // const ok for strict alias


//=//// SERIES "INFO" BITS (or INODE) //////////////////////////////////////=//
//
// See definitions of SERIES_INFO_XXX.
//
// Using token pasting macros helps avoid mixups with SERIES_FLAG_XXX!
//
// Not all series nodes have info bits, as some use the space to store a GC
// markable node.  This "INODE" is accessed via macros in the same way as the
// LINK() and MISC() macros (described in the section above):
//
// 1. See mutability notes on Set_Series_Flag()/Get_Series_Flag().  The same
//    applies to the info flags.
//
// 2. We check that the info is being used for bits, not an "INODE".  (A
//    checking SER_INODE() is overkill, given that the INODE() accessors
//    check the flavor.  Assume flavor has INFO_NODE_NEEDS_MARK right.)
//
#if (! CPLUSPLUS_11)
    #define SERIES_INFO(s) \
        x_cast(Series*, ensure(const Series*, (s)))->info.flags.bits  // [1]
#else
    INLINE uintptr_t &SERIES_INFO(const Series* s) {
        assert(Not_Series_Flag(s, INFO_NODE_NEEDS_MARK));  // [2]
        return m_cast(Series*, s)->info.flags.bits;  // [1]
    }
#endif

#define Get_Series_Info(s,name) \
    ((SERIES_INFO(s) & SERIES_INFO_##name) != 0)

#define Not_Series_Info(s,name) \
    ((SERIES_INFO(s) & SERIES_INFO_##name) == 0)

#define Set_Series_Info(s,name) \
    SERIES_INFO(s) |= SERIES_INFO_##name

#define Clear_Series_Info(s,name) \
    SERIES_INFO(s) &= ~SERIES_INFO_##name

#if (! DEBUG) || (! CPLUSPLUS_11)
    #define INODE(Field, s) \
        *x_cast(INODE_##Field##_TYPE*, m_cast(Node**, &(s)->info.node))
#else
    #define INODE(Field, s) \
        NodeHolder<INODE_##Field##_TYPE>( \
            ensure_flavor(HAS_INODE_##Field, (s))->info.node)
#endif

#define node_INODE(Field, s) \
    *m_cast(Node**, &(s)->info.node)  // const ok for strict alias


//=//// SERIES CAPACITY AND TOTAL SIZE /////////////////////////////////////=//
//
// See documentation of `bias` and `rest` in %struct-stub.h
//

INLINE bool Is_Series_Biased(const Series* s) {
    assert(Get_Series_Flag(s, DYNAMIC));
    return not IS_VARLIST(s);
}

INLINE REBLEN Series_Bias(const Series* s) {
    if (not Is_Series_Biased(s))
        return 0;
    return cast(REBLEN, ((s)->content.dynamic.bonus.bias >> 16) & 0xffff);
}

#define MAX_SERIES_BIAS 0x1000

INLINE void Set_Series_Bias(Series* s, REBLEN bias) {
    assert(Is_Series_Biased(s));
    s->content.dynamic.bonus.bias =
        (s->content.dynamic.bonus.bias & 0xffff) | (bias << 16);
}

INLINE void SER_ADD_BIAS(Series* s, REBLEN b) {
    assert(Is_Series_Biased(s));
    s->content.dynamic.bonus.bias += b << 16;
}

INLINE void SER_SUB_BIAS(Series* s, REBLEN b) {
    assert(Is_Series_Biased(s));
    s->content.dynamic.bonus.bias -= b << 16;
}

INLINE Length Series_Rest(const Series* s) {
    if (Get_Series_Flag(s, DYNAMIC))
        return s->content.dynamic.rest;

    if (Is_Series_Array(s))
        return 1;  // capacity of singular non-dynamic arrays is exactly 1

    assert(sizeof(s->content) % Series_Wide(s) == 0);
    return sizeof(s->content) / Series_Wide(s);
}

INLINE size_t Series_Total(const Series* s)
  { return (Series_Rest(s) + Series_Bias(s)) * Series_Wide(s); }


//=//// SERIES "BONUS" /////////////////////////////////////////////////////=//
//
// If a dynamic series isn't modified in ways that can leave extra capacity at
// the head, it might want to use the bias slot for something else.  This usage
// is called the "bonus".
//

#if (! CPLUSPLUS_11)
    #define Series_Bonus(s) \
        (s)->content.dynamic.bonus.node
#else
    INLINE const Node* const &Series_Bonus(const Series* s) {
        assert(Get_Series_Flag(s, DYNAMIC));
        return s->content.dynamic.bonus.node;
    }
    INLINE const Node* &Series_Bonus(Series* s) {
        assert(Get_Series_Flag(s, DYNAMIC));
        return s->content.dynamic.bonus.node;
    }
#endif

#if (! DEBUG) || (! CPLUSPLUS_11)
    #define BONUS(Field, s) \
        *x_cast(BONUS_##Field##_TYPE*, m_cast(Node**, &Series_Bonus(s)))
#else
    #define BONUS(Field, s) \
        NodeHolder<BONUS_##Field##_TYPE>( \
            Series_Bonus(ensure_flavor(HAS_BONUS_##Field, (s))))
#endif

#define node_BONUS(Field, s) \
    *m_cast(Node**, &Series_Bonus(s))  // const ok for strict alias


//=//// SERIES "TOUCH" FOR DEBUGGING ///////////////////////////////////////=//
//
// **IMPORTANT** - This is defined early before code that does manipulation
// on series, because it can be very useful in debugging the low-level code.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// It's nice to be able to trigger a debug_break() after-the-fact on some kind
// of guard which can show the stack where it was set.  Generally, series get
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

#if DEBUG_SERIES_ORIGINS || DEBUG_COUNT_TICKS
    INLINE void Touch_Stub_Debug(Stub *s)  // if alloc, only header valid [1]
    {
      #if DEBUG_SERIES_ORIGINS
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

    #define Touch_Stub_If_Debug(s) \
        Touch_Stub_Debug(s)
#else
    #define Touch_Stub_If_Debug(s) \
        NOOP
#endif


//=//// NUMBER OF WIDTH-SIZED UNITS "USED" IN SERIES //////////////////////=//
//
// There is an optimization based on SERIES_FLAG_DYNAMIC that allows data
// which is sizeof(Cell) or smaller to fit directly in the series stub.
//
// 1. If a series is dynamically allocated out of the memory pools, then
//    without the data itself taking up the StubContent, there's room for a
//    full used count in the content.
//
// 2. A non-dynamic array can store one or zero cells in the StubContent.
//    We report the units used as being 0 if it's the distinguished case of
//    a poisoned cell (added benefit: catches stray writes).
//
// 3. Other non-dynamic series are short, and so they use a byte out of the
//    series info to store the units used.  (This byte is currently free for
//    other purposes in cases [1] and [2].)
//

INLINE Length Series_Used(const Series* s) {
    if (Get_Series_Flag(s, DYNAMIC))
        return s->content.dynamic.used;  // length stored in header [1]
    if (Is_Series_Array(s)) {
        if (Is_Cell_Poisoned(&s->content.fixed.cell))  // empty singular [2]
            return 0;
        return 1;  // one-element singular array [2]
    }
    return USED_BYTE(s);  // small series length < sizeof(StubContent) [3]
}

INLINE Length Series_Dynamic_Used(const Series* s) {
    assert(Get_Series_Flag(s, DYNAMIC));
    return s->content.dynamic.used;
}

#define Is_Series_Full(s) \
    (Series_Used(s) + 1 >= Series_Rest(s))

#define Series_Available_Space(s) \
    (Series_Rest(s) - (Series_Used(s) + 1))  // space minus a terminator

#define Series_Fits(s,n) \
    ((Series_Used(s) + (n) + 1) <= Series_Rest(s))


//=//// SERIES DATA ACCESSORS /////////////////////////////////////////////=//
//
// 1. Callers like Cell_String() or Cell_Array() are expected to test for
//    NODE_FLAG_FREE and fail before getting as far as calling these routines.
//
// 2. Because these inline functions are called so often, Series_Data_At()
//    duplicates the code in Series_Data() rather than call it.  Be sure
//    to change both routines if changing one of them.
//
// 3. The C++ build uses `const` pointers to enforce the notion of immutable
//    series at compile time.  So a const series pointer should give a const
//    data pointer back.  Plain C would need two differently-named functions
//    to do this, which is deemed too ugly at callsites...so it's only done
//    with overloading in C++.  See %sys-protect.h for more information.
//
// 4. Note that series indexing in C is zero based.  So as far as Series is
//    concerned, `Series_Head(T, s)` is the same as `Series_At(T, s, 0)`
//
// 5. The clever c_cast() macro is used here to avoid writing overloads just
//    to get a const vs. non-const response.  But it only works to avoid the
//    overload if you can write it as a macro, and asserting on the series
//    would repeat the argument twice in a macro body (bad mojo!)
//

INLINE Byte* Series_Data(const_if_c Series* s) {  // assume valid [1]
    return Get_Series_Flag(s, DYNAMIC)  // inlined in Series_Data_At() [2]
        ? u_cast(Byte*, s->content.dynamic.data)
        : u_cast(Byte*, &s->content);
}

INLINE Byte* Series_Data_At(Byte w, const_if_c Series* s, REBLEN i) {
  #if !defined(NDEBUG)
    if (w != Series_Wide(s)) {  // will be "unusual" value if free
        if (Is_Node_Free(s))
            printf("Series_Data_At asked on freed series\n");
        else
            printf(
                "Series_Data_At asked %d on width=%d\n",
                w,
                cast(int, Series_Wide(s))
            );
        panic (s);
    }
  #endif

    assert(i <= Series_Used(s));

    return ((w) * (i)) + (  // v-- inlining of Series_Data() [2]
        Get_Series_Flag(s, DYNAMIC)
            ? cast(Byte*, s->content.dynamic.data)
            : cast(Byte*, &s->content)
        );
}

#if CPLUSPLUS_11  // give back const pointer on const series input [3]
    INLINE const Byte* Series_Data(const Series* s)
      { return Series_Data(m_cast(Series*, s)); }

    INLINE const Byte* Series_Data_At(
        Byte w,
        const Series* s,
        REBLEN i
    ){
        return Series_Data_At(w, m_cast(Series*, s), i);
    }
#endif

#define Series_At(T,s,i) \
    c_cast(T*, Series_Data_At(sizeof(T), (s), (i)))  // zero-based [4]

#if DEBUG
    #define Series_Head(T,s) \
        Series_At(T, (s), 0)  // Series_Data() doesn't check width, _At() does
#else
    #define Series_Head(T,s) \
        c_cast(T*, Series_Data(s))  // slightly faster, but no width check
#endif

#define Series_Data_Tail(w,s) \
    c_cast(Byte*, Series_Data_At((w), (s), Series_Used(s)))

#define Series_Tail(T,s) \
    c_cast(T*, Series_Data_Tail(sizeof(T), (s)))

INLINE Byte* Series_Data_Last(size_t wide, const_if_c Series* s) {
    assert(Series_Used(s) != 0);
    return Series_Data_At(wide, s, Series_Used(s) - 1);
}

#if CPLUSPLUS_11  // can't use c_cast() to inherit const, must overload [5]
    INLINE const Byte* Series_Data_Last(size_t wide, const Series* s) {
        assert(Series_Used(s) != 0);
        return Series_Data_At(wide, s, Series_Used(s) - 1);
    }
#endif

#define Series_Last(T,s) \
    c_cast(T*, Series_Data_Last(sizeof(T), (s)))


//=//// SERIES TERMINATION ////////////////////////////////////////////////=//
//
// R3-Alpha had a concept of termination which was that all series had one
// full-sized unit at their tail which was set to zero bytes.  Ren-C moves
// away from this concept...it only has terminating '\0' on UTF-8 strings,
// a reserved terminating *position* on binaries (in case they become
// aliased as UTF-8 strings), and the debug build terminates arrays in order
// to catch out-of-bounds accesses more easily:
//
// https://forum.rebol.info/t/1445
//
// Under this strategy, most of the termination is handled by the functions
// that deal with their specific subclass (e.g. Make_String()).  But some
// generic routines that memcpy() data behind the scenes needs to be sure it
// maintains the invariant that the higher level routines want.
//
// 1. A binary alias of a string must have all modifications keep it as valid
//    UTF-8, and it must maintain a `\0` terminator.  Because all binaries
//    are candidates for being aliased as strings, they reserve a byte at
//    their tail.  This debug setting helps ensure that binaries are setting
//    the '\0' tail intentionally when appropriate by poisoning the byte.
//
// 2. There's a difference with how byte buffers are handled vs. arrays, in
//    that arrays have to be expanded before they are written to, so that
//    the cells are formatted.  Byte strings don't have that requirement,
//    so the code isn't stylized to set the used size first and then put
//    data into the buffer.  So it wouldn't do any good to put a poison
//    byte at the head of a series allocation and expect to be able to see
//    it before the termination.  Review if callers can/should be changed.
//

#if DEBUG_POISON_SERIES_TAILS
    #define ONE_IF_POISON_TAILS 1

    #define BINARY_BAD_UTF8_TAIL_BYTE 0xFE  // binaries reserve tail byte [1]

    INLINE void Poison_Or_Unpoison_Tail_Debug(Series* s, bool poison) {
        if (Series_Wide(s) == 1) {  // presume BINARY! or ANY-STRING? (?)
            Byte* tail = Series_Tail(Byte, s);
            if (poison)
                *tail = BINARY_BAD_UTF8_TAIL_BYTE;
            else {
                /* assert(  // doesn't have an invariant [2]
                    *tail == BINARY_BAD_UTF8_TAIL_BYTE or *tail == '\0'
                ); */
            }
        }
        else if (Is_Series_Array(s) and Get_Series_Flag(s, DYNAMIC)) {
            Cell* tail = Series_At(Cell, s, s->content.dynamic.used);
            if (poison)
                Poison_Cell(tail);
            else {
                assert(Is_Cell_Poisoned(tail));
                Erase_Cell(tail);
            }
        }
    }

    #define POISON_SERIES_TAIL(s) \
        Poison_Or_Unpoison_Tail_Debug((s), true)

    #define UNPOISON_SERIES_TAIL(s) \
        Poison_Or_Unpoison_Tail_Debug((s), false)
#else
    #define ONE_IF_POISON_TAILS 0

    #define POISON_SERIES_TAIL(s) \
        NOOP

    #define UNPOISON_SERIES_TAIL(s) \
        NOOP
#endif

INLINE void Term_Series_If_Necessary(Series* s)
{
    if (Series_Wide(s) == 1) {
        if (Is_Series_UTF8(s))
            *Series_Tail(Byte, s) = '\0';
        else {
          #if DEBUG_POISON_SERIES_TAILS
            *Series_Tail(Byte, s) = BINARY_BAD_UTF8_TAIL_BYTE;
          #endif
        }
    }
    else if (Get_Series_Flag(s, DYNAMIC) and Is_Series_Array(s)) {
      #if DEBUG_POISON_SERIES_TAILS
        Poison_Cell(Series_Tail(Cell, s));
      #endif
    }
}

#ifdef NDEBUG
    #define Assert_Series_Term_If_Needed(s) \
        NOOP
#else
    #define Assert_Series_Term_If_Needed(s) \
        Assert_Series_Term_Core(s);
#endif

#define Note_Series_Maybe_Term(s) NOOP  // use to annotate if may-or-may-not be


//=//// SETTING SERIES LENGTH/SIZE ////////////////////////////////////////=//
//
// 1. Right now SERIES_FLAG_FIXED_SIZE merely means they can't expand, but
//    they set the flag before initializing things like termination and the
//    length.  If this routine were to disallow it, then the flag wouldn't
//    be passed into series creation but could only be added afterward.
//
// 2. UTF-8 strings maintain a length in codepoints (in misc.length), as well
//    as the size in bytes (as "used").  It's expected that both will be
//    updated together--see Term_String_Len_Size().  But sometimes the used
//    field is updated solo by a binary-based routine in an intermediate step.
//    That's okay so long as the length is not consulted before the string
//    handling code finalizes it.  DEBUG_UTF8_EVERYWHERE makes violations
//    obvious by corrupting the length.

INLINE void Set_Series_Used_Internal(Series* s, Count used) {
    /* assert(Not_Series_Flag(s, FIXED_SIZE)); */  // [1]
    if (Get_Series_Flag(s, DYNAMIC))
        s->content.dynamic.used = used;
    else {
        assert(used < sizeof(s->content));

        if (Is_Series_Array(s)) {  // content used by cell, no room for length
            if (used == 0)
                Poison_Cell(&s->content.fixed.cell);  // poison means 0 used
            else {
                assert(used == 1);  // any non-poison will mean length 1
                if (not Is_Cell_Poisoned(&s->content.fixed.cell)) {
                    // it was already length 1, leave the cell alone
                } else
                    Erase_Cell(&s->content.fixed.cell);
            }
        }
        else
            USED_BYTE(s) = used;
    }

  #if DEBUG_UTF8_EVERYWHERE
    if (Is_String_NonSymbol(s)) {
        Corrupt_If_Debug(s->misc.length);  // catch violators [2]
        Touch_Stub_If_Debug(s);
    }
  #endif
}

INLINE void Set_Series_Used(Series* s, Count used) {
    UNPOISON_SERIES_TAIL(s);
    Set_Series_Used_Internal(s, used);
    POISON_SERIES_TAIL(s);
}

INLINE void Set_Series_Len(Series* s, Length len) {
    assert(not Is_Series_UTF8(s));  // use _Len_Size() instead [2]
    Set_Series_Used(s, len);
}

#if CPLUSPLUS_11  // catch cases when calling on String* directly
    INLINE void Set_Series_Len(String* s, Length len) = delete;
#endif

// Optimized expand when at tail (but, does not reterminate)
//
INLINE void Expand_Series_Tail(Series* s, REBLEN delta) {
    if (Series_Fits(s, delta))
        Set_Series_Used(s, Series_Used(s) + delta);  // no termination implied
    else
        Expand_Series(s, Series_Used(s), delta);  // currently terminates
}



// Out of the 8 platform pointers that comprise a series node, only 3 actually
// need to be initialized to get a functional non-dynamic series or array of
// length 0!  Only one is set here.  The info should be set by the caller.
//
INLINE Stub* Prep_Stub(void *preallocated, Flags flags) {
    assert(not (flags & NODE_FLAG_CELL));

    Stub *s = u_cast(Stub*, preallocated);

    s->leader.bits = NODE_FLAG_NODE | flags;  // #1

  #if !defined(NDEBUG)
    SafeCorrupt_Pointer_Debug(s->link.corrupt);  // #2
    Mem_Fill(&s->content.fixed, 0xBD, sizeof(s->content));  // #3 - #6
    SafeCorrupt_Pointer_Debug(s->info.corrupt);  // #7
    SafeCorrupt_Pointer_Debug(s->link.corrupt);  // #8

  #if DEBUG_SERIES_ORIGINS
    s->guard = nullptr;  // so Touch_Stub_Debug() can tell data is invalid
  #endif

    Touch_Stub_If_Debug(s);  // tag current C stack as series origin in ASAN
  #endif

  #if DEBUG_COLLECT_STATS
    g_mem.series_made += 1;
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


// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
//
// 1. It is more efficient if you know a series is going to become managed to
//   create it in the managed state.  But be sure no evaluations are called
//   before it's made reachable by the GC, or use Push_GC_Guard().
//
INLINE Series* Make_Series_Into(
    void* preallocated,
    REBLEN capacity,
    Flags flags
){
    size_t wide = Wide_For_Flavor(Flavor_From_Flags(flags));
    if (cast(REBU64, capacity) * wide > INT32_MAX)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

    Stub* s = Prep_Stub(preallocated, flags);

  #if defined(NDEBUG)
    SERIES_INFO(s) = SERIES_INFO_MASK_NONE;
  #else
    if (flags & SERIES_FLAG_INFO_NODE_NEEDS_MARK)
        Corrupt_Pointer_If_Debug(s->info.node);
    else
        SERIES_INFO(s) = SERIES_INFO_MASK_NONE;
  #endif

    if (
        (flags & SERIES_FLAG_DYNAMIC)  // inlining will constant fold
        or (capacity * wide > sizeof(s->content))  // data won't fit in stub
    ){
        Set_Series_Flag(s, DYNAMIC);

        if (not Did_Series_Data_Alloc(s, capacity)) {
            Clear_Node_Managed_Bit(s);
            Set_Series_Inaccessible(s);
            GC_Kill_Stub(s);

            fail (Error_No_Memory(capacity * wide));
        }

      #if DEBUG_COLLECT_STATS
        g_mem.series_memory += capacity * wide;
      #endif
    }

    if (not (flags & NODE_FLAG_MANAGED)) {  // more efficient if managed [1]
        if (Is_Series_Full(g_gc.manuals))
            Extend_Series_If_Necessary(g_gc.manuals, 8);

        cast(Series**, g_gc.manuals->content.dynamic.data)[
            g_gc.manuals->content.dynamic.used++
        ] = s;  // will need to find/remove from this list later
    }

    return s;
}

#define Make_Series_Core(capacity,flags) \
    Make_Series_Into(Alloc_Pooled(STUB_POOL), (capacity), (flags))

#define Make_Series(T,capacity,flags) \
    cast(T*, Make_Series_Core((capacity), (flags)))


//=//// DEBUG SERIES MONITORING ////////////////////////////////////////////=//
//
// This once used a series flag in debug builds to tell whether a series was
// monitored or not.  But series flags are scarce, so the feature was scaled
// back to just monitoring a single node.  It could also track a list--but the
// point is just that stealing a flag is wasteful.
//
#if DEBUG_MONITOR_SERIES
    INLINE void Debug_Monitor_Series(void *p) {
        printf("Adding monitor to %p on tick #%d\n", p, cast(int, TG_tick));
        fflush(stdout);
        g_mem.monitor_node = cast(Series*, p);
    }
#endif
