//
//  File: %sys-rebser.h
//  Summary: {Structure Definition for Series (REBSER)}
//  Project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  Homepage: https://github.com/metaeducation/ren-c/
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Copyright 2012 REBOL Technologies
// Copyright 2012-2017 Rebol Open Source Contributors
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
// This contains the struct definition for the "REBSER" struct Reb_Series.
// It is a small-ish descriptor for a series (though if the amount of data
// in the series is small enough, it is embedded into the structure itself.)
//
// Every string, block, path, etc. in Rebol has a REBSER.  The implementation
// of them is reused in many places where Rebol needs a general-purpose
// dynamically growing structure.  It is also used for fixed size structures
// which would like to participate in garbage collection.
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
// managed series *must not be held onto across evaluations*, without
// special protection or accomodation.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
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


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES <<HEADER>> FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Series have two places to store bits...in the "header" and in the "info".
// The following are the SERIES_FLAG_XXX that are used in the header, while
// the SERIES_INFO_XXX flags will be found in the info.
//
// As a general rule for choosing which place to put a bit, if it may be
// interesting to test/set multiple bits at the same time, then they should
// be in the same flag group.  Also, SERIES_FLAG_XXX are passed to the
// Make_Series() function, so anything that controls series creation is best
// put in there.
//
// !!! Perhaps things that don't change for the lifetime of the series should
// also prefer the header vs. info?  Such separation might help with caching.
//

#define SERIES_MASK_NONE \
    0 // helps locate places that want to say "no flags"


//=//// ARRAY_FLAG_VOIDS_LEGAL ////////////////////////////////////////////=//
//
// Identifies arrays in which it is legal to have void elements.  This is true
// for instance on reified C va_list()s which were being used for unevaluated
// applies (like R3-Alpha's APPLY/ONLY).  When those va_lists need to be put
// into arrays for the purposes of GC protection, they may contain voids which
// they need to track.
//
// Note: ARRAY_FLAG_VARLIST also implies legality of voids, which
// are used to represent unset variables.
//
#define ARRAY_FLAG_VOIDS_LEGAL \
    NODE_FLAG_SPECIAL


//=//// SERIES_FLAG_FIXED_SIZE ////////////////////////////////////////////=//
//
// This means a series cannot be expanded or contracted.  Values within the
// series are still writable (assuming it isn't otherwise locked).
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
// changing size (such as memory compaction).  For now, just make the two
// equivalent but let the callsite distinguish the intent.
//
#define SERIES_FLAG_FIXED_SIZE \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 0)

#define SERIES_FLAG_DONT_RELOCATE SERIES_FLAG_FIXED_SIZE


//=//// SERIES_FLAG_FILE_LINE /////////////////////////////////////////////=//
//
// The Reb_Series node has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the keylist for an object,
// the C code that runs as the dispatcher for a function, etc.)  But for
// regular source series, they can be used to store the filename and line
// number, if applicable.
//
#define SERIES_FLAG_FILE_LINE \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 1)


//=//// SERIES_FLAG_UTF8_STRING ///////////////////////////////////////////=//
//
// Indicates the series holds a UTF-8 encoded string.
//
// !!! Currently this is only used to store ANY-WORD! symbols, which are
// read-only and cannot be indexed into, e.g. with `next 'foo`.  This is
// because UTF-8 characters are encoded at variable sizes, and the series
// indexing does not support that at this time.  However, it would be nice
// if a way could be figured out to unify ANY-STRING! with ANY-WORD! somehow
// in order to implement the "UTF-8 Everywhere" manifesto:
//
// http://utf8everywhere.org/
//
#define SERIES_FLAG_UTF8_STRING \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 2)


//=//// SERIES_FLAG_POWER_OF_2 ////////////////////////////////////////////=//
//
// This is set when an allocation size was rounded to a power of 2.  The bit
// was introduced in Ren-C when accounting was added to make sure the system's
// notion of how much memory allocation was outstanding would balance out to
// zero by the time of exiting the interpreter.
//
// The problem was that the allocation size was measured in terms of the
// number of elements in the series.  If the elements themselves were not the
// size of a power of 2, then to get an even power-of-2 size of memory
// allocated, the memory block would not be an even multiple of the element
// size.  So rather than track the "actual" memory allocation size as a 32-bit
// number, a single bit flag remembering that the allocation was a power of 2
// was enough to recreate the number to balance accounting at free time.
//
// !!! The original code which created series with items which were not a
// width of a power of 2 was in the FFI.  It has been rewritten to not use
// such custom structures, but the support for this remains in case there
// was a good reason to have a non-power-of-2 size in the future.
//
// !!! ...but rationale for why series were ever allocated to a power of 2
// should be revisited.  Current conventional wisdom suggests that asking
// for the amount of memory you need and not using powers of 2 is
// generally a better idea:
//
// http://stackoverflow.com/questions/3190146/
//
#define SERIES_FLAG_POWER_OF_2 \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 3)


//=//// SERIES_FLAG_ARRAY /////////////////////////////////////////////////=//
//
// Indicates that this is a series of REBVAL value cells, and suitable for
// using as the payload of an ANY-ARRAY! value.  When a series carries this
// bit, then if it is also NODE_FLAG_MANAGED the garbage ollector will process
// its transitive closure to make sure all the values it contains (and the
// values its references contain) do not have series GC'd out from under them.
//
// Note: R3-Alpha used `SER_WIDE(s) == sizeof(REBVAL)` as the test for if
// something was an array.  But this allows creation of series that have
// items which are incidentally the size of a REBVAL, but not actually arrays.
//
#define SERIES_FLAG_ARRAY \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 4)


//=//// ARRAY_FLAG_PARAMLIST //////////////////////////////////////////////=//
//
// ARRAY_FLAG_PARAMLIST indicates the array is the parameter list of a
// FUNCTION! (the first element will be a canon value of the function)
//
#define ARRAY_FLAG_PARAMLIST \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 5)


//=//// ARRAY_FLAG_VARLIST ////////////////////////////////////////////////=//
//
// This indicates this series represents the "varlist" of a context (which is
// interchangeable with the identity of the varlist itself).  A second series
// can be reached from it via the `->misc` field in the series node, which is
// a second array known as a "keylist".
//
// See notes on REBCTX for further details about what a context is.
//
#define ARRAY_FLAG_VARLIST \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 6)


//=//// ARRAY_FLAG_PAIRLIST ///////////////////////////////////////////////=//
//
// Indicates that this series represents the "pairlist" of a map, so the
// series also has a hashlist linked to in the series node.
//
#define ARRAY_FLAG_PAIRLIST \
    FLAGIT_LEFT(GENERAL_SERIES_BIT + 7)


// ^-- STOP AT FLAGIT_LEFT(15) --^
//
// The rightmost 16 bits of the series flags are used to store an arbitrary
// per-series-type 16 bit number.  Right now, that's used by the string series
// to save their REBSYM id integer(if they have one).  Note that the flags
// are flattened in kind of a wasteful way...some are mutually exclusive and
// could use the same bit, if needed.
//
#ifdef CPLUSPLUS_11
    static_assert(GENERAL_SERIES_BIT + 7 < 16, "SERIES_FLAG_XXX too high");
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES <<INFO>> BITS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See remarks above about the two places where series store bits.  These
// are the info bits, which are more likely to be changed over the lifetime
// of the series--defaulting to FALSE.
//
// See Init_Endlike_Header() for why the bits are chosen the way they are.
// 4 are reserved, this means that the Reb_Series->info field can function as
// an implicit END for Reb_Series->content, as well as be distinguished from
// a REBVAL*, a REBSER*, or a UTF8 string.
//
// Review: Due to the Init_Endlike_Header trick, it might be safer with the
// aliasing to make the info contain the properties that *don't* change over
// the lifetime of the series.  (?)
//

#define SERIES_INFO_0_IS_TRUE FLAGIT_LEFT(0) // NODE_FLAG_NODE
#define SERIES_INFO_1_IS_FALSE FLAGIT_LEFT(1) // NOT(NODE_FLAG_FREE)


//=//// SERIES_INFO_HAS_DYNAMIC ///////////////////////////////////////////=//
//
// Indicates that this series has a dynamically allocated portion.  If it does
// not, then its data pointer is the address of the embedded value inside of
// it, and that the length is stored in the rightmost byte of the header
// bits (of which this is one bit).
//
// This bit will be flipped if a series grows.  (In the future it should also
// be flipped when the series shrinks, but no shrinking in the GC yet.)
//
// Note: Same bit as NODE_FLAG_MANAGED, should not be relevant.
//
#define SERIES_INFO_HAS_DYNAMIC \
    FLAGIT_LEFT(2)


//=//// SERIES_INFO_BLACK /////////////////////////////////////////////////=//
//
// This is a generic bit for the "coloring API", e.g. Is_Series_Black(),
// Flip_Series_White(), etc.  These let native routines engage in marking
// and unmarking nodes without potentially wrecking the garbage collector by
// reusing NODE_FLAG_MARKED.  Purposes could be for recursion protection or
// other features, to avoid having to make a map from REBSER to REBOOL.
//
// Note: Same bit as NODE_FLAG_MARKED, interesting but irrelevant.
//
#define SERIES_INFO_BLACK \
    FLAGIT_LEFT(3)


#define SERIES_INFO_4_IS_TRUE FLAGIT_LEFT(4) // NODE_FLAG_END


//=//// SERIES_INFO_PROTECTED /////////////////////////////////////////////=//
//
// This indicates that the user had a tempoary desire to protect a series
// size or values from modification.  It is the usermode analogue of
// SERIES_INFO_FROZEN, but can be reversed.
//
// Note: There is a feature in PROTECT (CELL_FLAG_PROTECTED) which protects
// a certain variable in a context from being changed.  It is similar, but
// distinct.  SERIES_INFO_PROTECTED is a protection on a series itself--which
// ends up affecting all values with that series in the payload.
//
// Note: Same bit as NODE_FLAG_ROOT, should not be relevant.
//
#define SERIES_INFO_PROTECTED \
    FLAGIT_LEFT(5)


//=//// SERIES_INFO_HOLD //////////////////////////////////////////////////=//
//
// Set in the header whenever some stack-based operation wants a temporary
// hold on a series, to give it a protected state.  This will happen with a
// DO, or PARSE, or enumerations.  Even REMOVE-EACH will transition the series
// it is operating on into a HOLD state while the removal signals are being
// gathered, and apply all the removals at once before releasing the hold.
//
// It will be released when the execution is finished, which distinguishes it
// from SERIES_INFO_FROZEN, which will never be reset, as long as it lives...
//
// Note: Same bit as NODE_FLAG_SPECIAL, should not be relevant.
// 
#define SERIES_INFO_HOLD \
    FLAGIT_LEFT(6)


#define SERIES_INFO_7_IS_FALSE FLAGIT_LEFT(7) // NOT(NODE_FLAG_CELL)


//=//// SERIES_INFO_FROZEN ////////////////////////////////////////////////=//
//
// Indicates that the length or values cannot be modified...ever.  It has been
// locked and will never be released from that state for its lifetime, and if
// it's an array then everything referenced beneath it is also frozen.  This
// means that if a read-only copy of it is required, no copy needs to be made.
//
// (Contrast this with the temporary condition like caused by something
// like REBSER_FLAG_RUNNING or REBSER_FLAG_PROTECTED.)
//
// Note: This and the other read-only series checks are honored by some layers
// of abstraction, but if one manages to get a raw non-const pointer into a
// value in the series data...then by that point it cannot be enforced.
//
#define SERIES_INFO_FROZEN \
    FLAGIT_LEFT(8)


//=//// SERIES_INFO_INACCESSIBLE //////////////////////////////////////////=//
//
// Currently this used to note when a CONTEXT_INFO_STACK series has had its
// stack level popped (there's no data to lookup for words bound to it).
//
// !!! This is currently redundant with checking if a CONTEXT_INFO_STACK
// series has its `misc.f` (REBFRM) nulled out, but it means both can be
// tested at the same time with a single bit.
//
// !!! It is conceivable that there would be other cases besides frames that
// would want to expire their contents, and it's also conceivable that frames
// might want to *half* expire their contents (e.g. have a hybrid of both
// stack and dynamic values+locals).  These are potential things to look at.
//
#define SERIES_INFO_INACCESSIBLE \
    FLAGIT_LEFT(9)


//=//// STRING_INFO_CANON /////////////////////////////////////////////////=//
//
// This is used to indicate when a SERIES_FLAG_UTF8_STRING series represents
// the canon form of a word.  This doesn't mean anything special about the
// case of its letters--just that it was loaded first.  Canon forms can be
// GC'd and then delegate the job of being canon to another spelling.
//
// A canon string is unique because it does not need to store a pointer to
// its canon form.  So it can use the REBSER.misc field for the purpose of
// holding an index during binding.
//
#define STRING_INFO_CANON \
    FLAGIT_LEFT(10)


//=//// SERIES_INFO_SHARED_KEYLIST ////////////////////////////////////////=//
//
// This is indicated on the keylist array of a context when that same array
// is the keylist for another object.  If this flag is set, then modifying an
// object using that keylist (such as by adding a key/value pair) will require
// that object to make its own copy.
//
// Note: This flag did not exist in R3-Alpha, so all expansions would copy--
// even if expanding the same object by 1 item 100 times with no sharing of
// the keylist.  That would make 100 copies of an arbitrary long keylist that
// the GC would have to clean up.
//
#define SERIES_INFO_SHARED_KEYLIST \
    FLAGIT_LEFT(11)


//=//// CONTEXT_INFO_STACK ////////////////////////////////////////////////=//
//
// This indicates that a context's varlist data lives on the stack.  That
// means that when the function terminates, the data will no longer be
// accessible (so SERIES_INFO_INACCESSIBLE will be true).
//
// !!! Ultimately this flag may be unnecessary because stack-based and
// dynamic series will "hybridize" so that they may have some stack
// fields and some fields in dynamic memory.  For now it's a good sanity
// check that things which should only happen to stack contexts (like becoming
// inaccessible) are checked against this flag.
//
#define CONTEXT_INFO_STACK \
    FLAGIT_LEFT(12)


// ^-- STOP AT FLAGIT_LEFT(15) --^
//
// The rightmost 16 bits of the series info is used to store an 8 bit length
// for non-dynamic series and an 8 bit width of the series.  So the info
// flags need to stop at FLAGIT_LEFT(15).
//
#ifdef CPLUSPLUS_11
    static_assert(13 < 16, "SERIES_INFO_XXX too high");
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES NODE ("REBSER") STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A REBSER node is the size of two REBVALs, and there are 3 basic layouts
// which can be overlaid inside the node:
//
//      Dynamic: [header [allocation tracking] info link misc]
//     Singular: [header [REBVAL cell] info link misc]
//      Pairing: [[REBVAL cell] [REBVAL cell]]
//
// `info` is not the start of a "Rebol Node" (REBNODE, e.g. either a REBSER or
// a REBVAL cell).  But in the singular case it is positioned right where
// the next cell after the embedded cell *would* be.  Hence the bit in the
// info corresponding to NODE_FLAG_END is set, making it conform to the
// "terminating array" pattern.  To lower the risk of this implicit terminator
// being accidentally overwritten (which would corrupt link and misc), the
// bit corresponding to NODE_FLAG_CELL is clear.
//
// Singulars have widespread applications in the system, notably the
// efficient implementation of FRAME!.  They also narrow the gap in overhead
// between COMPOSE [A (B) C] vs. REDUCE ['A B 'C] such that the memory cost
// of the array is nearly the same as just having another value in the array.
//
// Pair REBSERs are allocated from the REBSER pool instead of their own to
// help exchange a common "currency" of allocation size more efficiently.
// They are planned for use in the PAIR! and MAP! datatypes, and anticipated
// to play a crucial part in the API--allowing a persistent handle for a
// GC'able REBVAL and associated "meta" value (which can be used for
// reference counting or other tracking.)
//
// Most of the time, code does not need to be concerned about distinguishing
// Pair from the Dynamic and Singular layouts--because it already knows
// which kind it has.  Only the GC needs to be concerned when marking
// and sweeping.
//

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


union Reb_Series_Content {
    //
    // If the series does not fit into the REBSER node, then it must be
    // dynamically allocated.  This is the tracking structure for that
    // dynamic data allocation.
    //
    struct Reb_Series_Dynamic dynamic;

    // If not SERIES_INFO_HAS_DYNAMIC, 0 or 1 length arrays can be held in
    // the series node.  This trick is accomplished via "implicit termination"
    // in the ->info bits that come directly after ->content.
    //
    // (See NODE_FLAG_END and NODE_FLAG_CELL for how this is done.)
    //
    // We do not use a RELVAL here, because it would rule out making simple
    // assignments of one series's content to another, as the assignment
    // operator is disabled in the C++ build.  But the value may be relative
    // or specific.
    //
    struct Reb_Cell values[1];
};


union Reb_Series_Link {
    //
    // If you assign one member in a union and read from another, then that's
    // technically undefined behavior.  But this field is used as the one
    // that is "trashed" in the debug build when the series is created, and
    // hopefully it will lead to the other fields reading garbage (vs. zero)
    //
#if !defined(NDEBUG)
    void *trash;
#endif

    // Ordinary source series use their ->link field to point to an
    // interned file name string from which the code was loaded.  If a
    // series was not created from a file, then the information from the
    // source that was running at the time is propagated into the new
    // second-generation series.
    //
    REBSTR *file;

    // REBCTX types use this field of their varlist (which is the identity of
    // an ANY-CONTEXT!) to find their "keylist".  It is stored in the REBSER
    // node of the varlist REBARR vs. in the REBVAL of the ANY-CONTEXT! so
    // that the keylist can be changed without needing to update all the
    // REBVALs for that object.
    //
    // It may be a simple REBARR* -or- in the case of the varlist of a running
    // FRAME! on the stack, it points to a REBFRM*.  If it's a FRAME! that
    // is not running on the stack, it will be the function paramlist of the
    // actual phase that function is for.  Since REBFRM* all start with a
    // REBVAL cell, this means NODE_FLAG_CELL can be used on the node to
    // discern the case where it can be cast to a REBFRM* vs. REBARR*.
    //
    // (Note: FRAME!s used to use a field `misc.f` to track the associated
    // frame...but that prevented the ability to SET-META on a frame.  While
    // that feature may not be essential, it seems awkward to not allow it
    // since it's allowed for other ANY-CONTEXT!s.  Also, it turns out that
    // FRAME! values have to get their keylist via the specifically applicable
    // ->phase field anyway, and it's a faster test to check this for
    // NODE_FLAG_CELL than to separately extract the CTX_TYPE() and treat
    // frames differently.)
    //
    // It is done as a base-class REBNOD* as opposed to a union in order to
    // not run afoul of C's rules, by which you cannot assign one member of
    // a union and then read from another.
    //
    REBNOD *keysource;

    // The facade is a REBARR which is a proxy for the paramlist of the
    // underlying frame which is pushed when a function is called.  For
    // instance, if a specialization of APPEND provides the value to
    // append, that removes a parameter from the paramlist.  So the
    // specialization will not have the value.  However, the frame that
    // needs to be pushed for the call ultimately needs to have the
    // value--so it must be pushed.
    //
    // Originally this was done just by caching the paramlist of the
    // "underlying" function.  However, that can be limiting if one wants
    // to constrain the types or change the parameter classes.  The facade
    // *can* be the the paramlist of the underlying function, but it is
    // not necessarily.
    //
    REBARR *facade;

    // For REBSTR, circularly linked list of othEr-CaSed string forms
    //
    REBSTR *synonym;

    // On Reb_Function body_holders, this is the specialization frame for
    // a function--or NULL if none.
    //
    REBCTX *exemplar;

    // The MAP! datatype uses this.
    //
    REBSER *hashlist;

    // for STRUCT, this is a "REBFLD" array.  It parallels an object's
    // keylist, giving not only names of the fields in the structure but
    // also the types and sizes.
    //
    // !!! The Atronix FFI has been gradually moved away from having its
    // hooks directly into the low-level implemetation and the garbage
    // collector.  With the conversion of REBFLD to a REBARR instead of
    // a custom C type, it is one step closer to making STRUCT! a very
    // OBJECT!-like type extension.  When there is a full story told on
    // user-defined types, this should be excisable from the core.
    //
    REBFLD *schema;

    // For LIBRARY!, the file descriptor.  This is set to NULL when the
    // library is not loaded.
    //
    // !!! As with some other types, this may not need the optimization of
    // being in the Reb_Series node--but be handled via user defined types
    //
    void *fd;
};


// The `misc` field is an extra pointer-sized piece of data which is resident
// in the series node, and hence visible to all REBVALs that might be
// referring to the series.
//
union Reb_Series_Misc {
    //
    // Used to preload bad data in the debug build; see notes on link.trash
    //
#if !defined(NDEBUG)
    void *trash;
#endif

    // Ordinary source series store the line number here.  It probably
    // could have some bits taken out of it, vs. being a full 32-bit
    // integer on 32-bit platforms.
    //
    REBUPT line;

    // For REBSTR the canon cased form of this symbol, if it isn't canon
    // itself.  If it *is* a canon, then the field is free and is used
    // instead for `bind_index`
    //
    REBSTR *canon;

    // When binding words into a context, it's necessary to keep a table
    // mapping those words to indices in the context's keylist.  R3-Alpha
    // had a global "binding table" for the spellings of words, where
    // those spellings were not garbage collected.  Ren-C uses REBSERs
    // to store word spellings, and then has a hash table indexing them.
    //
    // So the "binding table" is chosen to be indices reachable from the
    // REBSER nodes of the words themselves.  If it were necessary for
    // multiple clients to have bindings at the same time, this could be
    // done through a pointer that would "pop out" into some kind of
    // linked list.  For now, the binding API just demonstrates having
    // up to 2 different indices in effect at once.
    //
    // Note that binding indices can be negative, so the sign can be used
    // to encode a property of that particular binding.
    //
    struct {
        int high:16;
        int low:16;
    } bind_index;

    // FUNCTION! paramlists and ANY-CONTEXT! varlists can store a "meta"
    // object.  It's where information for HELP is saved, and it's how modules
    // store out-of-band information that doesn't appear in their body.
    //
    REBCTX *meta;

    // When copying arrays, it's necessary to keep a map from source series
    // to their corresponding new copied series.  This allows multiple
    // appearances of the same identities in the source to give corresponding
    // appearances of the same *copied* identity in the target, and also is
    // integral to avoiding problems with cyclic structures.
    //
    // As with the `bind_index` above, the cheapest way to build such a map is
    // to put the forward into the series node itself.  However, when copying
    // a generic series the bits are all used up.  So the ->misc field is
    // temporarily "co-opted"...its content taken out of the node and put into
    // the forwarding entry.  Then the index of the forwarding entry is put
    // here.  At the end of the copy, all the ->misc fields are restored.
    //
    REBDSP forwarding;

    // native dispatcher code, see Reb_Function's body_holder
    //
    REBNAT dispatcher;

    // some HANDLE!s use this for GC finalization
    //
    CLEANUP_FUNC cleaner;

    // Because a bitset can get very large, the negation state is stored
    // as a boolean in the series.  Since negating a bitset is intended
    // to affect all values, it has to be stored somewhere that all
    // REBVALs would see a change--hence the field is in the series.
    //
    REBOOL negated;

    // used for vectors and bitsets
    //
    REBCNT size;

    // used for IMAGE!
    //
    // !!! The optimization by which images live in a single REBSER vs.
    // actually being a class of OBJECT! with something like an ordinary
    // PAIR! for its size is superfluous, and would be excised when it
    // is possible to make images a user-defined type.
    //
    struct {
        int wide:16; // Note: bitfields can only be int
        int high:16;
    } area;
};


struct Reb_Series {
    //
    // The low 2 bits in the header must be 00 if this is an "ordinary" REBSER
    // node.  This allows such nodes to implicitly terminate a "pairing"
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
    struct Reb_Header header;

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this series would want to be able
    // to see.  This cannot be done (easily) for properties that are held
    // in REBVAL cells directly.
    //
    // This field is in the second pointer-sized slot in the REBSER node to
    // push the `content` so it is 64-bit aligned on 32-bit platforms.  This
    // is because a REBVAL may be the actual content, and a REBVAL assumes
    // it is on a 64-bit boundary to start with...in order to position its
    // "payload" which might need to be 64-bit aligned as well.
    //
    // Use the LINK() macro to acquire this field...don't access directly.
    //
    union Reb_Series_Link link_private;

    // `content` is the sizeof(REBVAL) data for the series, which is thus
    // 4 platform pointers in size.  If the series is small enough, the header
    // contains the size in bytes and the content lives literally in these
    // bits.  If it's too large, it will instead be a pointer and tracking
    // information for another allocation.
    //
    union Reb_Series_Content content;

    // `info` is the information about the series which needs to be known
    // even if it is not using a dynamic allocation.
    //
    // It is purposefully positioned in the structure directly after the
    // ->content field, because it has NODE_FLAG_END set to true.  Hence it
    // appears to terminate an array of values if the content is not dynamic.
    // Yet NODE_FLAG_CELL is set to false, so it is not a writable location
    // (an "implicit terminator").
    //
    // !!! Only 32-bits are used on 64-bit platforms.  There could be some
    // interesting added caching feature or otherwise that would use
    // it, while not making any feature specifically require a 64-bit CPU.
    //
    struct Reb_Header info;

    // This is the second pointer-sized piece of series data that is used
    // for various purposes.  It is similar to ->link, however at some points
    // it can be temporarily "corrupted", since copying extracts it into a
    // forwarding entry and co-opts `misc.forwarding` to point to that entry.
    // It can be recovered...but one must know one is copying and go through
    // the forwarding.
    //
    // Currently it is assumed no one needs the ->misc while forwarding is in
    // effect...but the MISC() macro checks that.  Don't access this directly.
    //
    union Reb_Series_Misc misc_private;

#if !defined(NDEBUG)
    int *guard; // intentionally alloc'd and freed for use by Panic_Series
    REBUPT tick; // also maintains sizeof(REBSER) % sizeof(REBI64) == 0
#endif
};


// No special assertion needed for link at this time, since it is never
// co-opted for other purposes.
//
#define LINK(s) \
    SER(s)->link_private


// Currently only the C++ build does the check that ->misc is not being used
// at a time when it is forwarded out for copying.  If the C build were to
// do it, then it would be forced to go through a pointer access to do any
// writing...which would likely be less efficient.
//
#ifdef __cplusplus
    inline static union Reb_Series_Misc& Get_Series_Misc(REBSER *s) {
        return s->misc_private;
    }

    #define MISC(s) \
        Get_Series_Misc(SER(s))
#else
    #define MISC(s) \
        SER(s)->misc_private
#endif
