//
//  File: %sys-series.h
//  Summary: {Definitions for Series (REBSER) plus Array, Frame, and Map}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2016 Rebol Open Source Contributors
// REBOL is a trademark of REBOL Technologies
//
// See README.md and CREDITS.md for more information
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: the word "Series" is overloaded in Rebol to refer to two related but
// distinct concepts:
//
// * The internal system datatype, also known as a REBSER.  It's a low-level
//   implementation of something similar to a vector or an array in other
//   languages.  It is an abstraction which represents a contiguous region
//   of memory containing equally-sized elements.
//
// * The user-level value type ANY-SERIES!.  This might be more accurately
//   called ITERATOR!, because it includes both a pointer to a REBSER of
//   data and an index offset into that data.  Attempts to reconcile all
//   the naming issues from historical Rebol have not yielded a satisfying
//   alternative, so the ambiguity has stuck.
//
// This file regards the first meaning of the word "series" and covers the
// low-level implementation details of a REBSER and its variants.  For info
// about the higher-level ANY-SERIES! value type and its embedded index,
// see %sys-value.h in the definition of `struct Reb_Any_Series`.
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A REBSER is a contiguous-memory structure with an optimization of behaving
// like a kind of "double-ended queue".  It is able to reserve capacity at
// both the tail and the head, and when data is taken from the head it will
// retain that capacity...reusing it on later insertions at the head.
//
// The space at the head is called the "bias", and to save on pointer math
// per-access, the stored data pointer is actually adjusted to include the
// bias.  This biasing is backed out upon insertions at the head, and also
// must be subtracted completely to free the pointer using the address
// originally given by the allocator.
//
// The element size in a REBSER is known as the "width".  It is designed
// to support widths of elements up to 255 bytes.  (See note on SER_FREED
// about accomodating 256-byte elements.)
//
// REBSERs may be either manually memory managed or delegated to the garbage
// collector.  Free_Series() may only be called on manual series.  See
// MANAGE_SERIES() and PUSH_GUARD_SERIES() for remarks on how to work safely
// with pointers to garbage-collected series, to avoid having them be GC'd
// out from under the code while working with them.
//
// This file defines series subclasses which are type-incompatible with
// REBSER for safety.  (In C++ they would be derived classes, so common
// operations would not require casting...but this is C.)  The subclasses
// are explained where they are defined.
//
// Notes:
//
// * For the struct definition of REBSER, see %sys-rebser.h
//


//=////////////////////////////////////////////////////////////////////////=//
//
//  DEBUG PANIC
//
//=////////////////////////////////////////////////////////////////////////=//
//
// "Series Panics" will (hopefully) trigger an alert under memory tools
// like address sanitizer and valgrind that indicate the call stack at the
// moment of allocation of a series.  Then you should have TWO stacks: the
// one at the call of the Panic, and one where that series was alloc'd.
//

#if !defined(NDEBUG)
    #define Panic_Series(s) \
        Panic_Series_Debug((s), __FILE__, __LINE__);
#endif


//
// Marking
//

static inline REBOOL IS_REBSER_MARKED(REBSER *rebser) {
    return LOGICAL(rebser->header.bits & REBSER_REBVAL_FLAG_MARK);
}

static inline void MARK_REBSER(REBSER *rebser) {
    assert(NOT(IS_REBSER_MARKED(rebser)));
    rebser->header.bits |= REBSER_REBVAL_FLAG_MARK;
}

static inline void UNMARK_REBSER(REBSER *rebser) {
    assert(IS_REBSER_MARKED(rebser));
    rebser->header.bits &= ~cast(REBUPT, REBSER_REBVAL_FLAG_MARK);
}


//
// Series flags
//

#define SET_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits |= cast(REBUPT, f)))

#define CLEAR_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits &= ~cast(REBUPT, f)))

#define GET_SER_FLAG(s,f) \
    LOGICAL((s)->info.bits & (f))

#define SET_SER_FLAGS(s,f) \
    SET_SER_FLAG((s), (f))

#define CLEAR_SER_FLAGS(s,f) \
    CLEAR_SER_FLAG((s), (f))


//
// The mechanics of the macros that get or set the length of a series are a
// little bit complicated.  This is due to the optimization that allows data
// which is sizeof(REBVAL) or smaller to fit directly inside the series node.
//
// If a series is dynamically allocated out of the memory pools, then its
// data doesn't live in the REBSER node.  Without the data itself taking up
// space, there's room for a length in the node.
//
// If a series is not "dynamic" (e.g. has a full pooled allocation) then its
// length is stored in the `misc` field -unless- it is an ARRAY of values.
// If it is an array then it is assumed that even length 0 arrays might want
// to use the misc field for other purposes.  Hence the length is derived from
// the presence or absence of an END marker in the first slot.
//

#define SER_WIDE(s) \
    ((REBYTE)((s)->info.bits >> 16) & 0xff) // no use to inline in debug build

inline static REBCNT SER_LEN(REBSER *s) {
    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC))
        return s->content.dynamic.len;

    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
        return IS_END(&s->content.values[0]) ? 0 : 1;

    assert(FALSE); // !!! currently not supported, length must be implicit
    return 0;
}

inline static void SET_SERIES_LEN(REBSER *s, REBCNT len) {
    assert(!GET_SER_FLAG(s, CONTEXT_FLAG_STACK));

    if (GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)) {
        s->content.dynamic.len = len;
    }
    else {
        assert(FALSE); // currently not supported, length must be implicit
    }
}

inline static REBCNT SER_REST(REBSER *s) {
    if (GET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC))
        return s->content.dynamic.rest;

    if (GET_SER_FLAG(s, SERIES_FLAG_ARRAY))
        return 2; // includes info bits acting as trick "terminator"

    return sizeof(s->content);
}

// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
inline static REBYTE *SER_DATA_RAW(REBSER *s) {
    return GET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC)
        ? s->content.dynamic.data
        : cast(REBYTE*, &s->content.values[0]);
}

inline static REBYTE *SER_AT_RAW(size_t w, REBSER *s, REBCNT i) {
    assert(w == SER_WIDE(s));
    return SER_DATA_RAW(s) + ((w) * (i));
}

inline static void SER_SET_EXTERNAL_DATA(REBSER *s, void *p) {
    SET_SER_FLAG(s, SERIES_FLAG_HAS_DYNAMIC);
    s->content.dynamic.data = cast(REBYTE*, p);
}


//
// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `SER_HEAD(t, s)` is the same as `SER_AT(t, s, 0)`
//

#define SER_AT(t,s,i) \
    cast(t*, SER_AT_RAW(sizeof(t), (s), (i)))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)

inline static REBYTE *SER_TAIL_RAW(size_t w, REBSER *s) {
    return SER_AT_RAW(w, s, SER_LEN(s));
}

#define SER_TAIL(t,s) \
    cast(t*, SER_TAIL_RAW(sizeof(t), (s)))

inline static REBYTE *SER_LAST_RAW(size_t w, REBSER *s) {
    assert(SER_LEN(s) != 0);
    return SER_AT_RAW(w, s, SER_LEN(s) - 1);
}

#define SER_LAST(t,s) \
    cast(t*, SER_LAST_RAW(sizeof(t), (s)))

//
// Series size measurements:
//
// SER_TOTAL - bytes of memory allocated (including bias area)
// SER_SPACE - bytes of series (not including bias area)
// SER_USED - bytes being used, including terminator
//

inline static size_t SER_SPACE(REBSER *s) {
    return SER_REST(s) * SER_WIDE(s);
}

inline static size_t SER_USED(REBSER *s) {
    return (SER_LEN(s) + 1) * SER_WIDE(s);
}

#define SER_FULL(s) \
    (SER_LEN(s) + 1 >= SER_REST(s))

#define SER_AVAIL(s) \
    (SER_REST(s) - (SER_LEN(s) + 1)) // space available (minus terminator)

#define SER_FITS(s,n) \
    ((SER_LEN(s) + (n) + 1) <= SER_REST(s))

#define Is_Array_Series(s) \
    GET_SER_FLAG((s), SERIES_FLAG_ARRAY)

inline static void FAIL_IF_LOCKED_SERIES(REBSER *s) {
    if (GET_SER_FLAG(s, SERIES_FLAG_LOCKED))
        fail (Error(RE_LOCKED));
}

//
// Series external data accessible
//
#define SER_DATA_NOT_ACCESSIBLE(s) \
    (GET_SER_FLAG(s, SERIES_FLAG_EXTERNAL) \
     && !GET_SER_FLAG(s, SERIES_FLAG_ACCESSIBLE))
//
// Optimized expand when at tail (but, does not reterminate)
//

inline static void EXPAND_SERIES_TAIL(REBSER *s, REBCNT delta) {
    if (SER_FITS(s, delta))
        (s)->content.dynamic.len += delta;
    else
        Expand_Series(s, SER_LEN(s), delta);
}

inline static void RESIZE_SERIES(REBSER *s, REBCNT len) {
    s->content.dynamic.len = 0;
    if (!SER_FITS(s, len))
        Expand_Series(s, SER_LEN(s), len);
    s->content.dynamic.len = 0;
}

//
// Termination
//

inline static void RESET_TAIL(REBSER *s) {
    s->content.dynamic.len = 0;
}

// Clear all and clear to tail:
//
inline static void CLEAR_SEQUENCE(REBSER *s) {
    assert(!Is_Array_Series(s));
    CLEAR(SER_DATA_RAW(s), SER_SPACE(s));
}

inline static void TERM_SEQUENCE(REBSER *s) {
    assert(!Is_Array_Series(s));
    memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM(s) \
        NOOP
#else
    #define ASSERT_SERIES_TERM(s) \
        Assert_Series_Term_Core(s)
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define NOTE_SERIES_MAYBE_TERM(s) NOOP


//=////////////////////////////////////////////////////////////////////////=//
//
//  SERIES MANAGED MEMORY
//
//=////////////////////////////////////////////////////////////////////////=//
//
// When a series is allocated by the Make_Series routine, it is not initially
// visible to the garbage collector.  To keep from leaking it, then it must
// be either freed with Free_Series or delegated to the GC to manage with
// MANAGE_SERIES.
//
// (In debug builds, there is a test at the end of every Rebol function
// dispatch that checks to make sure one of those two things happened for any
// series allocated during the call.)
//
// The implementation of MANAGE_SERIES is shallow--it only sets a bit on that
// *one* series, not any series referenced by values inside of it.  This
// means that you cannot build a hierarchical structure that isn't visible
// to the GC and then do a single MANAGE_SERIES call on the root to hand it
// over to the garbage collector.  While it would be technically possible to
// deeply walk the structure, the efficiency gained from pre-building the
// structure with the managed bit set is significant...so that's how deep
// copies and the scanner/load do it.
//
// (In debug builds, if any unmanaged series are found inside of values
// reachable by the GC, it will raise an alert.)
//

inline static REBOOL IS_SERIES_MANAGED(REBSER *s) {
    return LOGICAL(s->header.bits & REBSER_REBVAL_FLAG_MANAGED);
}

#define MANAGE_SERIES(s) \
    Manage_Series(s)

inline static void ENSURE_SERIES_MANAGED(REBSER *s) {
    if (NOT(IS_SERIES_MANAGED(s)))
        MANAGE_SERIES(s);
}

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s) \
        NOOP

    #define ASSERT_VALUE_MANAGED(v) \
        NOOP
#else
    inline static void ASSERT_SERIES_MANAGED(REBSER *s) {
        if (NOT(IS_SERIES_MANAGED(s)))
            Panic_Series(s);
    }

    #define ASSERT_VALUE_MANAGED(v) \
        assert(Is_Value_Managed(v))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  GUARDING SERIES FROM GARBAGE COLLECTION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The garbage collector can run anytime the evaluator runs (and also when
// ports are used).  So if a series has had MANAGE_SERIES run on it, the
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

#define PUSH_GUARD_SERIES(s) \
    Guard_Series_Core(s)

inline static void DROP_GUARD_SERIES(REBSER *s) {
    assert(s == *SER_LAST(REBSER*, GC_Series_Guard));
    GC_Series_Guard->content.dynamic.len--;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  BINARY and STRING series
//
//=////////////////////////////////////////////////////////////////////////=//
//
// The current implementation of Rebol's ANY-STRING! type has two different
// series widths that are used.  One is the BYTE_SIZED() series which encodes
// ASCII in the low bits, and Latin-1 extensions in the range 0x80 - 0xFF.
// So long as a codepoint can fit in this range, the string can be stored in
// single bytes:
//
// https://en.wikipedia.org/wiki/Latin-1_Supplement_(Unicode_block)
//
// (Note: This is not to be confused with the other "byte-width" encoding,
// which is UTF-8.  Rebol series routines are not set up to handle insertions
// or manipulations of UTF-8 encoded data in a Reb_Any_String payload at
// this time...it is a format used only in I/O.)
//
// The second format that is used puts codepoints into a 16-bit REBUNI-sized
// element.  If an insertion of a string or character into a byte sized
// string cannot be represented in 0xFF or lower, then the target string will
// be "widened"--doubling the storage space taken and requiring updating of
// the character data in memory.  At this time there are no "in-place"
// cases where a string is reduced from REBUNI to byte sized, but operations
// like Copy_String_Slimming() will scan a source string to see if a byte-size
// copy can be made from a REBUNI-sized one without loss of information.
//
// Byte-sized series are also used by the BINARY! datatype.  There is no
// technical difference between such series used as strings or used as binary,
// the difference comes from being marked REB_BINARY or REB_STRING in the
// header of the value carrying the series.
//
// For easier type-correctness, the series macros are given with names BIN_XXX
// and UNI_XXX.  There aren't distinct data types for the series themselves,
// just REBSER* is used.  Hence BIN_LEN() and UNI_LEN() aren't needed as you
// could just use SER_LEN(), but it helps a bit for readability...and an
// assert is included to ensure the size matches up.
//

// Is it a byte-sized series?
//
// !!! This trick in R3-Alpha "works because no other odd size allowed".  Is
// it worth it to prohibit odd sizes for this trick?  An assertion that the
// size is not odd was added to Make_Series; reconsider if this becomes an
// issue at some point.
//
#define BYTE_SIZE(s) \
    LOGICAL(((s)->info.bits) & (1 << 16))

//
// BIN_XXX: Binary or byte-size string seres macros
//

#define BIN_AT(s,n) \
    SER_AT(REBYTE, (s), (n))

#define BIN_HEAD(s) \
    SER_HEAD(REBYTE, (s))

#define BIN_TAIL(s) \
    SER_TAIL(REBYTE, (s))

#define BIN_LAST(s) \
    SER_LAST(REBYTE, (s))

inline static REBCNT BIN_LEN(REBSER *s) {
    assert(BYTE_SIZE(s));
    return SER_LEN(s);
}

inline static void SET_BIN_END(REBSER *s, REBCNT n) {
    *BIN_AT(s, n) = 0;
}

//
// UNI_XXX: Unicode string series macros
//

inline static REBCNT UNI_LEN(REBSER *s) {
    assert(SER_WIDE(s) == sizeof(REBUNI));
    return SER_LEN(s);
}

inline static void SET_UNI_LEN(REBSER *s, REBCNT len) {
    assert(SER_WIDE(s) == sizeof(REBUNI));
    SET_SERIES_LEN(s, len);
}

#define UNI_AT(s,n) \
    SER_AT(REBUNI, (s), (n))

#define UNI_HEAD(s) \
    SER_HEAD(REBUNI, (s))

#define UNI_TAIL(s) \
    SER_TAIL(REBUNI, (s))

#define UNI_LAST(s) \
    SER_LAST(REBUNI, (s))

inline static void UNI_TERM(REBSER *s) {
    *UNI_TAIL(s) = 0;
}

//
// Get a char, from either byte or unicode string:
//

inline static REBUNI GET_ANY_CHAR(REBSER *s, REBCNT n) {
    return BYTE_SIZE(s) ? BIN_HEAD(s)[n] : UNI_HEAD(s)[n];
}

inline static void SET_ANY_CHAR(REBSER *s, REBCNT n, REBYTE c) {
    if BYTE_SIZE(s)
        BIN_HEAD(s)[n] = c;
    else
        UNI_HEAD(s)[n] = c;
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBSTR series for UTF-8 strings
//
//=////////////////////////////////////////////////////////////////////////=//
//
// R3-Alpha and Red would work with strings in their decoded form, in
// series of varying widths.  Ren-C's goal is to replace this with the idea
// of "UTF-8 everywhere", working with the strings as UTF-8 and only
// converting if the platform requires it for I/O (e.g. Windows):
//
// http://utf8everywhere.org/
//
// As a first step toward this goal, one place where strings were kept in
// UTF-8 form has been converted into series...the word table.  So for now,
// all REBSTR instances are for ANY-WORD!.
//
// The concept is that a SYM refers to one of the built-in words and can
// be used in C switch statements.  A canon STR is used to identify everything
// else.
//

inline static const REBYTE *STR_HEAD(REBSTR *str) {
    return BIN_HEAD(str);
}

inline static REBSTR *STR_CANON(REBSTR *str) {
    if (GET_SER_FLAG(str, STRING_FLAG_CANON))
        return str;
    return str->misc.canon;
}

inline static OPT_REBSYM STR_SYMBOL(REBSTR *str) {
    assert(STR_CANON(str)->header.bits == str->header.bits);
    return cast(REBSYM, str->header.bits >> 16);
}

inline static REBSTR *Canon(REBSYM sym) {
    assert(cast(REBCNT, sym) != 0);
    assert(cast(REBCNT, sym) < SER_LEN(PG_Symbol_Canons));
    return *SER_AT(REBSTR*, PG_Symbol_Canons, cast(REBCNT, sym));
}

inline static REBOOL SAME_STR(REBSTR *s1, REBSTR *s2) {
    if (s1 == s2) return TRUE; // !!! does this check speed things up or not?
    return LOGICAL(STR_CANON(s1) == STR_CANON(s2)); // canon check, quite fast
}


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBARR (a.k.a. "Rebol Array")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A "Rebol Array" is a series of REBVAL values which is terminated by an
// END marker.  While many operations are shared in common with REBSER, the
// (deliberate) type incompatibility requires either a cast with ARR_SERIES
// or use of a wrapper macro from this list.
//
// !!! Write more about the special concerns of arrays here.
//

struct Reb_Array {
    struct Reb_Series series;
};

// These do REBSER <=> REBARR coercion.  Although it's desirable to make
// them type incompatible for most purposes, some operations require treating
// one kind of pointer as the other (and they are both Reb_Series)
//
// !!! See notes on AS_CONTEXT about the dodginess of how this is currently
// done.  But also see the note that it's something that could just be
// disabled by making arrays and series synonyms in "non-type-check" builds.
//
#define AS_ARRAY(s) \
    (cast(REBARR*, (s)))

#define ARR_SERIES(a) \
    (&(a)->series)

// HEAD, TAIL, and LAST refer to specific value pointers in the array.  An
// empty array should have an END marker in its head slot, and since it has
// no last value then ARR_LAST should not be called (this is checked in
// debug builds).  A fully constructed array should always have an END
// marker in its last slot, which is one past the last position that is
// valid for writing a full REBVAL.
//
// An ARRAY is the main place in the system where "relative" values come
// from, because all relative words are created during the copy of the
// bodies of functions.  The array accessors must err on the safe side and
// give back a relative value.  Many inspection operations are legal on
// a relative value, but it cannot be copied without a "specifier" FRAME!
// context (which is also required to do a GET_VAR lookup).

#define ARR_AT(a, n) \
    SER_AT(RELVAL, ARR_SERIES(a), (n))

#define ARR_HEAD(a) \
    SER_HEAD(RELVAL, ARR_SERIES(a))

#define ARR_TAIL(a) \
    SER_TAIL(RELVAL, ARR_SERIES(a))

#define ARR_LAST(a) \
    SER_LAST(RELVAL, ARR_SERIES(a))

// As with an ordinary REBSER, a REBARR has separate management of its length
// and its terminator.  Many routines seek to control these independently for
// performance reasons (for better or worse).
//
inline static REBCNT ARR_LEN(REBARR *a) {
    assert(Is_Array_Series(ARR_SERIES(a)));
    return SER_LEN(ARR_SERIES(a));
}

inline static void SET_ARRAY_LEN(REBARR *a, REBCNT len) {
    assert(Is_Array_Series(ARR_SERIES(a)));
    SET_SERIES_LEN(ARR_SERIES(a), len);
}

//
// !!! Write more about termination in series documentation.
//

#define TERM_ARRAY(a) \
    SET_END(ARR_TAIL(a))

inline static void RESET_ARRAY(REBARR *a) {
    SET_ARRAY_LEN(a, 0);
    TERM_ARRAY(a);
}

inline static void TERM_SERIES(REBSER *s) {
    if (Is_Array_Series(s))
        TERM_ARRAY(AS_ARRAY(s));
    else
        memset(SER_AT_RAW(SER_WIDE(s), s, SER_LEN(s)), 0, SER_WIDE(s));
}

inline static void RESET_SERIES(REBSER *s) {
    s->content.dynamic.len = 0;
    TERM_SERIES(s);
}

// Setting and getting array flags is common enough to want a macro for it
// vs. having to extract the ARR_SERIES to do it each time.
//
#define SET_ARR_FLAG(a,f) \
    SET_SER_FLAG(ARR_SERIES(a), (f))

#define CLEAR_ARR_FLAG(a,f) \
    CLEAR_SER_FLAG(ARR_SERIES(a), (f))

#define GET_ARR_FLAG(a,f) \
    GET_SER_FLAG(ARR_SERIES(a), (f))

#define FAIL_IF_LOCKED_ARRAY(a) \
    FAIL_IF_LOCKED_SERIES(ARR_SERIES(a))

#define PUSH_GUARD_ARRAY(a) \
    PUSH_GUARD_SERIES(ARR_SERIES(a))

#define DROP_GUARD_ARRAY(a) \
    DROP_GUARD_SERIES(ARR_SERIES(a))

#define IS_ARRAY_MANAGED(array) \
    IS_SERIES_MANAGED(ARR_SERIES(array))

#define MANAGE_ARRAY(array) \
    MANAGE_SERIES(ARR_SERIES(array))

#define ENSURE_ARRAY_MANAGED(array) \
    ENSURE_SERIES_MANAGED(ARR_SERIES(array))

#define Append_Value(a,v) \
    (*Alloc_Tail_Array(a) = *(v), NOOP)

#define Append_Value_Core(a,v,s) \
    COPY_VALUE(Alloc_Tail_Array(a), (v), (s))

#define Copy_Values_Len_Shallow(v,s,l) \
    Copy_Values_Len_Extra_Shallow((v), (s), (l), 0)

#define Copy_Array_Shallow(a,s) \
    Copy_Array_At_Shallow((a), 0, (s))

#define Copy_Array_Deep_Managed(a,s) \
    Copy_Array_At_Extra_Deep_Managed((a), 0, (s), 0)

#define Copy_Array_At_Deep_Managed(a,i,s) \
    Copy_Array_At_Extra_Deep_Managed((a), (i), (s), 0)

#define COPY_ANY_ARRAY_AT_DEEP_MANAGED(v) \
    Copy_Array_At_Extra_Deep_Managed( \
        VAL_ARRAY(v), VAL_INDEX(v), VAL_SPECIFIER(v), 0)

#define Copy_Array_At_Shallow(a,i,s) \
    Copy_Array_At_Extra_Shallow((a), (i), (s), 0)

#define Copy_Array_Extra_Shallow(a,s,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (s), (e))

#define Free_Array(a) \
    Free_Series(ARR_SERIES(a))

#ifdef NDEBUG
    #define ASSERT_ARRAY(s) \
        NOOP

    #define ASSERT_ARRAY_MANAGED(array) \
        NOOP

    #define ASSERT_SERIES(s) \
        NOOP
#else
    #define ASSERT_ARRAY(s) \
        Assert_Array_Core(s)

    #define ASSERT_ARRAY_MANAGED(array) \
        ASSERT_SERIES_MANAGED(ARR_SERIES(array))

    #define Panic_Array(a) \
        Panic_Series(ARR_SERIES(a))

    #define Debug_Array(a) \
        Debug_Series(ARR_SERIES(a))

    static inline void ASSERT_SERIES(REBSER *s) {
        if (Is_Array_Series(s))
            Assert_Array_Core(AS_ARRAY(s));
        else
            Assert_Series_Core(s);
    }
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBCTX (a.k.a. "Context")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// In Rebol terminology, a "context" is an abstraction which gives two
// parallel arrays, whose indices line up in a correspondence:
//
// * "keylist" - an array that contains TYPESET! values, but which have a
//   symbol ID encoded as an extra piece of information for that key.
//
// * "varlist" - an array of equal length to the keylist, which holds an
//   arbitrary REBVAL in each position that corresponds to its key.
//
// There is an additional aspect of a context which is that it holds two
// extra values in the 0 slot of each array (hence indexing the keys and
// vars is done 1-based).
//

struct Reb_Context {
    struct Reb_Array varlist; // keylist is held in ->link.keylist
};

#ifdef NDEBUG
    #define ASSERT_CONTEXT(c) cast(void, 0)
#else
    #define ASSERT_CONTEXT(c) Assert_Context_Core(c)
#endif

// Series-to-Frame cocercion
//
// !!! This cast is of a pointer to a type to a pointer to a struct with
// just that type in it may not technically be legal in the standard w.r.t.
// strict aliasing.  Review this mechanic.  There's probably a legal way
// of working around it.  But if worst comes to worst, it can be disabled
// and the two can be made equivalent--except when doing a build for
// type checking purposes.
//
#ifdef NDEBUG
    #define AS_CONTEXT(s)       cast(REBCTX*, (s))
#else
    // Put a debug version here that asserts.
    #define AS_CONTEXT(s)       cast(REBCTX*, (s))
#endif

inline static REBARR *CTX_VARLIST(REBCTX *c) {
    return &c->varlist;
}

// It's convenient to not have to extract the array just to check/set flags
//
inline static void SET_CTX_FLAG(REBCTX *c, REBUPT f) {
    SET_ARR_FLAG(CTX_VARLIST(c), f);
}

inline static void CLEAR_CTX_FLAG(REBCTX *c, REBUPT f) {
    CLEAR_ARR_FLAG(CTX_VARLIST(c), f);
}

inline static REBOOL GET_CTX_FLAG(REBCTX *c, REBUPT f) {
    return GET_ARR_FLAG(CTX_VARLIST(c), f);
}

// If you want to talk generically about a context just for the purposes of
// setting its series flags (for instance) and not to access the "varlist"
// data, then use CTX_SERIES(), as actual var access is hybridized
// between stack vars and dynamic vars...so there's not always a "varlist"
//
#define CTX_SERIES(c) \
    ARR_SERIES(CTX_VARLIST(c))

//
// Special property: keylist pointer is stored in the misc field of REBSER
//

inline static REBARR *CTX_KEYLIST(REBCTX *c) {
    return ARR_SERIES(CTX_VARLIST(c))->link.keylist;
}

static inline void INIT_CTX_KEYLIST_SHARED(REBCTX *c, REBARR *keylist) {
    SET_ARR_FLAG(keylist, KEYLIST_FLAG_SHARED);
    ARR_SERIES(CTX_VARLIST(c))->link.keylist = keylist;
}

static inline void INIT_CTX_KEYLIST_UNIQUE(REBCTX *c, REBARR *keylist) {
    assert(NOT(GET_ARR_FLAG(keylist, KEYLIST_FLAG_SHARED)));
    ARR_SERIES(CTX_VARLIST(c))->link.keylist = keylist;
}

// Navigate from context to context components.  Note that the context's
// "length" does not count the [0] cell of either the varlist or the keylist.
// Hence it must subtract 1.  Internally to the context building code, the
// real length of the two series must be accounted for...so the 1 gets put
// back in, but most clients are only interested in the number of keys/values
// (and getting an answer for the length back that was the same as the length
// requested in context creation).
//
#define CTX_LEN(c) \
    (ARR_LEN(CTX_KEYLIST(c)) - 1)

#define CTX_ROOTKEY(c) \
    SER_HEAD(REBVAL, ARR_SERIES(CTX_KEYLIST(c)))

#define CTX_TYPE(c) \
    VAL_TYPE(CTX_VALUE(c))

// The keys and vars are accessed by positive integers starting at 1.  If
// indexed access is used then the debug build will check to be sure that
// the indexing is legal.  To get a pointer to the first key or value
// regardless of length (e.g. will be an END if 0 keys/vars) use HEAD
//
// Rather than use ARR_AT (which returns RELVAL*) for the vars, this uses
// SER_AT to get REBVALs back, because the values of the context are known to
// not live in function body arrays--hence they can't hold relative words.
// Keys can't hold relative values either.
//
inline static REBVAL *CTX_KEYS_HEAD(REBCTX *c) {
    return SER_AT(REBVAL, ARR_SERIES(CTX_KEYLIST(c)), 1);
}

// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// REBSER node data itself.
//
inline static REBVAL *CTX_VALUE(REBCTX *c) {
    return GET_CTX_FLAG(c, CONTEXT_FLAG_STACK)
        ? KNOWN(&ARR_SERIES(CTX_VARLIST(c))->content.values[0])
        : KNOWN(ARR_HEAD(CTX_VARLIST(c))); // not a RELVAL
}

inline static struct Reb_Frame *CTX_FRAME(REBCTX *c) {
    return ARR_SERIES(CTX_VARLIST(c))->misc.f;
}

inline static REBVAL *CTX_VARS_HEAD(REBCTX *c) {
    return GET_CTX_FLAG(c, CONTEXT_FLAG_STACK)
        ? CTX_FRAME(c)->stackvars // if NULL, this will crash
        : SER_AT(REBVAL, ARR_SERIES(CTX_VARLIST(c)), 1);
}

inline static REBVAL *CTX_KEY(REBCTX *c, REBCNT n) {
    assert(n != 0 && n <= CTX_LEN(c));
    REBVAL *key = CTX_KEYS_HEAD(c) + (n) - 1;
    assert(key->extra.key_spelling != NULL);
    return key;
}

inline static REBVAL *CTX_VAR(REBCTX *c, REBCNT n) {
    REBVAL *var;
    assert(n != 0 && n <= CTX_LEN(c));
    assert(GET_ARR_FLAG(CTX_VARLIST(c), ARRAY_FLAG_CONTEXT_VARLIST));

    var = CTX_VARS_HEAD(c) + (n) - 1;

    assert(NOT(var->header.bits & VALUE_FLAG_RELATIVE));

    return var;
}

inline static REBSTR *CTX_KEY_CASED(REBCTX *c, REBCNT n) {
    return CTX_KEY(c, n)->extra.key_spelling;
}

inline static REBSTR *CTX_KEY_CANON(REBCTX *c, REBCNT n) {
    return STR_CANON(CTX_KEY_CASED(c, n));
}

inline static REBSYM CTX_KEY_SYM(REBCTX *c, REBCNT n) {
    return STR_SYMBOL(CTX_KEY_CASED(c, n)); // should be same as canon
}

inline static REBCTX *CTX_META(REBCTX *c) {
    return ARR_SERIES(CTX_KEYLIST(c))->link.meta;
}

inline static REBVAL *CTX_STACKVARS(REBCTX *c) {
    return CTX_FRAME(c)->stackvars;
}

#define FAIL_IF_LOCKED_CONTEXT(c) \
    FAIL_IF_LOCKED_ARRAY(CTX_VARLIST(c))

inline static void FREE_CONTEXT(REBCTX *c) {
    Free_Array(CTX_KEYLIST(c));
    Free_Array(CTX_VARLIST(c));
}

#define PUSH_GUARD_CONTEXT(c) \
    PUSH_GUARD_ARRAY(CTX_VARLIST(c)) // varlist points to/guards keylist

#define DROP_GUARD_CONTEXT(c) \
    DROP_GUARD_ARRAY(CTX_VARLIST(c))

#if! defined(NDEBUG)
    #define Panic_Context(c) \
        Panic_Array(CTX_VARLIST(c))
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBFUN (a.k.a. "Func")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Using a technique strongly parallel to CONTEXT, a function is identified
// by a series which acts as its paramlist, in which the 0th element is an
// ANY-FUNCTION! value.  Unlike a CONTEXT, a FUNC does not have values of its
// own... only parameter definitions (or "params").  The arguments ("args")
// come from finding a function instantiation on the stack.
//

struct Reb_Func {
    struct Reb_Array paramlist;
};

#ifdef NDEBUG
    #define AS_FUNC(s) \
        cast(REBFUN*, (s))
#else
    #define AS_FUNC(s) \
        cast(REBFUN*, (s)) // !!! worth it to add debug version that checks?
#endif

inline static REBARR *FUNC_PARAMLIST(REBFUN *f) {
    return &f->paramlist;
}

inline static REBVAL *FUNC_VALUE(REBFUN *f) {
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), 0);
}

inline static REBNAT FUNC_DISPATCHER(REBFUN *f) {
    return ARR_SERIES(
        FUNC_VALUE(f)->payload.function.body_holder
    )->misc.dispatcher;
}

inline static RELVAL *FUNC_BODY(REBFUN *f) {
    assert(ARR_LEN(FUNC_VALUE(f)->payload.function.body_holder) == 1);
    return ARR_HEAD(FUNC_VALUE(f)->payload.function.body_holder);
}

inline static REBVAL *FUNC_PARAM(REBFUN *f, REBCNT n) {
    assert(n != 0 && n < ARR_LEN(FUNC_PARAMLIST(f)));
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), n);
}

inline static REBCNT FUNC_NUM_PARAMS(REBFUN *f) {
    return ARR_LEN(FUNC_PARAMLIST(f)) - 1;
}

inline static REBCTX *FUNC_META(REBFUN *f) {
    return ARR_SERIES(FUNC_PARAMLIST(f))->link.meta;
}

// Note: On Windows, FUNC_DISPATCH is already defined in the header files
//
#define FUNC_DISPATCHER(f) \
    (ARR_SERIES(FUNC_VALUE(f)->payload.function.body_holder)->misc.dispatcher)

// There is no binding information in a function parameter (typeset) so a
// REBVAL should be okay.
//
inline static REBVAL *FUNC_PARAMS_HEAD(REBFUN *f) {
    return SER_AT(REBVAL, ARR_SERIES(FUNC_PARAMLIST(f)), 1);
}

inline static REBSTR *FUNC_PARAM_CASED(REBFUN *f, REBCNT n) {
    return FUNC_PARAM((f), (n))->extra.key_spelling;
}

inline static REBRIN *FUNC_ROUTINE(REBFUN *f) {
    return cast(REBRIN*, FUNC_BODY(f)->payload.handle.data);
}



//=////////////////////////////////////////////////////////////////////////=//
//
//  REBMAP (a.k.a. "Rebol Map")
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Maps are implemented as a light hashing layer on top of an array.  The
// hash indices are stored in the series node's "misc", while the values are
// retained in pairs as `[key val key val key val ...]`.
//
// When there are too few values to warrant hashing, no hash indices are
// made and the array is searched linearly.  This is indicated by the hashlist
// being NULL.
//
// Though maps are not considered a series in the "ANY-SERIES!" value sense,
// they are implemented using series--and hence are in %sys-series.h, at least
// until a better location for the definition is found.
//
// !!! Should there be a MAP_LEN()?  Current implementation has NONE in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.
//

struct Reb_Map {
    struct Reb_Array pairlist; // hashlist is held in ->link.hashlist
};

#define MAP_PAIRLIST(m) \
    (&(m)->pairlist)

#define MAP_HASHLIST(m) \
    (ARR_SERIES(&(m)->pairlist)->link.hashlist)

#define MAP_HASHES(m) \
    SER_HEAD(MAP_HASHLIST(m))

#define AS_MAP(s) \
    cast(REBMAP*, (s))
