//
// Rebol 3 Language Interpreter and Run-time Environment
// "Ren-C" branch @ https://github.com/metaeducation/ren-c
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2015 Rebol Open Source Contributors
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
//  Summary: Definitions for Series (REBSER) plus Array, Frame, and Map
//  File: %sys-series.h
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Note: the word "Series" is overloaded in Rebol to refer to two related but
// distinct concepts:
//
// * The internal system datatype, also known as a REBSER.  It's a low-level
//   implementation of something similar to a vector or an array in other
//   languages.  It is an abstraction which represens a contiguous region
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


//=////////////////////////////////////////////////////////////////////////=//
//
//  REBSER (a.k.a. `struct Reb_Series`)
//
//=////////////////////////////////////////////////////////////////////////=//
//
// This structure is a small descriptor for a series, and contains information
// about its content and flags.  Every string and block in REBOL has one.
//
// The REBSER is fixed-size, and is allocated as a "node" from a memory pool.
// That pool quickly grants and releases memory ranges that are sizeof(REBSER)
// without needing to use malloc() and free() for each individual allocation.
// These nodes can also be enumerated in the pool without needing the series
// to be tracked via a linked list or other structure.  The garbage collector
// is one example of code that performs such an enumeration.
//
// A REBSER node pointer will remain valid as long as outstanding references
// to the series exist in values visible to the GC.  On the other hand, the
// series's data pointer may be freed and reallocated to respond to the needs
// of resizing.  (In the future, it may be reallocated just as an idle task
// by the GC to reclaim or optimize space.)  Hence pointers into data in a
// managed series *must not be held onto across evaluations*.
//
// If the data requirements of a series are small, then rather than using the
// space in the REBSER to track a dynamic allocation, the data is included
// directly in the node itself.  The capacity is chosen to be exactly enough
// to store a single REBVAL so that length 0 or 1 series can be represented.
// However, that same amount of capacity may be used by arbitrary series (such
// as strings) if they too are small enough to fit.
//

// Series Flags
//
enum {
    // `SERIES_FLAG_0_IS_FALSE` represents the lowest bit and should always be
    // set to zero.  This is because it means that when the REBSER's
    // contents are interpreted as value data, then the info bits can do
    // double-duty serving as an END marker.  For a description of the
    // method see notes on NOT_END_MASK.
    //
    SERIES_FLAG_0_IS_FALSE = 1 << 0,

    // `SERIES_FLAG_1_IS_FALSE` is the second lowest bit, and it is set to zero
    // as a safety precaution.  In the debug build this is checked by value
    // writes to ensure that when the info flags are serving double duty
    // as an END marker, they do not get overwritten by rogue code that
    // thought a REBVAL* pointing at the memory had a full value's worth
    // of memory to write into.  See WRITABLE_MASK_DEBUG.
    //
    SERIES_FLAG_1_IS_FALSE = 1 << 1,

    // `SERIES_FLAG_HAS_DYNAMIC` indicates that this series has a dynamically
    // allocated portion.  If it does not, then its data pointer is the
    // address of the embedded value inside of it (marked terminated by
    // the SERIES_FLAG_0_IS_ZERO if it has an element in it)
    //
    SERIES_FLAG_HAS_DYNAMIC = 1 << 2,

    // `SERIES_FLAG_MARK` is used in the "mark and sweep" method of garbage
    // collection.  It is also used for other purposes which need to go
    // through and set a generic bit, e.g. to protect against loops in
    // the transitive closure ("if you hit a SER_MARK, then you've already
    // processed this series").
    //
    // Because of the dual purpose, it's important to be sure to not run
    // garbage collection while one of these alternate uses is in effect.
    // It's also important to reset the bit when done, as GC assumes when
    // it starts that all bits are cleared.  (The GC itself clears all
    // the bits by enumerating every series in the series pool during the
    // sweeping phase.)
    //
    // !!! With more series bits now available, the dual purpose is something
    // that should be reexamined--so long as bits are free, why reuse if it
    // creates more risk?
    //
    SERIES_FLAG_MARK = 1 << 3,

    // `SERIES_FLAG_MANAGED` indicates that a series is managed by garbage
    // collection.  If this bit is not set, then during the GC's sweeping
    // phase the simple fact that it hasn't been SER_MARK'd won't be enough
    // to let it be considered for freeing.
    //
    // See MANAGE_SERIES for details on the lifecycle of a series (how it
    // starts out manually managed, and then must either become managed or be
    // freed before the evaluation that created it ends).
    //
    SERIES_FLAG_MANAGED = 1 << 4,

    // `SERIES_FLAG_ARRAY` indicates that this is a series of REBVAL values,
    // and suitable for using as the payload of an ANY-ARRAY! value.  When a
    // series carries this bit, that means that if it is also SER_MANAGED
    // then the garbage collector will process its transitive closure to
    // make sure all the values it contains (and the values its references
    // contain) do not have series GC'd out from under them.
    //
    // (In R3-Alpha, whether a series was an array or not was tested by if
    // its width was sizeof(REBVAL).  The Ren-C approach allows for the
    // creation of series that contain items that incidentally happen to be
    // the same size as a REBVAL, while not actually being REBVALs.)
    //
    SERIES_FLAG_ARRAY = 1 << 5,

    // `SERIES_FLAG_CONTEXT` indicates this series represents the "varlist"
    // of a context.  A second series can be reached from it via the `->misc`
    // field in the series node, which is a second array known as a "keylist".
    //
    // See notes on REBCTX for further details about what a context is.
    //
    SERIES_FLAG_CONTEXT = 1 << 6,

    // `SERIES_FLAG_LOCKED` indicates that the series size or values cannot
    // be modified.  This check is honored by some layers of abstraction, but
    // if one manages to get a raw pointer into a value in the series data
    // then by that point it cannot be enforced.
    //
    // !!! Could the 'writable' flag be used for this in the debug build,
    // if the locking process went through and cleared writability...then
    // put it back if the series were unlocked?
    //
    // This is related to the feature in PROTECT (OPT_TYPESET_LOCKED) which
    // protects a certain variable in a context from being changed.  Yet
    // it is distinct as it's a protection on a series itself--which ends
    // up affecting all variable content with that series in the payload.
    //
    SERIES_FLAG_LOCKED = 1 << 7,

    // `SERIES_FLAG_FIXED_SIZE` indicates the size is fixed, and the series
    // cannot be expanded or contracted.  Values within the series are still
    // writable, assuming SERIES_FLAG_LOCKED isn't set.
    //
    // !!! Is there checking in all paths?  Do series contractions check this?
    //
    // One important reason for ensuring a series is fixed size is to avoid
    // the possibility of the data pointer being reallocated.  This allows
    // code to ignore the usual rule that it is unsafe to hold a pointer to
    // a value inside the series data.
    //
    // !!! Strictly speaking, SERIES_FLAG_NO_RELOCATE could be different
    // from fixed size... if there would be a reason to reallocate besides
    // changing size (such as memory compaction).
    //
    SERIES_FLAG_FIXED_SIZE  = 1 << 8,

    // `SERIES_FLAG_POWER_OF_2` is set when an allocation size was rounded to
    // a power of 2.  This flag was introduced in Ren-C when accounting was
    // added to make sure the system's notion of how much memory allocation
    // was outstanding would balance out to zero by the time of exiting the
    // interpreter.
    //
    // The problem was that the allocation size was measured in terms of the
    // number of elements.  If the elements themselves were not the size of
    // a power of 2, then to get an even power-of-2 size of memory allocated
    // the memory block would not be an even multiple of the element size.
    // Rather than track the actual memory allocation size as a 32-bit number,
    // a single bit flag remembering that the allocation was a power of 2
    // was enough to recreate the number to balance accounting at free time.
    //
    // !!! The rationale for why series were ever allocated to a power of 2
    // should be revisited.  Current conventional wisdom suggests that asking
    // for the amount of memory you need and not using powers of 2 is
    // generally a better idea:
    //
    // http://stackoverflow.com/questions/3190146/
    //
    SERIES_FLAG_POWER_OF_2  = 1 << 9,

    // `SERIES_FLAG_EXTERNAL` indicates that when the series was created, the
    // `->data` pointer was poked in by the creator.  It takes responsibility
    // for freeing it, so don't free() on GC.
    //
    // !!! It's not clear what the lifetime management of data used in this
    // way is.  If the external system receives no notice when Rebol is done
    // with the data and GC's the series, how does it know when it's safe
    // to free the data or not?  The feature is not used by the core or
    // Ren-Cpp, but by relatively old extensions...so there may be no good
    // answer in the case of those clients (likely either leaks or crashes).
    //
    SERIES_FLAG_EXTERNAL = 1 << 10,

    // `SERIES_FLAG_ACCESSIBLE` indicates that the external memory pointed by
    // `->data` is accessible. This is not checked at every access to the
    // `->data` for the performance consideration, only on those that are
    // known to have possible external memory storage.  Currently this is
    // used for STRUCT! and to note when an SERIES_FLAG_STACK series has its
    // stack level popped (there's no data to lookup for words bound to it)
    //
    SERIES_FLAG_ACCESSIBLE = 1 << 11,

    // `SERIES_FLAG_STACK` indicates that this series data lives on the stack.
    // This is a work in progress to unify objects and function call frames
    // as a prelude to unifying FUNCTION! and CLOSURE!.
    //
    // !!! Ultimately this flag may be unnecessary because stack-based and
    // dynamic series will "hybridize" so that they may have some stack
    // fields and some fields in dynamic memory.  The foundation already
    // exists, and both can be stored in the same REBSER.  It's just a problem
    // of mapping index numbers in the function paramlist into either the
    // stack array or the dynamic array during binding in an efficient way.
    //
    SERIES_FLAG_STACK = 1 << 12
};

struct Reb_Series_Dynamic {
    //
    // `data` is the "head" of the series data.  It may not point directly at
    // the memory location that was returned from the allocator if it has
    // bias included in it.
    //
    REBYTE *data;

    // `len` is one past end of useful data.
    //
    REBCNT len;

    // `rest` is the total number of units from bias to end.  Having a
    // slightly weird name draws attention to the idea that it's not really
    // the "capacity", just the "rest of the capacity after the bias".
    //
    REBCNT rest;

    // This is the 4th pointer on 32-bit platforms which could be used for
    // something when a series is dynamic.  Previously the bias was not
    // a full REBCNT but was limited in range to 16 bits or so.  But if
    // it were here then it would free up a number of flags for the
    // series, which would be helpful as they are necessary
    //
    REBCNT bias;

#if defined(__LP64__) || defined(__LLP64__)
    //
    // The Reb_Series_Dynamic is used in Reb_Series inside of a union with a
    // REBVAL.  On 64-bit machines this will leave one unused 32-bit slot
    // (which will couple with the previous REBCNT) and one naturally aligned
    // 64-bit pointer.  These could be used for some enhancement that would
    // be available per-dynamic-REBSER on 64-bit architectures.
    //
    REBCNT unused_32;
    void *unused_64;
#endif

};

struct Reb_Series {

    union {
        //
        // If the series does not fit into the REBSER node, then it must be
        // dynamically allocated.  This is the tracking structure for that
        // dynamic data allocation.
        //
        struct Reb_Series_Dynamic dynamic;

        // If not SERIES_FLAG_HAS_DYNAMIC, 0 or 1 length arrays can be held
        // directly in the series node.  The SERIES_FLAG_0_IS_FALSE bit is set
        // to zero and signals an IS_END().
        //
        // It is thus an "array" of effectively up to length two.  Either the
        // [0] full element is IS_END(), or the [0] element is another value
        // and the [1] element is read-only and passes IS_END() to terminate
        // (but can't have any other value written, as the info bits are
        // marked as unwritable by SERIES_FLAG_1_IS_FALSE...this protects the
        // rest of the bits in the debug build as it is checked whenver a
        // REBVAL tries to write a new header.)
        //
        struct Reb_Value values[1];
    } content;

    // `info` is the information about the series which needs to be known
    // even if it is not using a dynamic allocation.  So even if the alloc
    // size, length, and bias aren't relevant...the series flags need to
    // be known...including the flag of whether this is a dynamic series
    // node or not!
    //
    // The lowest 2 bits of info are required to be 0 when used with the trick
    // of implicitly terminating series data.  See SERIES_FLAG_0_IS_FALSE and
    // SERIES_FLAG_1_IS_FALSE for more information.
    //
    // !!! Only the low 32-bits are used on 64-bit platforms.  There could
    // be some interesting added caching feature or otherwise that would use
    // it, while not making any feature specifically require a 64-bit CPU.
    //
    struct Reb_Value_Header info;

    union {
        REBCNT len; // length of non-arrays when !SERIES_FLAG_HAS_DYNAMIC
        REBCNT size;    // used for vectors and bitsets
        REBSER *hashlist; // MAP datatype uses this
        REBARR *keylist; // used by CONTEXT
        struct {
            REBCNT wide:16;
            REBCNT high:16;
        } area;
        REBOOL negated; // for bitsets (can't be EXT flag on just one value)
    } misc;

#if !defined(NDEBUG)
    REBINT *guard; // intentionally alloc'd and freed for use by Panic_Series
#endif
};

// "Series Panics" will (hopefully) trigger an alert under memory tools
// like address sanitizer and valgrind that indicate the call stack at the
// moment of allocation of a series.  Then you should have TWO stacks: the
// one at the call of the Panic, and one where that series was alloc'd.
//
//    THIS FEATURE IS MENTIONED UP TOP BECAUSE IT IS VERY, VERY USEFUL!
//
#if !defined(NDEBUG)
    #define Panic_Series(s) \
        Panic_Series_Debug((s), __FILE__, __LINE__);
#endif

#define SER_REST(s)   ((s)->content.dynamic.rest)

#define SER_WIDE(s) \
    cast(REBYTE, ((s)->info.bits >> 16) & 0xff)

//
// Series flags
//
// !!! These should be renamed to the more readable SET_SER_FLAG, etc.
//

#define SET_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits |= (f)))

#define CLEAR_SER_FLAG(s,f) \
    cast(void, ((s)->info.bits &= ~(f)))

#define GET_SER_FLAG(s,f) \
    LOGICAL((s)->info.bits & (f))

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

#define SER_LEN(s) \
    (GET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC) \
        ? ((s)->content.dynamic.len + 0) \
        : GET_SER_FLAG((s), SERIES_FLAG_ARRAY) \
            ? (IS_END(&(s)->content.values[0]) ? 0 : 1) \
            : (s)->misc.len)

#define SET_SERIES_LEN(s,l) \
    (assert(!GET_SER_FLAG((s), SERIES_FLAG_STACK)), \
        GET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC) \
        ? ((s)->content.dynamic.len = (l)) \
        : GET_SER_FLAG((s), SERIES_FLAG_ARRAY) \
            ? 1337 /* trust the END marker? */ \
            : ((s)->misc.len = (l)))

// Raw access does not demand that the caller know the contained type.  So
// for instance a generic debugging routine might just want a byte pointer
// but have no element type pointer to pass in.
//
#define SER_DATA_RAW(s) \
    (GET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC) \
        ? ((s)->content.dynamic.data + 0) /* Lvalue! */ \
        : cast(REBYTE*, &(s)->content.values[0]))

#define SER_AT_RAW(s,i)      (SER_DATA_RAW(s) + (SER_WIDE(s) * (i)))

#define SER_SET_EXTERNAL_DATA(s,p) \
    (SET_SER_FLAG((s), SERIES_FLAG_HAS_DYNAMIC), \
        (s)->content.dynamic.data = cast(REBYTE*, (p)))

//
// In general, requesting a pointer into the series data requires passing in
// a type which is the correct size for the series.  A pointer is given back
// to that type.
//
// Note that series indexing in C is zero based.  So as far as SERIES is
// concerned, `SER_HEAD(t, s)` is the same as `SER_AT(t, s, 0)`
//
// !!! C++11 note: can't use cast helper here and have the const of an
// incoming REBVAL be ignored... must use `(old_cast)casting_style`
//

#define SER_AT(t,s,i) \
    (assert(SER_WIDE(s) == sizeof(t)), \
        (t*)(SER_DATA_RAW(s) + (sizeof(t) * (i))))

#define SER_HEAD(t,s) \
    SER_AT(t, (s), 0)

#define SER_TAIL(t,s) \
    SER_AT(t, (s), SER_LEN(s))

#define SER_LAST(t,s) \
    (assert(SER_LEN(s) != 0), SER_AT(t, (s), SER_LEN(s) - 1))

//
// Series size measurements:
//
// SER_TOTAL - bytes of memory allocated (including bias area)
// SER_SPACE - bytes of series (not including bias area)
// SER_USED - bytes being used, including terminator
//

#define SER_TOTAL(s) \
    ((SER_REST(s) + SER_BIAS(s)) * SER_WIDE(s))

#define SER_SPACE(s) \
    (SER_REST(s) * SER_WIDE(s))

#define SER_USED(s) \
    ((SER_LEN(s) + 1) * SER_WIDE(s))

// Returns space that a series has available (less terminator):
#define SER_FULL(s) (SER_LEN(s) + 1 >= SER_REST(s))
#define SER_AVAIL(s) (SER_REST(s) - (SER_LEN(s) + 1))
#define SER_FITS(s,n) ((SER_LEN(s) + (n) + 1) <= SER_REST(s))


#define Is_Array_Series(s) GET_SER_FLAG((s), SERIES_FLAG_ARRAY)

#define FAIL_IF_LOCKED_SERIES(s) \
    if (GET_SER_FLAG(s, SERIES_FLAG_LOCKED)) fail (Error(RE_LOCKED))

//
// Series external data accessible
//
#define SER_DATA_NOT_ACCESSIBLE(s) \
    (GET_SER_FLAG(s, SERIES_FLAG_EXTERNAL) \
     && !GET_SER_FLAG(s, SERIES_FLAG_ACCESSIBLE))
//
// Optimized expand when at tail (but, does not reterminate)
//

#define EXPAND_SERIES_TAIL(s,l) \
    do { \
        if (SER_FITS((s), (l))) (s)->content.dynamic.len += (l); \
        else Expand_Series((s), SER_LEN(s), (l)); \
    } while (0)

#define RESIZE_SERIES(s,l) \
    do { \
        (s)->content.dynamic.len = 0; \
        if (!SER_FITS((s), (l))) Expand_Series((s), SER_LEN(s), (l)); \
        (s)->content.dynamic.len = 0; \
    } while (0)

//
// Termination
//

#define RESET_SERIES(s) \
    ((s)->content.dynamic.len = 0, TERM_SERIES(s))

#define RESET_TAIL(s) \
    ((s)->content.dynamic.len = 0)

// Clear all and clear to tail:
//
#define CLEAR_SEQUENCE(s) \
    do { \
        assert(!Is_Array_Series(s)); \
        CLEAR(SER_DATA_RAW(s), SER_SPACE(s)); \
    } while (0)

#define TERM_SEQUENCE(s) \
    do { \
        assert(!Is_Array_Series(s)); \
        memset(SER_AT_RAW((s), SER_LEN(s)), 0, SER_WIDE(s)); \
    } while (0)

#ifdef NDEBUG
    #define ASSERT_SERIES_TERM(s) NOOP
#else
    #define ASSERT_SERIES_TERM(s) Assert_Series_Term_Core(s)
#endif

// Just a No-Op note to point out when a series may-or-may-not be terminated
//
#define NOTE_SERIES_MAYBE_TERM(s) NOOP

#ifdef NDEBUG
    #define ASSERT_SERIES(s) NOOP
#else
    #define ASSERT_SERIES(s) \
        do { \
            if (Is_Array_Series(s)) \
                Assert_Array_Core(AS_ARRAY(s)); \
            else \
                Assert_Series_Core(s); \
        } while (0)
#endif

// This is a rather expensive check for whether a REBVAL* lives anywhere in
// series memory, and hence may be relocated.  It can be useful for certain
// stress tests to try and catch cases where values that should not be
// living in a series are passed to some routines.
//
#ifdef NDEBUG
    #define ASSERT_NOT_IN_SERIES_DATA(p) NOOP
#else
    #define ASSERT_NOT_IN_SERIES_DATA(v) \
        Assert_Not_In_Series_Data_Debug(v, TRUE)
#endif


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

#define MANAGE_SERIES(s) \
    Manage_Series(s)

#define ENSURE_SERIES_MANAGED(s) \
    (GET_SER_FLAG((s), SERIES_FLAG_MANAGED) \
        ? NOOP \
        : MANAGE_SERIES(s))

#ifdef NDEBUG
    #define ASSERT_SERIES_MANAGED(s) \
        NOOP

    #define ASSERT_VALUE_MANAGED(v) \
        NOOP
#else
    #define ASSERT_SERIES_MANAGED(s) \
        do { \
            if (!GET_SER_FLAG((s), SERIES_FLAG_MANAGED)) \
                Panic_Series(s); \
        } while (0)

    #define ASSERT_VALUE_MANAGED(v) \
        assert(Is_Value_Managed((v), TRUE))
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

#define DROP_GUARD_SERIES(s) \
    do { \
        GC_Series_Guard->content.dynamic.len--; \
        assert((s) == \
            *SER_AT(REBSER*, GC_Series_Guard, SER_LEN(GC_Series_Guard)) \
        ); \
    } while (0)


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
#define BYTE_SIZE(s)    LOGICAL(((s)->info.bits) & (1 << 16))

//
// BIN_XXX: Binary or byte-size string seres macros
//

#define BIN_AT(s,n)     SER_AT(REBYTE, (s), (n))
#define BIN_HEAD(s)     SER_HEAD(REBYTE, (s))
#define BIN_TAIL(s)     SER_TAIL(REBYTE, (s))
#define BIN_LAST(s)     SER_LAST(REBYTE, (s))

#define BIN_LEN(s)      (assert(BYTE_SIZE(s)), SER_LEN(s))

#define SET_BIN_END(s,n) (*BIN_AT(s,n) = 0)

//
// UNI_XXX: Unicode string series macros
//

#define UNI_LEN(s) \
    (assert(SER_WIDE(s) == sizeof(REBUNI), SER_LEN(s))

#define SET_UNI_LEN(s,l) \
    (assert(SER_WIDE(s) == sizeof(REBUNI), SET_SERIES_LEN((s), (l)))

#define UNI_AT(s,n)     SER_AT(REBUNI, (s), (n))
#define UNI_HEAD(s)     SER_HEAD(REBUNI, (s))
#define UNI_TAIL(s)     SER_TAIL(REBUNI, (s))
#define UNI_LAST(s)     SER_LAST(REBUNI, (s))

#define UNI_TERM(s)     (*UNI_TAIL(s) = 0)
#define UNI_RESET(s)    (UNI_HEAD(s)[(s)->tail = 0] = 0)

//
// Get a char, from either byte or unicode string:
//

#define GET_ANY_CHAR(s,n) \
    cast(REBUNI, BYTE_SIZE(s) ? BIN_HEAD(s)[n] : UNI_HEAD(s)[n])

#define SET_ANY_CHAR(s,n,c) \
    (BYTE_SIZE(s) \
        ? (BIN_HEAD(s)[n]=(cast(REBYTE, (c)))) \
        : (UNI_HEAD(s)[n]=(cast(REBUNI, (c)))) \
    )


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
#define AS_ARRAY(s)         (cast(REBARR*, (s)))
#define ARR_SERIES(a)     (&(a)->series)

// HEAD, TAIL, and LAST refer to specific value pointers in the array.  An
// empty array should have an END marker in its head slot, and since it has
// no last value then ARR_LAST should not be called (this is checked in
// debug builds).  A fully constructed array should always have an END
// marker in its last slot, which is one past the last position that is
// valid for writing a full REBVAL.
//

#define ARR_AT(a, n)      SER_AT(REBVAL, ARR_SERIES(a), (n))
#define ARR_HEAD(a)       SER_HEAD(REBVAL, ARR_SERIES(a))
#define ARR_TAIL(a)       SER_TAIL(REBVAL, ARR_SERIES(a))
#define ARR_LAST(a)       SER_LAST(REBVAL, ARR_SERIES(a))

// As with an ordinary REBSER, a REBARR has separate management of its length
// and its terminator.  Many routines seek to control these independently for
// performance reasons (for better or worse).
//
#define ARR_LEN(a) \
    (assert(Is_Array_Series(ARR_SERIES(a))), SER_LEN(ARR_SERIES(a)))

#define SET_ARRAY_LEN(a,l) \
    (assert(Is_Array_Series(ARR_SERIES(a))), \
        SET_SERIES_LEN(ARR_SERIES(a), (l)))

//
// !!! Write more about termination in series documentation.
//

#define TERM_ARRAY(a) \
    SET_END(ARR_TAIL(a))

#define RESET_ARRAY(a) \
    (SET_ARRAY_LEN((a), 0), TERM_ARRAY(a))

#define TERM_SERIES(s) \
    Is_Array_Series(s) \
        ? (void)TERM_ARRAY(AS_ARRAY(s)) \
        : (void)memset(SER_AT_RAW(s, SER_LEN(s)), 0, SER_WIDE(s))

// Setting and getting array flags is common enough to want a macro for it
// vs. having to extract the ARR_SERIES to do it each time.
//
#define SET_ARR_FLAG(a,f)     SET_SER_FLAG(ARR_SERIES(a), (f))
#define CLEAR_ARR_FLAG(a,f)     CLEAR_SER_FLAG(ARR_SERIES(a), (f))
#define GET_ARR_FLAG(a,f)     GET_SER_FLAG(ARR_SERIES(a), (f))

#define FAIL_IF_LOCKED_ARRAY(a) \
    FAIL_IF_LOCKED_SERIES(ARR_SERIES(a))

#define PUSH_GUARD_ARRAY(a) \
    PUSH_GUARD_SERIES(ARR_SERIES(a))

#define DROP_GUARD_ARRAY(a) \
    DROP_GUARD_SERIES(ARR_SERIES(a))

#define MANAGE_ARRAY(array) \
    MANAGE_SERIES(ARR_SERIES(array))

#define ENSURE_ARRAY_MANAGED(array) \
    ENSURE_SERIES_MANAGED(ARR_SERIES(array))

#define Append_Value(a,v) \
    (*Alloc_Tail_Array((a)) = *(v), NOOP)

#define Copy_Values_Len_Shallow(v,l) \
    Copy_Values_Len_Extra_Shallow((v), (l), 0)

#define Copy_Array_Shallow(a) \
    Copy_Array_At_Shallow((a), 0)

#define Copy_Array_Deep_Managed(a) \
    Copy_Array_At_Extra_Deep_Managed((a), 0, 0)

#define Copy_Array_At_Deep_Managed(a,i) \
    Copy_Array_At_Extra_Deep_Managed((a), (i), 0)

#define Copy_Array_At_Shallow(a,i) \
    Copy_Array_At_Extra_Shallow((a), (i), 0)

#define Copy_Array_Extra_Shallow(a,e) \
    Copy_Array_At_Extra_Shallow((a), 0, (e))

#define Free_Array(a) \
    Free_Series(ARR_SERIES(a))

#ifdef NDEBUG
    #define ASSERT_ARRAY(s) cast(void, 0)

    #define ASSERT_ARRAY_MANAGED(array) \
        NOOP
#else
    #define ASSERT_ARRAY(s) Assert_Array_Core(s)

    #define ASSERT_ARRAY_MANAGED(array) \
        ASSERT_SERIES_MANAGED(ARR_SERIES(array))

    #define Panic_Array(a) \
        Panic_Series(ARR_SERIES(a))

    #define Debug_Array(a) \
        Debug_Series(ARR_SERIES(a))
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
    struct Reb_Array varlist; // keylist is held in ->misc.keylist
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

#define CTX_VARLIST(c) \
    (&(c)->varlist)

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

#define CTX_KEYLIST(c) \
    (ARR_SERIES(CTX_VARLIST(c))->misc.keylist)

#define INIT_CONTEXT_KEYLIST(c,k) \
    (ARR_SERIES(CTX_VARLIST(c))->misc.keylist = (k))

// The keys and vars are accessed by positive integers starting at 1.  If
// indexed access is used then the debug build will check to be sure that
// the indexing is legal.  To get a pointer to the first key or value
// regardless of length (e.g. will be an END if 0 keys/vars) use HEAD
//
#define CTX_KEYS_HEAD(c)    ARR_AT(CTX_KEYLIST(c), 1)
#define CTX_VARS_HEAD(c) \
    (GET_ARR_FLAG(CTX_VARLIST(c), SERIES_FLAG_STACK) \
        ? VAL_CONTEXT_STACKVARS(CTX_VALUE(c)) \
        : ARR_AT(CTX_VARLIST(c), 1))

#ifdef NDEBUG
    #define CTX_KEY(c,n)    (CTX_KEYS_HEAD(c) + (n) - 1)
    #define CTX_VAR(c,n)    (CTX_VARS_HEAD(c) + (n) - 1)
#else
    #define CTX_KEY(c,n)    CTX_KEY_Debug((c), (n))
    #define CTX_VAR(c,n)    CTX_VAR_Debug((c), (n))
#endif
#define CTX_KEY_SYM(c,n)    VAL_TYPESET_SYM(CTX_KEY((c), (n)))
#define CTX_KEY_CANON(c,n)  VAL_TYPESET_CANON(CTX_KEY((c), (n)))

// There may not be any dynamic or stack allocation available for a stack
// allocated context, and in that case it will have to come out of the
// REBSER node data itself.
//
#define CTX_VALUE(c) \
    (GET_ARR_FLAG(CTX_VARLIST(c), SERIES_FLAG_STACK) \
        ? &ARR_SERIES(CTX_VARLIST(c))->content.values[0] \
        : ARR_HEAD(CTX_VARLIST(c)))

// Navigate from context to context components.  Note that the context's
// "length" does not count the [0] cell of either the varlist or the keylist.
// Hence it must subtract 1.  Internally to the context building code, the
// real length of the two series must be accounted for...so the 1 gets put
// back in, but most clients are only interested in the number of keys/values
// (and getting an answer for the length back that was the same as the length
// requested in context creation).
//
#define CTX_LEN(c)          (ARR_LEN(CTX_KEYLIST(c)) - 1)
#define CTX_ROOTKEY(c)      ARR_HEAD(CTX_KEYLIST(c))
#define CTX_TYPE(c)         VAL_TYPE(CTX_VALUE(c))

#define INIT_CONTEXT_SPEC(c,s) \
    (assert(!IS_FRAME(CTX_VALUE(c))), \
        VAL_CONTEXT_SPEC(CTX_VALUE(c)) = (s))

#define CTX_SPEC(c) \
    (VAL_CONTEXT_SPEC(CTX_VALUE(c)) + 0)

#define INIT_CONTEXT_FUNC(c,f) \
    (assert(IS_FRAME(CTX_VALUE(c))), \
        VAL_CONTEXT_FUNC(CTX_VALUE(c)) = (f))

#define CTX_FUNC(c) \
    (VAL_CONTEXT_FUNC(CTX_VALUE(c)) + 0)

#define CTX_STACKVARS(c)    VAL_CONTEXT_STACKVARS(CTX_VALUE(c))

#define FAIL_IF_LOCKED_CONTEXT(c) \
    FAIL_IF_LOCKED_ARRAY(CTX_VARLIST(c))

#define FREE_CONTEXT(c) \
    do { \
        Free_Array(CTX_KEYLIST(c)); \
        Free_Array(CTX_VARLIST(c)); \
    } while (0)

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
    #define AS_FUNC(s)     cast(REBFUN*, (s))
#else
    // Put a debug version here that asserts.
    #define AS_FUNC(s)     cast(REBFUN*, (s))
#endif

#define FUNC_PARAMLIST(f)       (&(f)->paramlist)

#define FUNC_NUM_PARAMS(f)      (ARR_LEN(FUNC_PARAMLIST(f)) - 1)
#define FUNC_PARAMS_HEAD(f)     ARR_AT(FUNC_PARAMLIST(f), 1)
#ifdef NDEBUG
    #define FUNC_PARAM(f,n)     ARR_AT(FUNC_PARAMLIST(f), (n))
#else
    #define FUNC_PARAM(f,n)     FUNC_PARAM_Debug((f), (n))
#endif
#define FUNC_PARAM_SYM(f,n)     VAL_TYPESET_SYM(FUNC_PARAM((f), (n)))

#define FUNC_VALUE(f)           ARR_HEAD(FUNC_PARAMLIST(f))
#define FUNC_SPEC(f)            (FUNC_VALUE(f)->payload.any_function.spec)
#define FUNC_CODE(f)            (FUNC_VALUE(f)->payload.any_function.impl.code)
#define FUNC_BODY(f)            (FUNC_VALUE(f)->payload.any_function.impl.body)
#define FUNC_ACT(f)             (FUNC_VALUE(f)->payload.any_function.impl.act)
#define FUNC_INFO(f)            (FUNC_VALUE(f)->payload.any_function.impl.info)


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

struct Reb_Map {
    struct Reb_Array pairlist; // hashlist is held in ->misc.hashlist
};

#define MAP_PAIRLIST(m)         (&(m)->pairlist)
#define MAP_HASHLIST(m)         (ARR_SERIES(&(m)->pairlist)->misc.hashlist)
#define MAP_HASHES(m)           SER_HEAD(MAP_HASHLIST(m))

// !!! Should there be a MAP_LEN()?  Current implementation has NONE in
// slots that are unused, so can give a deceptive number.  But so can
// objects with hidden fields, locals in paramlists, etc.

#define AS_MAP(s)               (*cast(REBMAP**, &(s)))

#ifdef NDEBUG
    #define VAL_MAP(v)          AS_MAP(VAL_ARRAY(v))
#else
    #define VAL_MAP(v)          (*VAL_MAP_Ptr_Debug(v))
#endif
