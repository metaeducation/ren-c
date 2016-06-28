//
//  File: %sys-rebser.h
//  Summary: {Structure Definition for Series (REBSER)}
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
// This contains the struct definition for a REBSER (`struct Reb_Series`)
// It is a small descriptor for a series, and contains information
// about its content and flags.  Every string and block in REBOL has one,
// and the code implementing them is reused in many places that Rebol needs
// a general-purpose dynamically growing structure.
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
// space in the REBSER to track a dynamic allocation, the data can be placed
// directly in the node itself.  By leveraging the trick that the low bit of
// an end marker is all that's needed to terminate arrays, the `info` bits
// internal to the structure can implicitly terminate the array.  The capacity
// is enough to store a single REBVAL so that length 0 or 1 series can be
// represented, along with two extra pointer-sized items (a `link` and `misc`).
//
// Additionally, if the data requirements are such that exactly two REBVALs
// of space are needed, another trick is used.  Implicit termination in that
// case cannot be accomplished inside the REBSER (as it is exactly two
// REBVALs in size).  Instead, REBSERs with such a requirement are not
// located in the pool before another one with the same requirement.  By
// interleaving them with ordinary series that have bits to spare for the
// terminator signal, they can fit.  See REBSER_REBVAL_FLAG_MARK, etc.
//
// Notes:
//
// * For the forward declarations of series subclasses, see %reb-defs.h
//
// * Because a series contains a union member that embeds a REBVAL directly,
//   `struct Reb_Value` must be fully defined before this file can compile.
//   Hence %sys-rebval.h must already be included.
//
// * For the API of operations available on REBSER types, see %sys-series.h
//
// * REBARR is a series that contains Rebol values (REBVALs).  It has many
//   concerns specific to special treatment and handling, in interaction with
//   the garbage collector as well as handling "relative vs specific" values.
//
// * Several related types (REBFUN for function, REBCTX for context) are
//   actually stylized arrays.  They are laid out with special values in their
//   content (e.g. at the [0] index), or by links to other series in their
//   `->misc` field of the REBSER node.  Hence series are the basic building
//   blocks of nearly all variable-size structures in the system.
//

// Series Flags
//
enum {
    // `SERIES_FLAG_0_IS_FALSE` represents the lowest bit and should always
    // be set to zero.  This is because it means that when Reb_Series_Content
    // is interpreted as a REBVAL's worth of data, then the info bits are in
    // a location where they do double-duty serving as an END marker.  For a
    // description of the method see notes on NOT_END_MASK.
    //
    SERIES_FLAG_0_IS_FALSE = 1 << 0,

    // `SERIES_FLAG_1_IS_FALSE` is the second lowest bit, and is set to zero
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

    // `SERIES_FLAG_STRING` identifies that this series holds a string, which
    // is important to the GC in order to successfully
    //
    // !!! While there are advantages to having symbols be a compatible shape
    // with other series for clients convenience, it may be that using a
    // different memory pool for tracking some bits make sense.  For instance,
    // knowing the series is a string is important at GC time...to clean up
    // aliases and adjust canons.
    //
    SERIES_FLAG_STRING = 1 << 3,

    // `STRING_FLAG_CANON` is used to indicate when a REBSTR series represents
    // the canon form of a word.  This doesn't mean anything special about
    // the case of its letters--just that it was loaded first.  A canon
    // string is unique because it does not need to store a pointer to its
    // canon form, so it can use the REBSER.misc field for the purpose of
    // holding an index during binding.
    //
    STRING_FLAG_CANON = 1 << 4,

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

    // `ARRAY_FLAG_CONTEXT_VARLIST` indicates this series represents the
    // "varlist" of a context.  A second series can be reached from it via
    // the `->misc` field in the series node, which is a second array known
    // as a "keylist".
    //
    // See notes on REBCTX for further details about what a context is.
    //
    ARRAY_FLAG_CONTEXT_VARLIST = 1 << 6,

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
    // used for STRUCT! and to note when a CONTEXT_FLAG_STACK series has its
    // stack level popped (there's no data to lookup for words bound to it)
    //
    SERIES_FLAG_ACCESSIBLE = 1 << 11,

    // `CONTEXT_FLAG_STACK` indicates that varlist data lives on the stack.
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
    CONTEXT_FLAG_STACK = 1 << 12,

    // `KEYLIST_FLAG_SHARED` is indicated on the keylist array of a context
    // when that same array is the keylist for another object.  If this flag
    // is set, then modifying an object using that keylist (such as by adding
    // a key/value pair) will require that object to make its own copy.
    //
    // (Note: This flag did not exist in R3-Alpha, so all expansions would
    // copy--even if expanding the same object by 1 item 100 times with no
    // sharing of the keylist.  That would make 100 copies of an arbitrary
    // long keylist that the GC would have to clean up.)
    //
    KEYLIST_FLAG_SHARED = 1 << 13,

    // `ARRAY_FLAG_VOIDS_LEGAL` identifies arrays in which it is legal to
    // have void elements.  This is used for instance on reified C va_list()s
    // which were being used for unevaluated applies.  When those va_lists
    // need to be put into arrays for the purposes of GC protection, they may
    // contain voids which they need to track.
    //
    // Note: ARRAY_FLAG_CONTEXT_VARLIST also implies legality of voids, which
    // are used to represent unset variables.
    //
    ARRAY_FLAG_VOIDS_LEGAL = 1 << 14,

    // `SERIES_FLAG_LEGACY` is a flag which is marked at the root set of the
    // body of legacy functions.  It can be used in a dynamic examination of
    // a call to see if it "originates from legacy code".  This is a vague
    // concept given the ability to create blocks and run them--so functions
    // like COPY would have to propagate the flag to make it "more accurate".
    // But it's good enough for casual compatibility in many cases.
    //
#if !defined NDEBUG
    SERIES_FLAG_LEGACY = 1 << 15,
#endif

    SERIES_FLAG_NO_COMMA_NEEDED = 0 // solves dangling comma from above
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
    // a full REBCNT but was limited in range to 16 bits or so.  This means
    // 16 info bits are likely available if needed for dynamic series.
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

    // The low 2 bits in the header must be 00 if this is an "ordinary" REBSER
    // node.  This allows such nodes to implicitly terminate a "doubular"
    // REBSER node, that is being used as storage for exactly 2 REBVALs.
    // As long as there aren't two of those REBSERs sequentially in the pool,
    // an unused node or a used ordinary one can terminate it.
    //
    // The other bit that is checked in the header is the USED bit, which is
    // bit #9.  This is set on all REBVALs and also in END marking headers,
    // and should be set in used series nodes.
    //
    // The remaining bits are free, and used to hold SYM values for those
    // words that have them.
    //
    struct Reb_Value_Header header;

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
        RELVAL values[1];
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

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this series would want to be able
    // to see.
    //
    union {
        REBSER *hashlist; // MAP datatype uses this
        REBARR *keylist; // used by CONTEXT
        REBARR *subfeed; // for *non-frame* VARARGS! ("array1") shared feed
        REBSER *schema; // STRUCT uses this (parallels object's keylist)
        REBCTX *meta; // paramlists and keylists can store a "meta" object
        REBSTR *synonym; // circularly linked list of othEr-CaSed string forms
    } link;

    // The `misc` field is an extra pointer-sized piece of data which is
    // resident in the series node, and hence visible to all REBVALs that
    // might be referring to the series.
    //
    union {
        REBNAT dispatcher; // native dispatcher code, see Reb_Function's body
        REBCNT size;    // used for vectors and bitsets
        struct {
            REBCNT wide:16;
            REBCNT high:16;
        } area;
        REBOOL negated; // for bitsets (must be shared, can't be in REBVAL)
        REBFUN *underlying; // specialization -or- final underlying function
        struct Reb_Frame *f; // for a FRAME! series, the call frame (or NULL)
        void *fd; // file descriptor for library
        REBSTR *canon; // canon cased form of this symbol (if not canon)
        struct {
            REBINT high:16;
            REBINT low:16;
        } bind_index; // canon words hold index for binding--demo sharing 2
    } misc;

#if !defined(NDEBUG)
    int *guard; // intentionally alloc'd and freed for use by Panic_Series
    REBUPT do_count; // also maintains sizeof(REBSER) % sizeof(REBI64) == 0
#endif
};
