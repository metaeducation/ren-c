//
//  File: %sys-series.h
//  Summary: {any-series! defs AFTER %tmp-internals.h (see: %sys-rebser.h)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012-2021 Ren-C Open Source Contributors
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
// 1. The internal system datatype, also known as a REBSER.  It's a low-level
//    implementation of something similar to a vector or an array in other
//    languages.  It is an abstraction which represents a contiguous region
//    of memory containing equally-sized elements.
//
//   (For the struct definition of REBSER, see %sys-rebser.h)
//
// 2. The user-level value type ANY-SERIES!.  This might be more accurately
//    called ITERATOR!, because it includes both a pointer to a REBSER of
//    data and an index offset into that data.  Attempts to reconcile all
//    the naming issues from historical Rebol have not yielded a satisfying
//    alternative, so the ambiguity has stuck.
//
// An ANY-SERIES! value contains an `index` as the 0-based position into the
// series represented by this ANY-VALUE! (so if it is 0 then that means a
// Rebol index of 1).
//
// It is possible that the index could be to a point beyond the range of the
// series.  This is intrinsic, because the REBSER can be modified through
// other values and not update the others referring to it.  Hence VAL_INDEX()
// must be checked, or the routine called with it must.
//
//=//// NOTES //////////////////////////////////////////////////////////////=//
//
// * Series subclasses Array, Context, REBACT, REBMAP are defined which are
//   explained where they are defined in separate header files.
//
// * It is desirable to have series subclasses be different types, even though
//   there are some common routines for processing them.  e.g. not every
//   function that would take a REBSER* would actually be handled in the same
//   way for a Array(*).  Plus, just because a Context(*) is implemented as a
//   Array(*) with a link to another Array(*) doesn't mean most clients should
//   be accessing the array--in a C++ build this would mean it would have some
//   kind of protected inheritance scheme.
//
// * !!! It doesn't seem like index-out-of-range checks on the cells are being
//   done in a systemic way.  VAL_LEN_AT() bounds the length at the index
//   position by the physical length, but VAL_ARRAY_AT() doesn't check.
//


//=//// SERIES "FLAG" BITS /////////////////////////////////////////////////=//
//
// See definitions of SERIES_FLAG_XXX.
//
// Using token pasting macros helps avoid mixups with SERIES_INFO_XXX!
//

#define Set_Series_Flag(s,name) \
    ((s)->leader.bits |= SERIES_FLAG_##name)

#define Get_Series_Flag(s,name) \
    (((s)->leader.bits & SERIES_FLAG_##name) != 0)

#define Clear_Series_Flag(s,name) \
    ((s)->leader.bits &= ~SERIES_FLAG_##name)

#define Not_Series_Flag(s,name) \
    (((s)->leader.bits & SERIES_FLAG_##name) == 0)


//=//// SERIES SUBCLASS FLAGS //////////////////////////////////////////////=//
//
// In the debug build, ensure_flavor() checks if a series node matches the
// expected FLAVOR_XXX, and panics if it does not.  This is used by the
// subclass testing macros as a check that you are testing the flag for the
// flavor that you expect.
//

#if defined(NDEBUG)
    #define ensure_flavor(flavor,s) (s)  // no-op in release build
#else
    inline static REBSER *ensure_flavor(Flavor flavor, const_if_c REBSER *s) {
        if (SER_FLAVOR(s) != flavor) {
            Flavor actual_flavor = SER_FLAVOR(s);
            USED(actual_flavor);
            panic (s);
        }
        return m_cast(REBSER*, s);
    }

    #if CPLUSPLUS_11
        inline static const REBSER *ensure_flavor(
            Flavor flavor,
            const REBSER *s
        ){
            if (SER_FLAVOR(s) != flavor) {
                Flavor actual_flavor = SER_FLAVOR(s);
                USED(actual_flavor);
                panic (s);
            }
            return s;
        }
    #endif
#endif

#define Get_Subclass_Flag(subclass,s,name) \
    ((ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        & subclass##_FLAG_##name) != 0)

#define Not_Subclass_Flag(subclass,s,name) \
    ((ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        & subclass##_FLAG_##name) == 0)

#define Set_Subclass_Flag(subclass,s,name) \
    (ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        |= subclass##_FLAG_##name)

#define Clear_Subclass_Flag(subclass,s,name) \
    (ensure_flavor(FLAVOR_##subclass, (s))->leader.bits \
        &= ~subclass##_FLAG_##name)


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
//      REBBMK *bookmarks = string.link.bookmarks;
//      string.link.bookmarks = bookmarks;
//
//      Symbol(const*) synonym = symbol.link.synonym;
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
//      REBBMK *bookmarks = LINK(Bookmarks, string);  // actually reads `node`
//      mutable_LINK(Bookmarks, string) = bookmarks;
//
//      Symbol(const*) synonym = LINK(Synonym, symbol);  // also reads `node`
//      mutable_LINK(Synonym, symbol) = synonym;
//
// The syntax is *almost* as readable, but throws in benefits of offering some
// helpful debug runtime checks that you're accessing what the series holds.
// It has yet another advantage because it allows new "members" to be "added"
// by extension code that wouldn't be able to edit a union in a core header.
//
// To use the LINK() and MISC(), you must define three macros, like this:
//
//      #define LINK_Bookmarks_TYPE     REBBMK*
//      #define LINK_Bookmarks_CAST     (REBBMK*)SER
//      #define HAS_LINK_Bookmarks      FLAVOR_STRING
//
// You get the desired properties of being easy to find cases of a particular
// interpretation of the field, along with type checking on the assignment,
// and a cast operation that does potentially heavy debug checks on the
// extraction.
//
// (See DEBUG_CHECK_CASTS for the C++ versions of SER(), ARR(), CTX()...)
//
// Note: C casts are used here to gloss the `const` status of the node.  The
// caller is responsible for storing reads in the right constness for what
// they know to be stored in the node.
//

#define LINK(Field, s) \
    LINK_##Field##_CAST(m_cast(Node*, \
        ensure_flavor(HAS_LINK_##Field, (s))->link.any.node))

#define MISC(Field, s) \
    MISC_##Field##_CAST(m_cast(Node*, \
        ensure_flavor(HAS_MISC_##Field, (s))->misc.any.node))

#define mutable_LINK(Field, s) \
    ensured(LINK_##Field##_TYPE, const Node*, \
        ensure_flavor(HAS_LINK_##Field, (s))->link.any.node)

#define mutable_MISC(Field, s) \
    ensured(MISC_##Field##_TYPE, const Node*, \
        ensure_flavor(HAS_MISC_##Field, (s))->misc.any.node)

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

#if (! CPLUSPLUS_11)
    #define SER_INFO(s) \
        (s)->info.flags.bits

    // !!! A checking SER_INODE() is overkill, given that the INODE() accessors
    // check the flavor.  Assume flavor has INFO_NODE_NEEDS_MARK right.
#else
    inline static const uintptr_t &SER_INFO(const REBSER *s) {
        assert(Not_Series_Flag(s, INFO_NODE_NEEDS_MARK));
        return s->info.flags.bits;
    }

    inline static uintptr_t &SER_INFO(REBSER *s) {
        assert(Not_Series_Flag(s, INFO_NODE_NEEDS_MARK));
        return s->info.flags.bits;
    }
#endif

#define SET_SERIES_INFO(s,name) \
    (SER_INFO(s) |= SERIES_INFO_##name)

#define GET_SERIES_INFO(s,name) \
    ((SER_INFO(s) & SERIES_INFO_##name) != 0)

#define CLEAR_SERIES_INFO(s,name) \
    (SER_INFO(s) &= ~SERIES_INFO_##name)

#define NOT_SERIES_INFO(s,name) \
    ((SER_INFO(s) & SERIES_INFO_##name) == 0)

#define INODE(Field, s) \
    INODE_##Field##_CAST(m_cast(Node*, \
        ensure_flavor(HAS_INODE_##Field, (s))->info.node))

#define mutable_INODE(Field, s) \
    ensured(INODE_##Field##_TYPE, const Node*, \
        ensure_flavor(HAS_INODE_##Field, (s))->info.node)

#define node_INODE(Field, s) \
    *m_cast(Node**, &(s)->info.node)  // const ok for strict alias


//=//// SERIES CAPACITY AND TOTAL SIZE /////////////////////////////////////=//
//
// See documentation of `bias` and `rest` in %sys-rebser.h
//

inline static bool IS_SER_BIASED(const REBSER *s) {
    assert(Get_Series_Flag(s, DYNAMIC));
    return not IS_VARLIST(s);
}

inline static REBLEN SER_BIAS(const REBSER *s) {
    if (not IS_SER_BIASED(s))
        return 0;
    return cast(REBLEN, ((s)->content.dynamic.bonus.bias >> 16) & 0xffff);
}

#define MAX_SERIES_BIAS 0x1000

inline static void SER_SET_BIAS(REBSER *s, REBLEN bias) {
    assert(IS_SER_BIASED(s));
    s->content.dynamic.bonus.bias =
        (s->content.dynamic.bonus.bias & 0xffff) | (bias << 16);
}

inline static void SER_ADD_BIAS(REBSER *s, REBLEN b) {
    assert(IS_SER_BIASED(s));
    s->content.dynamic.bonus.bias += b << 16;
}

inline static void SER_SUB_BIAS(REBSER *s, REBLEN b) {
    assert(IS_SER_BIASED(s));
    s->content.dynamic.bonus.bias -= b << 16;
}

inline static Length SER_REST(const REBSER *s) {
    if (Get_Series_Flag(s, DYNAMIC))
        return s->content.dynamic.rest;

    if (IS_SER_ARRAY(s))
        return 2; // includes info bits acting as trick "terminator"

    assert(sizeof(s->content) % SER_WIDE(s) == 0);
    return sizeof(s->content) / SER_WIDE(s);
}

inline static size_t SER_TOTAL(const REBSER *s) {
    return (SER_REST(s) + SER_BIAS(s)) * SER_WIDE(s);
}

inline static size_t SER_TOTAL_IF_DYNAMIC(const REBSER *s) {
    if (Not_Series_Flag(s, DYNAMIC))
        return 0;
    return SER_TOTAL(s);
}


//=//// SERIES "BONUS" /////////////////////////////////////////////////////=//
//
// If a dynamic series isn't modified in ways that can leave extra capacity at
// the head, it might want to use the bias slot for something else.  This usage
// is called the "bonus".
//

#if (! CPLUSPLUS_11)
    #define SER_BONUS(s) \
        (s)->content.dynamic.bonus.node
#else
    inline static const struct Raw_Node * const &SER_BONUS(const REBSER *s) {
        assert(s->leader.bits & SERIES_FLAG_DYNAMIC);
        return s->content.dynamic.bonus.node;
    }
    inline static const struct Raw_Node * &SER_BONUS(REBSER *s) {
        assert(s->leader.bits & SERIES_FLAG_DYNAMIC);
        return s->content.dynamic.bonus.node;
    }
#endif

#define BONUS(Field, s) \
    BONUS_##Field##_CAST(m_cast(Node*, \
        SER_BONUS(ensure_flavor(HAS_BONUS_##Field, (s)))))

#define mutable_BONUS(Field, s) \
    ensured(BONUS_##Field##_TYPE, const Node*, \
        SER_BONUS(ensure_flavor(HAS_BONUS_##Field, (s))))

#define node_BONUS(Field, s) \
    *m_cast(Node**, &SER_BONUS(s))  // const ok for strict alias


//=//// SERIES "TOUCH" FOR DEBUGGING ///////////////////////////////////////=//
//
// For debugging purposes, it's nice to be able to crash on some kind of guard
// for tracking the call stack at the point of allocation if we find some
// undesirable condition that we want a trace from.  Generally, series get
// set with this guard at allocation time.  But if you want to mark a moment
// later, you can.
//
// This works with Address Sanitizer or with Valgrind, but the config flag to
// enable it only comes automatically with address sanitizer.
//
#if DEBUG_SERIES_ORIGINS || DEBUG_COUNT_TICKS
    inline static void Touch_Stub_Debug(void *p) {
        Stub *s = cast(Stub*, p);  // Array*, Context*, Action*...

        // NOTE: When series are allocated, the only thing valid here is the
        // header.  Hence you can't tell (for instance) if it's an array or
        // not, as that's in the info.

      #if DEBUG_SERIES_ORIGINS
        s->guard = cast(intptr_t*, malloc(sizeof(*s->guard)));
        free(s->guard);
      #endif

      #if DEBUG_COUNT_TICKS
        s->tick = TG_tick;
      #else
        s->tick = 0;
      #endif
    }

    #define TOUCH_STUB_IF_DEBUG(s) \
        Touch_Stub_Debug(s)
#else
    #define TOUCH_STUB_IF_DEBUG(s) \
        NOOP
#endif


//=//// DEBUG SERIES MONITORING ////////////////////////////////////////////=//
//
// This once used a series flag in debug builds to tell whether a series was
// monitored or not.  But series flags are scarce, so the feature was scaled
// back to just monitoring a single node.  It could also track a list--but the
// point is just that stealing a flag isn't viable.
//

#if DEBUG_MONITOR_SERIES
    inline static void MONITOR_SERIES(void *p) {
        printf("Adding monitor to %p on tick #%d\n", p, cast(int, TG_tick));
        fflush(stdout);
        PG_Monitor_Node_Debug = SER(cast(Node*, p));
    }
#endif


//
// The mechanics of the macros that get or set the length of a series are a
// little bit complicated.  This is due to the optimization that allows data
// which is sizeof(REBVAL) or smaller to fit directly inside the series node.
//
// If a series is not "dynamic" (e.g. has a full pooled allocation) then its
// length is stored in the header.  But if a series is dynamically allocated
// out of the memory pools, then without the data itself taking up the
// "content", there's room for a length in the node.
//

inline static Length SER_USED(const REBSER *s) {
    if (Get_Series_Flag(s, DYNAMIC))
        return s->content.dynamic.used;
    if (IS_SER_ARRAY(s)) {
        //
        // We report the array length as being 0 if it's the distinguished
        // case of a poisoned cell (added benefit: catches stray writes)
        //
        if (Is_Cell_Poisoned(SER_CELL(s)))
            return 0;
        return 1;  // Note: might be a plain void cell
    }
    return USED_BYTE(s);
}


// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
inline static Byte* SER_DATA(const_if_c REBSER *s) {
    // if updating, also update manual inlining in SER_AT_RAW

    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(Not_Series_Flag(s, INACCESSIBLE));

    return Get_Series_Flag(s, DYNAMIC)
        ? cast(Byte*, s->content.dynamic.data)
        : cast(Byte*, &s->content);
}

inline static Byte* SER_DATA_AT(Byte w, const_if_c REBSER *s, REBLEN i) {
  #if !defined(NDEBUG)
    if (w != SER_WIDE(s)) {  // will be "unusual" value if free
        if (IS_FREE_NODE(s))
            printf("SER_DATA_AT asked on freed series\n");
        else
            printf(
                "SER_DATA_AT asked %d on width=%d\n",
                w,
                cast(int, SER_WIDE(s))
            );
        panic (s);
    }
  #endif

    // The VAL_CONTEXT(), VAL_SERIES(), VAL_ARRAY() extractors do the failing
    // upon extraction--that's meant to catch it before it gets this far.
    //
    assert(Not_Series_Flag(s, INACCESSIBLE));

    assert(i <= SER_USED(s));

    return ((w) * (i)) + ( // v-- inlining of SER_DATA
        Get_Series_Flag(s, DYNAMIC)
            ? cast(Byte*, s->content.dynamic.data)
            : cast(Byte*, &s->content)
        );
}

#if CPLUSPLUS_11
    inline static const Byte* SER_DATA(const REBSER *s)  // "SER_DATA_HEAD"
      { return SER_DATA(m_cast(REBSER*, s)); }

    inline static const Byte* SER_DATA_AT(
        Byte w,
        const REBSER *s,
        REBLEN i
    ){
        return SER_DATA_AT(w, m_cast(REBSER*, s), i);
    }
#endif


// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `SER_HEAD(t, s)` is the same as `SER_AT(t, s, 0)`

#define SER_AT(t,s,i) \
    cast(t*, SER_DATA_AT(sizeof(t), (s), (i)))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)  // using SER_DATA_AT() vs. just SER_DATA() checks width


// If a binary series is a string (or aliased as a string), it must have all
// modifications keep it with valid UTF-8 content.  That includes having a
// terminal `\0` byte.  Since there is a special code path for setting the
// length in the case of aliased binaries, that's what enforces the 0 byte
// rule...but if a binary is never aliased as a string it may not be
// terminated.  It's always long enough to carry a terminator...and the
// debug build sets binary-sized series tails to this byte to make sure that
// they are formally terminated if they need to be.
//
#if DEBUG_POISON_SERIES_TAILS
    #define BINARY_BAD_UTF8_TAIL_BYTE 0xFE
#endif


inline static Byte* SER_DATA_TAIL(size_t w, const_if_c REBSER *s)
  { return SER_DATA_AT(w, s, SER_USED(s)); }

#if CPLUSPLUS_11
    inline static const Byte* SER_DATA_TAIL(size_t w, const REBSER *s)
      { return SER_DATA_AT(w, s, SER_USED(s)); }
#endif

#define SER_TAIL(t,s) \
    cast(t*, SER_DATA_TAIL(sizeof(t), (s)))

inline static Byte* SER_DATA_LAST(size_t wide, const_if_c REBSER *s) {
    assert(SER_USED(s) != 0);
    return SER_DATA_AT(wide, s, SER_USED(s) - 1);
}

#if CPLUSPLUS_11
    inline static const Byte* SER_DATA_LAST(size_t wide, const REBSER *s) {
        assert(SER_USED(s) != 0);
        return SER_DATA_AT(wide, s, SER_USED(s) - 1);
    }
#endif

#define SER_LAST(t,s) \
    cast(t*, SER_DATA_LAST(sizeof(t), (s)))


#define SER_FULL(s) \
    (SER_USED(s) + 1 >= SER_REST(s))

#define SER_AVAIL(s) \
    (SER_REST(s) - (SER_USED(s) + 1)) // space available (minus terminator)

#define SER_FITS(s,n) \
    ((SER_USED(s) + (n) + 1) <= SER_REST(s))


#if DEBUG_POISON_SERIES_TAILS
    inline static void Poison_Or_Unpoison_Tail_Debug(REBSER *s, bool poison) {
        if (SER_WIDE(s) == 1) {  // presume BINARY! or ANY-STRING! (?)
            Byte* tail = SER_TAIL(Byte, s);
            if (poison)
                *tail = BINARY_BAD_UTF8_TAIL_BYTE;
            else {
                /* Doesn't seem there's any invariant here--improve over time.
                assert(*tail == BINARY_BAD_UTF8_TAIL_BYTE or *tail == '\0');
                */
            }
        }
        else if (IS_SER_ARRAY(s) and Get_Series_Flag(s, DYNAMIC)) {
            Reb_Cell* tail = SER_AT(Reb_Cell, s, s->content.dynamic.used);
            if (poison)
                Poison_Cell(tail);
            else {
                assert(Is_Cell_Poisoned(tail));
                Erase_Cell(tail);
            }
        }
    }

    #define POISON_SERIES_TAIL(s)   Poison_Or_Unpoison_Tail_Debug((s), true)
    #define UNPOISON_SERIES_TAIL(s) Poison_Or_Unpoison_Tail_Debug((s), false)
#else
    #define POISON_SERIES_TAIL(s) NOOP
    #define UNPOISON_SERIES_TAIL(s) NOOP
#endif

// !!! Review if SERIES_FLAG_FIXED_SIZE should be calling this routine.  At
// the moment, fixed size series merely can't expand, but it might be more
// efficient if they didn't use any "appending" operators to get built.
//
inline static void Set_Series_Used_Internal(REBSER *s, REBLEN used) {
    if (Get_Series_Flag(s, DYNAMIC))
        s->content.dynamic.used = used;
    else {
        assert(used < sizeof(s->content));

        if (IS_SER_ARRAY(s)) {  // content taken up by cell, no room for length
            if (used == 0)
                Poison_Cell(mutable_SER_CELL(s));  // poison cell means 0 used
            else {
                assert(used == 1);  // any non-poison will mean length 1
                if (not Is_Cell_Poisoned(SER_CELL(s))) {
                    // it was already length 1, leave the cell alone
                } else
                    Erase_Cell(mutable_SER_CELL(s));
            }
        }
        else
            mutable_USED_BYTE(s) = used;
    }

  #if DEBUG_UTF8_EVERYWHERE
    //
    // Low-level series mechanics will manipulate the used field, but that's
    // at the byte level.  The higher level string mechanics must be used on
    // strings.
    //
    if (IS_NONSYMBOL_STRING(s)) {
        s->misc.length = 0xDECAFBAD;
        TOUCH_STUB_IF_DEBUG(s);
    }
  #endif
}

inline static void SET_SERIES_USED(REBSER *s, REBLEN used) {
    UNPOISON_SERIES_TAIL(s);
    Set_Series_Used_Internal(s, used);
    POISON_SERIES_TAIL(s);
}

// See TERM_STRING_LEN_SIZE() for the code that maintains string invariants,
// including the '\0' termination (this routine will corrupt the tail byte
// in the debug build to catch violators.)
//
inline static void SET_SERIES_LEN(REBSER *s, REBLEN len) {
    assert(not IS_SER_UTF8(s));  // use _LEN_SIZE
    SET_SERIES_USED(s, len);
}

#if CPLUSPLUS_11  // catch cases when calling on String(*) directly
    inline static void SET_SERIES_LEN(String(*) s, REBLEN len) = delete;
#endif


//
// Optimized expand when at tail (but, does not reterminate)
//

inline static void EXPAND_SERIES_TAIL(REBSER *s, REBLEN delta) {
    if (SER_FITS(s, delta))
        SET_SERIES_USED(s, SER_USED(s) + delta);  // no termination implied
    else
        Expand_Series(s, SER_USED(s), delta);  // currently terminates
}


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

inline static void TERM_SERIES_IF_NECESSARY(REBSER *s)
{
    if (SER_WIDE(s) == 1) {
        if (IS_SER_UTF8(s))
            *SER_TAIL(Byte, s) = '\0';
        else {
          #if DEBUG_POISON_SERIES_TAILS
            *SER_TAIL(Byte, s) = BINARY_BAD_UTF8_TAIL_BYTE;
          #endif
        }
    }
    else if (Get_Series_Flag(s, DYNAMIC) and IS_SER_ARRAY(s)) {
      #if DEBUG_POISON_SERIES_TAILS
        Poison_Cell(SER_TAIL(Reb_Cell, s));
      #endif
    }
}

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM_IF_NEEDED(s) \
        NOOP
#else
    #define ASSERT_SERIES_TERM_IF_NEEDED(s) \
        Assert_Series_Term_Core(s);
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define NOTE_SERIES_MAYBE_TERM(s) NOOP


//=//// SERIES MANAGED MEMORY /////////////////////////////////////////////=//
//
// If NODE_FLAG_MANAGED is not explicitly passed to Make_Series(), a
// series will be manually memory-managed by default.  Hence you don't need
// to worry about the series being freed out from under you while building it.
// Manual series are tracked, and automatically freed in the case of a fail().
//
// All manual series *must* either be freed with Free_Unmanaged_Series() or
// delegated to the GC with Manage_Series() before the level ends.  Once a
// series is managed, only the GC is allowed to free it.
//
// Manage_Series() is shallow--it only sets a bit on that *one* series, not
// any series referenced by values inside of it.  Hence many routines that
// build hierarchical structures (like the scanner) only return managed
// results, since they can manage it as they build them.

inline static void Untrack_Manual_Series(REBSER *s)
{
    REBSER ** const last_ptr
        = &cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.used - 1
        ];

    assert(GC_Manuals->content.dynamic.used >= 1);
    if (*last_ptr != s) {
        //
        // If the series is not the last manually added series, then
        // find where it is, then move the last manually added series
        // to that position to preserve it when we chop off the tail
        // (instead of keeping the series we want to free).
        //
        REBSER **current_ptr = last_ptr - 1;
        for (; *current_ptr != s; --current_ptr) {
          #if !defined(NDEBUG)
            if (
                current_ptr
                <= cast(REBSER**, GC_Manuals->content.dynamic.data)
            ){
                printf("Series not in list of last manually added series\n");
                panic(s);
            }
          #endif
        }
        *current_ptr = *last_ptr;
    }

    // !!! Should GC_Manuals ever shrink or save memory?
    //
    --GC_Manuals->content.dynamic.used;
}

inline static REBSER *Manage_Series(REBSER *s)  // give manual series to GC
{
  #if !defined(NDEBUG)
    if (Get_Series_Flag(s, MANAGED))
        panic (s);  // shouldn't manage an already managed series
  #endif

    s->leader.bits |= NODE_FLAG_MANAGED;
    Untrack_Manual_Series(s);
    return s;
}

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s) NOOP
#else
    inline static void ASSERT_SERIES_MANAGED(const REBSER *s) {
        if (Not_Series_Flag(s, MANAGED))
            panic (s);
    }
#endif

inline static REBSER *Force_Series_Managed(const_if_c REBSER *s) {
    if (Not_Series_Flag(s, MANAGED))
        Manage_Series(m_cast(REBSER*, s));
    return m_cast(REBSER*, s);
}

#if (! CPLUSPLUS_11)
    #define Force_Series_Managed_Core Force_Series_Managed
#else
    inline static REBSER *Force_Series_Managed_Core(REBSER *s)
      { return Force_Series_Managed(s); }  // mutable series may be unmanaged

    inline static REBSER *Force_Series_Managed_Core(const REBSER *s) {
        ASSERT_SERIES_MANAGED(s);  // const series should already be managed
        return m_cast(REBSER*, s);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES COLORING API
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha re-used the same marking flag from the GC in order to do various
// other bit-twiddling tasks when the GC wasn't running.  This is an
// unusually dangerous thing to be doing...because leaving a stray mark on
// during some other traversal could lead the GC to think it had marked
// things reachable from that series when it had not--thus freeing something
// that was still in use.
//
// While leaving a stray mark on is a bug either way, GC bugs are particularly
// hard to track down.  So one doesn't want to risk them if not absolutely
// necessary.  Not to mention that sharing state with the GC that you can
// only use when it's not running gets in the way of things like background
// garbage collection, etc.
//
// Ren-C keeps the term "mark" for the GC, since that's standard nomenclature.
// A lot of basic words are taken other places for other things (tags, flags)
// so this just goes with a series "color" of black or white, with white as
// the default.  The debug build keeps a count of how many black series there
// are and asserts it's 0 by the time each evaluation ends, to ensure balance.
//

inline static bool Is_Series_Black(const REBSER *s) {
    return Get_Series_Flag(s, BLACK);
}

inline static bool Is_Series_White(const REBSER *s) {
    return Not_Series_Flag(s, BLACK);
}

inline static void Flip_Series_To_Black(const REBSER *s) {
    assert(Not_Series_Flag(s, BLACK));
    Set_Series_Flag(m_cast(REBSER*, s), BLACK);
  #if !defined(NDEBUG)
    ++TG_Num_Black_Series;
  #endif
}

inline static void Flip_Series_To_White(const REBSER *s) {
    assert(Get_Series_Flag(s, BLACK));
    Clear_Series_Flag(m_cast(REBSER*, s), BLACK);
  #if !defined(NDEBUG)
    --TG_Num_Black_Series;
  #endif
}


//
// Freezing and Locking
//

inline static void Freeze_Series(const REBSER *s) {  // there is no unfreeze
    assert(not IS_SER_ARRAY(s)); // use Deep_Freeze_Array

    // Mutable cast is all right for this bit.  We set the FROZEN_DEEP flag
    // even though there is no structural depth here, so that the generic
    // test for deep-frozenness can be faster.
    //
    SET_SERIES_INFO(m_cast(REBSER*, s), FROZEN_SHALLOW);
    SET_SERIES_INFO(m_cast(REBSER*, s), FROZEN_DEEP);
}

inline static bool Is_Series_Frozen(const REBSER *s) {
    assert(not IS_SER_ARRAY(s));  // use Is_Array_Deeply_Frozen
    if (NOT_SERIES_INFO(s, FROZEN_SHALLOW))
        return false;
    assert(GET_SERIES_INFO(s, FROZEN_DEEP));  // true on frozen non-arrays
    return true;
}

inline static bool Is_Series_Read_Only(const REBSER *s) {  // may be temporary
    return 0 != (SER_INFO(s) &
        (SERIES_INFO_HOLD | SERIES_INFO_PROTECTED
        | SERIES_INFO_FROZEN_SHALLOW | SERIES_INFO_FROZEN_DEEP)
    );
}


// Gives the appropriate kind of error message for the reason the series is
// read only (frozen, running, protected, locked to be a map key...)
//
// !!! Should probably report if more than one form of locking is in effect,
// but if only one error is to be reported then this is probably the right
// priority ordering.
//

inline static void FAIL_IF_READ_ONLY_SER(REBSER *s) {
    if (not Is_Series_Read_Only(s))
        return;

    if (GET_SERIES_INFO(s, AUTO_LOCKED))
        fail (Error_Series_Auto_Locked_Raw());

    if (GET_SERIES_INFO(s, HOLD))
        fail (Error_Series_Held_Raw());

    if (GET_SERIES_INFO(s, FROZEN_SHALLOW))
        fail (Error_Series_Frozen_Raw());

    assert(NOT_SERIES_INFO(s, FROZEN_DEEP));  // implies FROZEN_SHALLOW

    assert(GET_SERIES_INFO(s, PROTECTED));
    fail (Error_Series_Protected_Raw());
}


#if defined(NDEBUG)
    #define KNOWN_MUTABLE(v) v
#else
    inline static Cell(const*) KNOWN_MUTABLE(Cell(const*) v) {
        assert(Get_Cell_Flag(v, FIRST_IS_NODE));
        REBSER *s = SER(VAL_NODE1(v));  // can be pairlist, varlist, etc.
        assert(not Is_Series_Read_Only(s));
        assert(Not_Cell_Flag(v, CONST));
        return v;
    }
#endif

// Forward declaration needed
inline static REBVAL* Unrelativize(Cell(*) out, Cell(const*) v);

inline static Cell(const*) ENSURE_MUTABLE(Cell(const*) v) {
    assert(Get_Cell_Flag(v, FIRST_IS_NODE));
    REBSER *s = SER(VAL_NODE1(v));  // can be pairlist, varlist, etc.

    FAIL_IF_READ_ONLY_SER(s);

    if (Not_Cell_Flag(v, CONST))
        return v;

    DECLARE_LOCAL (specific);
    Unrelativize(specific, v);  // relative values lose binding in error object
    fail (Error_Const_Value_Raw(specific));
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING SERIES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the evaluator runs (and also when
// ports are used).  So if a series has had Manage_Series() run on it, the
// potential exists that any C pointers that are outstanding may "go bad"
// if the series wasn't reachable from the root set.  This is important to
// remember any time a pointer is held across a call that runs arbitrary
// user code.
//
// This simple stack approach allows pushing protection for a series, and
// then can release protection only for the last series pushed.  A parallel
// pair of macros exists for pushing and popping of guard status for values,
// to protect any series referred to by the value's contents.  (Note: This can
// only be used on values that do not live inside of series, because there is
// no way to guarantee a value in a series will keep its address besides
// guarding the series AND locking it from resizing.)
//
// The guard stack is not meant to accumulate, and must be cleared out
// before a command ends.
//

#define PUSH_GC_GUARD(node) \
    Push_Guard_Node(node)

inline static void DROP_GC_GUARD(const Node* node) {
  #if defined(NDEBUG)
    UNUSED(node);
  #else
    if (node != *SER_LAST(const Node*, GC_Guarded)) {
        printf("DROP_GC_GUARD() pointer that wasn't last PUSH_GC_GUARD()\n");
        panic (node);
    }
  #endif

    --GC_Guarded->content.dynamic.used;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  ANY-SERIES!
//
//=////////////////////////////////////////////////////////////////////////=//

// Uses "evil macro" variations because it is called so frequently, that in
// the debug build (which doesn't inline functions) there's a notable cost.
//
inline static const REBSER *VAL_SERIES(noquote(Cell(const*)) v) {
  #if !defined(NDEBUG)
    enum Reb_Kind k = CELL_HEART(v);
    assert(
        ANY_SERIES_KIND(k)
        or k == REB_ISSUE or k == REB_URL
        or ANY_ARRAYLIKE(v)
    );
  #endif
    const REBSER *s = SER(VAL_NODE1(v));
    if (Get_Series_Flag(s, INACCESSIBLE))
        fail (Error_Series_Data_Freed_Raw());
    return s;
}

#define VAL_SERIES_ENSURE_MUTABLE(v) \
    m_cast(REBSER*, VAL_SERIES(ENSURE_MUTABLE(v)))

#define VAL_SERIES_KNOWN_MUTABLE(v) \
    m_cast(REBSER*, VAL_SERIES(KNOWN_MUTABLE(v)))


#define VAL_INDEX_RAW(v) \
    PAYLOAD(Any, (v)).second.i

#if defined(NDEBUG) || (! CPLUSPLUS_11)
    #define VAL_INDEX_UNBOUNDED(v) \
        VAL_INDEX_RAW(v)
#else
    // allows an assert, but uses C++ reference for lvalue:
    //
    //     VAL_INDEX_UNBOUNDED(v) = xxx;  // ensures v is ANY_SERIES!
    //
    // Avoids READABLE() macro, because it's assumed that it was done in the
    // type checking to ensure VAL_INDEX() applied.  (This is called often.)
    //
    inline static REBIDX VAL_INDEX_UNBOUNDED(noquote(Cell(const*)) v) {
        enum Reb_Kind k = CELL_HEART_UNCHECKED(v);  // only const if heart!
        assert(
            ANY_SERIES_KIND(k)
            or k == REB_ISSUE or k == REB_URL
            or ANY_ARRAYLIKE(v)
        );
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        return VAL_INDEX_RAW(v);
    }
    inline static REBIDX & VAL_INDEX_UNBOUNDED(Cell(*) v) {
        ASSERT_CELL_WRITABLE_EVIL_MACRO(v);
        enum Reb_Kind k = CELL_HEART_UNCHECKED(v);
        assert(
            ANY_SERIES_KIND(k)
            or k == REB_ISSUE or k == REB_URL
            or ANY_ARRAYLIKE(v)
        );
        assert(Get_Cell_Flag_Unchecked(v, FIRST_IS_NODE));
        return VAL_INDEX_RAW(v);  // returns a C++ reference
    }
#endif


inline static REBLEN VAL_LEN_HEAD(noquote(Cell(const*)) v);  // forward decl

// Unlike VAL_INDEX_UNBOUNDED() that may give a negative number or past the
// end of series, VAL_INDEX() does bounds checking and always returns an
// unsigned REBLEN.
//
inline static REBLEN VAL_INDEX(noquote(Cell(const*)) v) {
    enum Reb_Kind k = CELL_HEART(v);  // only const access if heart!
    assert(
        ANY_SERIES_KIND(k)
        or k == REB_ISSUE or k == REB_URL
        or ANY_ARRAYLIKE(v)
    );
    UNUSED(k);
    assert(Get_Cell_Flag(v, FIRST_IS_NODE));
    REBIDX i = VAL_INDEX_RAW(v);
    if (i < 0 or i > cast(REBIDX, VAL_LEN_HEAD(v)))
        fail (Error_Index_Out_Of_Range_Raw());
    return i;
}


inline static const Byte* VAL_DATA_AT(noquote(Cell(const*)) v) {
    return SER_DATA_AT(SER_WIDE(VAL_SERIES(v)), VAL_SERIES(v), VAL_INDEX(v));
}


inline static void INIT_SPECIFIER(Cell(*) v, const void *p) {
    //
    // can be called on non-bindable series, but p must be nullptr

    const REBSER *binding = SER(p);  // can't (currently) be a cell/pairing
    mutable_BINDING(v) = binding;

  #if !defined(NDEBUG)
    if (not binding)
        return;  // e.g. UNBOUND

    assert(Is_Bindable(v));  // works on partially formed values

    if (Get_Series_Flag(binding, MANAGED)) {
        assert(
            IS_DETAILS(binding)  // relative
            or IS_VARLIST(binding)  // specific
            or (
                ANY_ARRAY(v) and (IS_LET(binding) or IS_USE(binding)) // virtual
            ) or (
                IS_VARARGS(v) and Not_Series_Flag(binding, DYNAMIC)
            )  // varargs from MAKE VARARGS! [...], else is a varlist
        );
    }
    else
        assert(IS_VARLIST(binding));
  #endif
}


inline static REBVAL *Init_Series_Cell_At_Core(
    Cell(*) out,
    enum Reb_Kind type,
    const REBSER *s,  // ensured managed by calling macro
    REBLEN index,
    Array(*) specifier
){
  #if !defined(NDEBUG)
    assert(ANY_SERIES_KIND(type) or type == REB_URL);
    assert(Get_Series_Flag(s, MANAGED));

    // Note: a R3-Alpha Make_Binary() comment said:
    //
    //     Make a binary string series. For byte, C, and UTF8 strings.
    //     Add 1 extra for terminator.
    //
    // One advantage of making all binaries terminate in 0 is that it means
    // that if they were valid UTF-8, they could be aliased as Rebol strings,
    // which are zero terminated.  So it's the rule.
    //
    ASSERT_SERIES_TERM_IF_NEEDED(s);

    if (ANY_ARRAY_KIND(type))
        assert(IS_SER_ARRAY(s));
    else if (ANY_STRING_KIND(type))
        assert(IS_SER_UTF8(s));
    else {
        // Note: Binaries are allowed to alias strings
    }
  #endif

    Reset_Unquoted_Header_Untracked(
        out,
        FLAG_HEART_BYTE(type) | CELL_FLAG_FIRST_IS_NODE
    );
    INIT_VAL_NODE1(out, s);
    VAL_INDEX_RAW(out) = index;
    INIT_SPECIFIER(out, specifier);  // asserts if unbindable type tries to bind
    return cast(REBVAL*, out);
}

#define Init_Series_Cell_At(v,t,s,i) \
    Init_Series_Cell_At_Core((v), (t), \
        Force_Series_Managed_Core(s), (i), UNBOUND)

#define Init_Series_Cell(v,t,s) \
    Init_Series_Cell_At((v), (t), (s), 0)


// Out of the 8 platform pointers that comprise a series node, only 3 actually
// need to be initialized to get a functional non-dynamic series or array of
// length 0!  Only one is set here.  The info should be set by the caller.
//
inline static Stub* Prep_Stub(void *preallocated, Flags flags) {
    assert(not (flags & NODE_FLAG_CELL));

    Stub *s = cast(Stub*, preallocated);

    s->leader.bits = NODE_FLAG_NODE | flags;  // #1

  #if !defined(NDEBUG)
    SAFETRASH_POINTER_IF_DEBUG(s->link.trash);  // #2
    memset(  // https://stackoverflow.com/q/57721104/
        cast(char*, &s->content.fixed),
        0xBD,
        sizeof(s->content)
    );  // #3 - #6
    memset(&s->info, 0xAE, sizeof(s->info));  // #7
    SAFETRASH_POINTER_IF_DEBUG(s->link.trash);  // #8

    TOUCH_STUB_IF_DEBUG(s);  // tag current C stack as series origin in ASAN
  #endif

  #if DEBUG_COLLECT_STATS
    PG_Reb_Stats->Series_Made++;
  #endif

  #if DEBUG_COUNT_LOCALS
    s->num_locals = 0;
  #endif

    return s;
}


inline static PoolID Pool_Id_For_Size(Size size) {
  #if DEBUG_ENABLE_ALWAYS_MALLOC
    if (PG_Always_Malloc)
        return SYSTEM_POOL;
  #endif

    // Using a simple > or < check here triggers Spectre Mitigation warnings
    // in MSVC, while the division does not.  :-/  Hopefully the compiler is
    // smart enough to figure out how to do this efficiently in any case.

    if (size / (4 * MEM_BIG_SIZE + 1) == 0)
        return PG_Pool_Map[size]; // ((4 * MEM_BIG_SIZE) + 1) entries

    return SYSTEM_POOL;
}


// Allocates element array for an already allocated REBSER node structure.
// Resets the bias and tail to zero, and sets the new width.  Flags like
// SERIES_FLAG_FIXED_SIZE are left as they were, and other fields in the
// series structure are untouched.
//
// This routine can thus be used for an initial construction or an operation
// like expansion.
//
inline static bool Did_Series_Data_Alloc(REBSER *s, REBLEN capacity) {
    //
    // Currently once a series becomes dynamic, it never goes back.  There is
    // no shrinking process that will pare it back to fit completely inside
    // the REBSER node.
    //
    assert(Get_Series_Flag(s, DYNAMIC)); // caller sets

    Byte wide = SER_WIDE(s);
    assert(wide != 0);

    if (cast(REBU64, capacity) * wide > INT32_MAX)  // R3-Alpha said "too big"
        return false;

    Size size; // size of allocation (possibly bigger than we need)

    PoolID pool_id = Pool_Id_For_Size(capacity * wide);
    if (pool_id < SYSTEM_POOL) {
        // ...there is a pool designated for allocations of this size range
        s->content.dynamic.data = cast(char*, Try_Alloc_Pooled(pool_id));
        if (not s->content.dynamic.data)
            return false;

        // The pooled allocation might wind up being larger than we asked.
        // Don't waste the space...mark as capacity the series could use.
        size = Mem_Pools[pool_id].wide;
        assert(size >= capacity * wide);

        // We don't round to power of 2 for allocations in memory pools
        Clear_Series_Flag(s, POWER_OF_2);
    }
    else {
        // ...the allocation is too big for a pool.  But instead of just
        // doing an unpooled allocation to give you the size you asked
        // for, the system does some second-guessing to align to 2Kb
        // boundaries (or choose a power of 2, if requested).

        size = capacity * wide;
        if (Get_Series_Flag(s, POWER_OF_2)) {
            Size size2 = 2048;
            while (size2 < size)
                size2 *= 2;
            size = size2;

            // Clear the power of 2 flag if it isn't necessary, due to even
            // divisibility by the item width.
            //
            if (size % wide == 0)
                Clear_Series_Flag(s, POWER_OF_2);
        }

        s->content.dynamic.data = TRY_ALLOC_N(char, size);
        if (not s->content.dynamic.data)
            return false;

        Mem_Pools[SYSTEM_POOL].has += size;
        Mem_Pools[SYSTEM_POOL].free++;
    }

    // Note: Bias field may contain other flags at some point.  Because
    // SER_SET_BIAS() uses bit masking on an existing value, we are sure
    // here to clear out the whole value for starters.
    //
    if (IS_SER_BIASED(s))
        s->content.dynamic.bonus.bias = 0;
    else {
        // Leave as trash, or as existing bonus (if called in Expand_Series())
    }

    // The allocation may have returned more than we requested, so we note
    // that in 'rest' so that the series can expand in and use the space.
    //
    /*assert(size % wide == 0);*/  // allow irregular sizes
    s->content.dynamic.rest = size / wide;

    // We set the tail of all series to zero initially, but currently do
    // leave series termination to callers.  (This is under review.)
    //
    s->content.dynamic.used = 0;

    // See if allocation tripped our need to queue a garbage collection

    if ((GC_Ballast -= size) <= 0)
        SET_SIGNAL(SIG_RECYCLE);

    assert(SER_TOTAL(s) <= size);  // irregular sizes won't use all the space
    return true;
}


// If the data is tiny enough, it will be fit into the series node itself.
// Small series will be allocated from a memory pool.
// Large series will be allocated from system memory.
//
inline static REBSER *Make_Series_Into(
    void* preallocated,
    REBLEN capacity,
    Flags flags
){
    size_t wide = Wide_For_Flavor(cast(Flavor, FLAVOR_BYTE(flags)));
    if (cast(REBU64, capacity) * wide > INT32_MAX)
        fail (Error_No_Memory(cast(REBU64, capacity) * wide));

    Stub* s = Prep_Stub(preallocated, flags);

  #if defined(NDEBUG)
    SER_INFO(s) = SERIES_INFO_MASK_NONE;
  #else
    if (flags & SERIES_FLAG_INFO_NODE_NEEDS_MARK)
        TRASH_POINTER_IF_DEBUG(s->info.node);
    else
        SER_INFO(s) = SERIES_INFO_MASK_NONE;
  #endif

    if (
        (flags & SERIES_FLAG_DYNAMIC)  // inlining will constant fold
        or (capacity * wide > sizeof(s->content))
    ){
        // Data won't fit in a REBSER node, needs a dynamic allocation.  The
        // capacity given back as the ->rest may be larger than the requested
        // size, because the memory pool reports the full rounded allocation.

        Set_Series_Flag(s, DYNAMIC);

        if (not Did_Series_Data_Alloc(s, capacity)) {
            Clear_Series_Flag(s, MANAGED);
            Set_Series_Flag(s, INACCESSIBLE);
            GC_Kill_Series(s);  // ^-- needs non-null data unless INACCESSIBLE

            fail (Error_No_Memory(capacity * wide));
        }

      #if DEBUG_COLLECT_STATS
        PG_Reb_Stats->Series_Memory += capacity * wide;
      #endif
    }

    // It is more efficient if you know a series is going to become managed to
    // create it in the managed state.  But be sure no evaluations are called
    // before it's made reachable by the GC, or use PUSH_GC_GUARD().
    //
    // !!! Code duplicated in Make_Array_Core() ATM.
    //
    if (not (flags & NODE_FLAG_MANAGED)) {
        if (SER_FULL(GC_Manuals))
            Extend_Series_If_Necessary(GC_Manuals, 8);

        cast(REBSER**, GC_Manuals->content.dynamic.data)[
            GC_Manuals->content.dynamic.used++
        ] = s; // start out managed to not need to find/remove from this later
    }

    return s;
}

#define Make_Series_Core(capacity,flags) \
    Make_Series_Into(Alloc_Pooled(STUB_POOL), (capacity), (flags))

#define Make_Series(flavor,capacity,flags) \
    cast(Raw_##flavor*, Make_Series_Core((capacity), (flags)))


enum act_modify_mask {
    AM_PART = 1 << 0,
    AM_SPLICE = 1 << 1,
    AM_LINE = 1 << 2
};

enum act_find_mask {
    AM_FIND_CASE = 1 << 1,
    AM_FIND_MATCH = 1 << 2
};
