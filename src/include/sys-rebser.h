//
//  File: %sys-rebser.h
//  Summary: {any-series! defs BEFORE %tmp-internals.h (see: %sys-series.h)}
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
// This contains the struct definition for the StubStruct.  It is a small-ish
// descriptor for a series (though if the amount of data in the series is
// small enough, it is embedded into the structure itself.)
//
// Every string, block, path, etc. in Rebol has a Series.  The implementation
// of them is reused in many places where Rebol needs a general-purpose
// dynamically growing structure.  It is also used for fixed size structures
// which would like to participate in garbage collection.
//
// The Stub is fixed-size, and is allocated as a "node" from a memory pool.
// That pool quickly grants and releases memory ranges that are sizeof(Stub)
// without needing to use malloc() and free() for each individual allocation.
// These nodes can also be enumerated in the pool without needing the series
// to be tracked via a linked list or other structure.  The garbage collector
// is one example of code that performs such an enumeration.
//
// A Stub node pointer will remain valid as long as outstanding references
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
// * Because a series contains a union member that embeds a cell directly,
//   `struct Reb_Value` must be fully defined before this file can compile.
//   Hence %sys-rebval.h must already be included.
//
// * For the API of operations available on Series types, see %sys-series.h
//
// * Array is a series that contains Rebol Cells or Values.  It has many
//   concerns specific to special treatment and handling, in interaction with
//   the garbage collector as well as handling "relative vs specific" values.
//
// * Several related types (REBACT for function, REBCTX for context) are
//   actually stylized arrays.  They are laid out with special values in their
//   content (e.g. at the [0] index), or by links to other series in their
//   `->misc` field of the Stub node.  Hence series are the basic building
//   blocks of nearly all variable-size structures in the system.
//


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES <<HEADER>> FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Series have two places to store bits...in the "header" and in the "info".
// The following are the SERIES_FLAG_XXX and ARRAY_FLAG_XXX etc. that are used
// in the header, while the SERIES_INFO_XXX flags will be found in the info.
//
// ** Make_Series() takes SERIES_FLAG_XXX as a parameter, so anything that
// controls series creation should be a _FLAG_ as opposed to an _INFO_! **
//
// (Other general rules might be that bits that are to be tested or set as
// a group should be in the same flag group.  Perhaps things that don't change
// for the lifetime of the series might prefer header to the info, too?
// Such things might help with caching.)
//

#define SERIES_FLAGS_NONE \
    0 // helps locate places that want to say "no flags"


// Detect_Rebol_Pointer() uses the fact that this bit is 0 for series headers
// to discern between Stub, Cell, and END.  If push comes to shove that
// could be done differently, and this bit retaken.
//
#define SERIES_FLAG_8_IS_TRUE FLAG_LEFT_BIT(8) // CELL_FLAG_NOT_END


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
    FLAG_LEFT_BIT(9)

#define SERIES_FLAG_DONT_RELOCATE SERIES_FLAG_FIXED_SIZE


//=//// SERIES_FLAG_UTF8 ///////////////////////////////////////////=//
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
#define SERIES_FLAG_UTF8 \
    FLAG_LEFT_BIT(10)


//=//// SERIES_FLAG_POWER_OF_2 ////////////////////////////////////////////=//
//
// R3-Alpha would round some memory allocation requests up to a power of 2.
// This may well not be a good idea:
//
// http://stackoverflow.com/questions/3190146/
//
// But leaving it alone for the moment: there is a mechanical problem that the
// specific number of bytes requested for allocating series data is not saved.
// Only the series capacity measured in elements is known.
//
// Hence this flag is marked on the node, which is enough to recreate the
// actual number of allocator bytes to release when the series is freed.  The
// memory is accurately tracked for GC decisions, and balances back to 0 at
// program end.
//
// Note: All R3-Alpha's series had elements that were powers of 2, so this bit
// was not necessary there.
//
#define SERIES_FLAG_POWER_OF_2 \
    FLAG_LEFT_BIT(11)


//=//// SERIES_FLAG_12 ////////////////////////////////////////////////////=//
//
// Reclaimed.
//
#define SERIES_FLAG_12 \
    FLAG_LEFT_BIT(12)


//=//// SERIES_FLAG_ALWAYS_DYNAMIC ////////////////////////////////////////=//
//
// The optimization which uses small series will fit the data into the series
// node if it is small enough.  But doing this requires a test on Series_Len()
// and SER_DATA_RAW() to see if the small optimization is in effect.  Some
// code is more interested in the performance gained by being able to assume
// where to look for the data pointer and the length (e.g. paramlists and
// context varlists/keylists).  Passing this flag into series creation
// routines will avoid creating the shortened form.
//
// Note: Currently SERIES_INFO_INACCESSIBLE overrides this, but does not
// remove the flag...e.g. there can be inaccessible contexts that carry the
// SERIES_FLAG_ALWAYS_DYNAMIC bit but no longer have an allocation.
//
#define SERIES_FLAG_ALWAYS_DYNAMIC \
    FLAG_LEFT_BIT(13)


// ^-- STOP GENERIC SERIES FLAGS AT FLAG_LEFT_BIT(15) --^
//
// If a series is not an array, then the rightmost 16 bits of the series flags
// are used to store an arbitrary per-series-type 16 bit number.  Right now,
// that's used by the string series to save their SymId id integer (if they
// have one).
//
#if CPLUSPLUS_11
    static_assert(13 < 16, "SERIES_FLAG_XXX too high");
#endif


//
// Because there are a lot of different array flags that one might want to
// check, they are broken into a separate section.  However, note that if you
// do not know a series is an array you can't check just for this...e.g.
// an arbitrary Series tested for ARRAY_FLAG_VARLIST might alias with a
// UTF-8 symbol string whose symbol number uses that bit (!).
//


//=//// ARRAY_FLAG_FILE_LINE //////////////////////////////////////////////=//
//
// The Stub node has two pointers in it, ->link and ->misc, which are
// used for a variety of purposes (pointing to the keylist for an object,
// the C code that runs as the dispatcher for a function, etc.)  But for
// regular source series, they can be used to store the filename and line
// number, if applicable.
//
// Only arrays preserve file and line info, as UTF-8 strings need to use the
// ->misc and ->link fields for caching purposes in strings.
//
#define ARRAY_FLAG_FILE_LINE \
    FLAG_LEFT_BIT(16)


//=//// ARRAY_FLAG_NULLEDS_LEGAL //////////////////////////////////////////=//
//
// Identifies arrays in which it is legal to have nulled elements.  This is
// true for reified C va_list()s which treated slots as if they had already
// abeen evaluated.  (See VALUE_FLAG_EVAL_FLIP).  When those va_lists need to
// be put into arrays for the purposes of GC protection, they may contain
// nulled cells.  (How to present this in the debugger will be a UI issue.)
//
// Note: ARRAY_FLAG_VARLIST also implies legality of nulleds, which in that
// case are used to represent unset variables.
//
#define ARRAY_FLAG_NULLEDS_LEGAL \
    FLAG_LEFT_BIT(17)


//=//// ARRAY_FLAG_PARAMLIST //////////////////////////////////////////////=//
//
// ARRAY_FLAG_PARAMLIST indicates the array is the parameter list of a
// ACTION! (the first element will be a canon value of the function)
//
#define ARRAY_FLAG_PARAMLIST \
    FLAG_LEFT_BIT(18)


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
    FLAG_LEFT_BIT(19)


//=//// ARRAY_FLAG_PAIRLIST ///////////////////////////////////////////////=//
//
// Indicates that this series represents the "pairlist" of a map, so the
// series also has a hashlist linked to in the series node.
//
#define ARRAY_FLAG_PAIRLIST \
    FLAG_LEFT_BIT(20)


//=//// ARRAY_FLAG_21 /////////////////////////////////////////////////////=//
//
// Not used as of yet.
//
#define ARRAY_FLAG_21 \
    FLAG_LEFT_BIT(21)


//=//// ARRAY_FLAG_TAIL_NEWLINE ///////////////////////////////////////////=//
//
// The mechanics of how Rebol tracks newlines is that there is only one bit
// per value to track the property.  Yet since newlines are conceptually
// "between" values, that's one bit too few to represent all possibilities.
//
// Ren-C carries a bit for indicating when there's a newline intended at the
// tail of an array.
//
#define ARRAY_FLAG_TAIL_NEWLINE \
    FLAG_LEFT_BIT(22)


// ^-- STOP ARRAY FLAGS AT FLAG_LEFT_BIT(31) --^
//
// Arrays can use all the way up to the 32-bit limit on the flags (since
// they're not using the arbitrary 16-bit number the way that a Symbol is for
// storing the symbol).  64-bit machines have more space, but it shouldn't
// be used for anything but optimizations.
//
#if CPLUSPLUS_11
    static_assert(22 < 32, "ARRAY_FLAG_XXX too high");
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES <<INFO>> BITS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See remarks on SERIES <<FLAG>> BITS about the two places where series store
// bits.  These are the info bits, which are more likely to be changed over
// the lifetime of the series--defaulting to FALSE.
//
// See Endlike_Header() for why the reserved bits are chosen the way they are.
//

#define SERIES_INFO_0_IS_TRUE FLAG_LEFT_BIT(0) // NODE_FLAG_NODE
#define SERIES_INFO_1_IS_FALSE FLAG_LEFT_BIT(1) // NOT(NODE_FLAG_FREE)


//=//// SERIES_INFO_2 /////////////////////////////////////////////////////=//
//
// reclaimed.
//
// Note: Same bit position as NODE_FLAG_MANAGED in flags, if that is relevant.
//
#define SERIES_INFO_2 \
    FLAG_LEFT_BIT(2)


//=//// SERIES_INFO_BLACK /////////////////////////////////////////////////=//
//
// This is a generic bit for the "coloring API", e.g. Is_Series_Black(),
// Flip_Series_White(), etc.  These let native routines engage in marking
// and unmarking nodes without potentially wrecking the garbage collector by
// reusing NODE_FLAG_MARKED.  Purposes could be for recursion protection or
// other features, to avoid having to make a map from Series to bool.
//
// Note: Same bit as NODE_FLAG_MARKED, interesting but irrelevant.
//
#define SERIES_INFO_BLACK \
    FLAG_LEFT_BIT(3)


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
#define SERIES_INFO_PROTECTED \
    FLAG_LEFT_BIT(4)


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
#define SERIES_INFO_HOLD \
    FLAG_LEFT_BIT(5)


//=//// SERIES_INFO_FROZEN ////////////////////////////////////////////////=//
//
// Indicates that the length or values cannot be modified...ever.  It has been
// locked and will never be released from that state for its lifetime, and if
// it's an array then everything referenced beneath it is also frozen.  This
// means that if a read-only copy of it is required, no copy needs to be made.
//
// (Contrast this with the temporary condition like caused by something
// like SERIES_INFO_HOLD or SERIES_INFO_PROTECTED.)
//
// Note: This and the other read-only series checks are honored by some layers
// of abstraction, but if one manages to get a raw non-const pointer into a
// value in the series data...then by that point it cannot be enforced.
//
#define SERIES_INFO_FROZEN \
    FLAG_LEFT_BIT(6)


#define SERIES_INFO_7_IS_FALSE FLAG_LEFT_BIT(7) // NOT(NODE_FLAG_CELL)


//=//// BITS 8-15 ARE FOR Series_Wide() //////////////////////////////////////=//

// The "width" is the size of the individual elements in the series.  For an
// ANY-ARRAY this is always 0, to indicate IS_END() for arrays of length 0-1
// (singulars) which can be held completely in the content bits before the
// ->info field.  Hence this is also used for IS_SER_ARRAY()

#define FLAG_WIDE_BYTE_OR_0(wide) \
    FLAG_SECOND_BYTE(wide)

#define WIDE_BYTE_OR_0(s) \
    SECOND_BYTE(&(s)->info.bits)


//=//// BITS 16-23 ARE Series_Len() FOR NON-DYNAMIC SERIES ///////////////////=//

// There is currently no usage of this byte for dynamic series, so it could
// be used for something else there.  (Or a special value like 255 could be
// used to indicate dynamic/non-dynamic series, which might speed up Series_Len()
// and other bit fiddling operations vs. SERIES_INFO_HAS_DYNAMIC).
//
// 255 indicates that this series has a dynamically allocated portion.  If it
// is another value, then it's the length of content which is found directly
// in the series node's embedded StubContentUnion.
//
// (See also: SERIES_FLAG_ALWAYS_DYNAMIC to prevent creating embedded data.)
//

#define FLAG_LEN_BYTE_OR_255(len) \
    FLAG_THIRD_BYTE(len)

#define const_LEN_BYTE_OR_255(s) \
    const_THIRD_BYTE((s)->info)

#define LEN_BYTE_OR_255(s) \
    THIRD_BYTE(&(s)->info)


//=//// SERIES_INFO_AUTO_LOCKED ///////////////////////////////////////////=//
//
// Some operations lock series automatically, e.g. to use a piece of data as
// map keys.  This approach was chosen after realizing that a lot of times,
// users don't care if something they use as a key gets locked.  So instead
// of erroring by telling them they can't use an unlocked series as a map key,
// this locks it but changes the SERIES_FLAG_FILE_LINE to implicate the
// point where the locking occurs.
//
// !!! The file-line feature is pending.
//
#define SERIES_INFO_AUTO_LOCKED \
    FLAG_LEFT_BIT(24)


//=//// SERIES_INFO_INACCESSIBLE //////////////////////////////////////////=//
//
// Currently this used to note when a CONTEXT_INFO_STACK series has had its
// stack level popped (there's no data to lookup for words bound to it).
//
// !!! This is currently redundant with checking if a CONTEXT_INFO_STACK
// series has its `misc.L` (Level) nulled out, but it means both can be
// tested at the same time with a single bit.
//
// !!! It is conceivable that there would be other cases besides frames that
// would want to expire their contents, and it's also conceivable that frames
// might want to *half* expire their contents (e.g. have a hybrid of both
// stack and dynamic values+locals).  These are potential things to look at.
//
#define SERIES_INFO_INACCESSIBLE \
    FLAG_LEFT_BIT(25)


//=//// FRAME_INFO_FAILED /////////////////////////////////////////////////=//
//
// In the specific case of a frame being freed due to a failure, this mark
// is put on the context node.  What this allows is for the system to account
// for which nodes are being GC'd due to lack of a rebRelease(), as opposed
// to those being GC'd due to failure.
//
// What this means is that the system can use managed handles by default
// while still letting "rigorous" code track cases where it made use of the
// GC facility vs. doing explicit tracking.  Essentially, it permits a kind
// of valgrind/address-sanitizer way of looking at a codebase vs. just taking
// for granted that it will GC things.
//
#define FRAME_INFO_FAILED \
    FLAG_LEFT_BIT(26)


//=//// STRING_INFO_CANON /////////////////////////////////////////////////=//
//
// This is used to indicate when a SERIES_FLAG_UTF8 series represents
// the canon form of a word.  This doesn't mean anything special about the
// case of its letters--just that it was loaded first.  Canon forms can be
// GC'd and then delegate the job of being canon to another symbol.
//
// A canon symbol is unique because it does not need to store a pointer to
// its canon form.  So it can use the Stub.misc field for the purpose of
// holding an index during binding.
//
#define STRING_INFO_CANON \
    FLAG_LEFT_BIT(27)


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
    FLAG_LEFT_BIT(28)


//=//// SERIES_INFO_API_RELEASE ///////////////////////////////////////////=//
//
// The rebT() function can be used with an API handle to tell a variadic
// function to release that handle after encountering it.
//
// !!! API handles are singular arrays, because there is already a stake in
// making them efficient.  However it means they have to share header and
// info bits, when most are not applicable to them.  This is a tradeoff, and
// contention for bits may become an issue in the future.
//
#define SERIES_INFO_API_RELEASE \
    FLAG_LEFT_BIT(29)


//=//// SERIES_INFO_API_INSTRUCTION ///////////////////////////////////////=//
//
// Rather than have LINK() and MISC() fields used to distinguish an API
// handle like an INTEGER! from something like a rebEval(), a flag helps
// keep those free for different purposes.
//
#define SERIES_INFO_API_INSTRUCTION \
    FLAG_LEFT_BIT(30)


#ifdef DEBUG_MONITOR_SERIES

    //=//// SERIES_INFO_MONITOR_DEBUG /////////////////////////////////////=//
    //
    // Simple feature for tracking when a series gets freed or otherwise
    // messed with.  Setting this bit on it asks for a notice.
    //
    #define SERIES_INFO_MONITOR_DEBUG \
        FLAG_LEFT_BIT(31)
#endif


// ^-- STOP AT FLAG_LEFT_BIT(31) --^
//
// While 64-bit systems have another 32-bits available in the header, core
// functionality shouldn't require using them...only optimization features.
//
#if CPLUSPLUS_11
    static_assert(31 < 32, "SERIES_INFO_XXX too high");
#endif


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES NODE ("Stub") STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A Stub node is the size of two cells, and there are 3 basic layouts
// which can be overlaid inside the node:
//
//      Dynamic: [header [allocation tracking] info link misc]
//     Singular: [header [cell] info link misc]
//      Pairing: [[cell] [cell]]
//
// `info` is not the start of a "Rebol Node" (REBNODE, e.g. either a Stub or
// a value Cell).  But in the singular case it is positioned right where
// the next cell after the embedded cell *would* be.  Hence the second byte
// in the info corresponding to VAL_TYPE() is 0, making it conform to the
// "terminating array" pattern.  To lower the risk of this implicit terminator
// being accidentally overwritten (which would corrupt link and misc), the
// bit corresponding to NODE_FLAG_CELL is clear.
//
// Singulars have widespread applications in the system, notably the
// efficient implementation of FRAME!.  They also narrow the gap in overhead
// between COMPOSE [A (B) C] vs. REDUCE ['A B 'C] such that the memory cost
// of the array is nearly the same as just having another value in the array.
//
// Pair nodes are allocated from the Stub pool instead of their own to
// help exchange a common "currency" of allocation size more efficiently.
// They are planned for use in the PAIR! and MAP! datatypes, and anticipated
// to play a crucial part in the API--allowing a persistent handle for a
// GC'able cell and associated "meta" value (which can be used for
// reference counting or other tracking.)
//
// Most of the time, code does not need to be concerned about distinguishing
// Pair from the Dynamic and Singular layouts--because it already knows
// which kind it has.  Only the GC needs to be concerned when marking
// and sweeping.
//

struct StubDynamicStruct {
    //
    // `data` is the "head" of the series data.  It may not point directly at
    // the memory location that was returned from the allocator if it has
    // bias included in it.
    //
    // !!! We use `char*` here to ease debugging in systems that don't show
    // ASCII by default for unsigned characters, for when it's UTF-8 data.
    //
    char *data;

    // `len` is one past end of useful data.
    //
    REBLEN len;

    // `rest` is the total number of units from bias to end.  Having a
    // slightly weird name draws attention to the idea that it's not really
    // the "capacity", just the "rest of the capacity after the bias".
    //
    REBLEN rest;

    // This is the 4th pointer on 32-bit platforms which could be used for
    // something when a series is dynamic.  Previously the bias was not
    // a full REBLEN but was limited in range to 16 bits or so.  This means
    // 16 info bits are likely available if needed for dynamic series.
    //
    REBLEN bias;
};


union StubContentUnion {
    //
    // If the series does not fit into the Stub node, then it must be
    // dynamically allocated.  This is the tracking structure for that
    // dynamic data allocation.
    //
    struct StubDynamicStruct dynamic;

    // If LEN_BYTE_OR_255() != 255, 0 or 1 length arrays can be held in
    // the series node.  This trick is accomplished via "implicit termination"
    // in the ->info bits that come directly after ->content.  For how this is
    // done, see Endlike_Header()
    //
    union {
        // Due to strict aliasing requirements, this has to be a Cell to
        // read cell data.  Unfortunately this means StubContentUnion can't
        // be copied by simple assignment, because in the C++ build it is
        // disallowed to say (`*value1 = *value2;`).  Use memcpy().
        //
        Cell cell;

      #if !defined(NDEBUG) // https://en.wikipedia.org/wiki/Type_punning
        char utf8_pun[sizeof(Cell)]; // debug watchlist insight into UTF-8
        REBUNI ucs2_pun[sizeof(Cell)/sizeof(REBUNI)]; // wchar_t insight
      #endif
    } fixed;
};

#define SER_CELL(s) \
    (&(s)->content.fixed.cell) // unchecked ARR_SINGLE(), used for init


union StubLinkUnion {
    //
    // If you assign one member in a union and read from another, then that's
    // technically undefined behavior.  But this field is used as the one
    // that is "corrupted" in the debug build when the series is created, and
    // hopefully it will lead to the other fields reading garbage (vs. zero)
    //
  #if !defined(NDEBUG)
    void *corrupt;
  #endif

    // API handles use "singular" format arrays (see notes on that), which
    // lay out the link field in the bytes preceding the Value* payload.
    // Because the API tries to have routines that work across arbitrary
    // rebMalloc() memory as well as individual cells, the bytes preceding
    // the pointer handed out to the client are examined to determine which
    // it is.  If it's an array-type series, it is either the varlist of
    // the owning frame *or* the EMPTY_ARRAY (to avoid a nullptr check)
    //
    REBCTX* owner;

    // Ordinary source series use their ->link field to point to an
    // interned file name string from which the code was loaded.  If a
    // series was not created from a file, then the information from the
    // source that was running at the time is propagated into the new
    // second-generation series.
    //
    String* file;

    // REBCTX types use this field of their varlist (which is the identity of
    // an ANY-CONTEXT!) to find their "keylist".  It is stored in the Stub
    // node of the varlist Array vs. in the cell of the ANY-CONTEXT! so
    // that the keylist can be changed without needing to update all the
    // REBVALs for that object.
    //
    // It may be a simple Array* -or- in the case of the varlist of a running
    // FRAME! on the stack, it points to a Level*.  If it's a FRAME! that
    // is not running on the stack, it will be the function paramlist of the
    // actual phase that function is for.  Since Level* all start with a
    // leading cell, this means NODE_FLAG_CELL can be used on the node to
    // discern the case where it can be cast to a Level* vs. Array*.
    //
    // (Note: FRAME!s used to use a field `misc.L` to track the associated
    // frame...but that prevented the ability to SET-META on a frame.  While
    // that feature may not be essential, it seems awkward to not allow it
    // since it's allowed for other ANY-CONTEXT!s.  Also, it turns out that
    // heap-based FRAME! values--such as those that come from MAKE FRAME!--
    // have to get their keylist via the specifically applicable ->phase field
    // anyway, and it's a faster test to check this for NODE_FLAG_CELL than to
    // separately extract the CTX_TYPE() and treat frames differently.)
    //
    // It is done as a base-class Node* as opposed to a union in order to
    // not run afoul of C's rules, by which you cannot assign one member of
    // a union and then read from another.
    //
    Node* keysource;

    // On the keylist of an object, this points at a keylist which has the
    // same number of keys or fewer, which represents an object which this
    // object is derived from.  Note that when new object instances are
    // created which do not require expanding the object, their keylist will
    // be the same as the object they are derived from.
    //
    Array* ancestor;

    // An underlying function is one whose frame is compatible with a
    // derived function (e.g. the underlying function of a specialization or
    // an adaptation).
    //
    REBACT *underlying;

    // For a *read-only* Symbol, circularly linked list of othEr-CaSed
    // symbol forms.  It should be relatively quick to find the canon form on
    // average, since many-cased forms are somewhat rare.
    //
    Symbol* synonym;

    // REBACT uses this.  It can hold either the varlist of a frame containing
    // specialized values (e.g. an "exemplar"), with ARRAY_FLAG_VARLIST set.
    // Or just hold the paramlist.  This speeds up Push_Action() because
    // if this were `REBCTX *exemplar;` then it would have to test it for null
    // explicitly to default L->special to L->param.
    //
    Array* specialty;

    // The MAP! datatype uses this.
    //
    Series* hashlist;

    // The Level's `varlist` field holds a ready-made varlist for a level,
    // which may be reused.  However, when a stack frame is dropped it can
    // only be reused by putting it in a place that future pushes can find
    // it.  This is used to link a varlist into the reusable list.
    //
    Array* reuse;
};


// The `misc` field is an extra pointer-sized piece of data which is resident
// in the series node, and hence visible to all REBVALs that might be
// referring to the series.
//
union StubMiscUnion {
    //
    // Used to preload bad data in the debug build; see notes on link.corrupt
    //
  #if !defined(NDEBUG)
    void *corrupt;
  #endif

    // Ordinary source series store the line number here.  It perhaps could
    // have some bits taken out of it, vs. being a full 32-bit integer on
    // 32-bit platforms or 64-bit integer on 64-bit platforms.
    //
    LineNumber line;

    // Under UTF-8 everywhere, strings are byte-sized...so the series "size"
    // is actually counting *bytes*, not logical character codepoint units.
    // SER_SIZE() and Series_Len() can therefore be different...where Series_Len()
    // on a string series comes from here, vs. just report the size.
    //
    REBSIZ length;

    // When binding words into a context, it's necessary to keep a table
    // mapping those words to indices in the context's keylist.  R3-Alpha
    // had a global "binding table" for the symbols of words, where
    // those symbols were not garbage collected.  Ren-C uses Series
    // to store word symbols, and then has a hash table indexing them.
    // So the "binding table" is chosen to be indices reachable from the
    // Stub nodes of the words themselves.
    //
    // !!! This technique is modified heavily in modern Ren-C with what is
    // known as "sea of words", where variables are free-floating stubs
    // reachable from the symbol stubs.  That is more complex than this old
    // bootstrap executable can accomplish, so instead stubs just store a
    // transient index for a binder, as well as a persistent index for where
    // things are in lib.
    //
    // !!! Note that binding indices can be negative, so the sign can be used
    // to encode a property of that particular binding.
    //
    struct {
        int lib:16;
        int other:16;
    } bind_index;

    // ACTION! paramlists and ANY-CONTEXT! varlists can store a "meta"
    // object.  It's where information for HELP is saved, and it's how modules
    // store out-of-band information that doesn't appear in their body.
    //
    REBCTX *meta;

    // native dispatcher code, see Reb_Function's body_holder
    //
    REBNAT dispatcher;

    // some HANDLE!s use this for GC finalization
    //
    CLEANUP_CFUNC *cleaner;

    // Because a bitset can get very large, the negation state is stored
    // as a boolean in the series.  Since negating a bitset is intended
    // to affect all values, it has to be stored somewhere that all
    // REBVALs would see a change--hence the field is in the series.
    //
    bool negated;
};


struct StubStruct {
    //
    // The bit that is checked in the header is the USED bit, which is
    // bit #9.  This is set on all REBVALs and also in END marking headers,
    // and should be set in used series nodes.
    //
    // The remaining bits are free, and used to hold SYM values for those
    // words that have them.
    //
    union HeaderUnion header;

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this series would want to be able
    // to see.  This cannot be done (easily) for properties that are held
    // in cells directly.
    //
    // This field is in the second pointer-sized slot in the StubStruct to
    // push the `content` so it is 64-bit aligned on 32-bit platforms.  This
    // is because a cell may be the actual content, and a cell assumes
    // it is on a 64-bit boundary to start with...in order to position its
    // "payload" which might need to be 64-bit aligned as well.
    //
    // Use the LINK() macro to acquire this field...don't access directly.
    //
    union StubLinkUnion link_private;

    // `content` is the sizeof(Cell) data for the series, which is thus
    // 4 platform pointers in size.  If the series is small enough, the header
    // contains the size in bytes and the content lives literally in these
    // bits.  If it's too large, it will instead be a pointer and tracking
    // information for another allocation.
    //
    union StubContentUnion content;

    // `info` is the information about the series which needs to be known
    // even if it is not using a dynamic allocation.
    //
    // It is purposefully positioned in the structure directly after the
    // ->content field, because its second byte is '\0' when the series is
    // an array.  Hence it appears to terminate an array of values if the
    // content is not dynamic.  Yet NODE_FLAG_CELL is set to false, so it is
    // not a writable location (an "implicit terminator").
    //
    // !!! Only 32-bits are used on 64-bit platforms.  There could be some
    // interesting added caching feature or otherwise that would use
    // it, while not making any feature specifically require a 64-bit CPU.
    //
    union HeaderUnion info;

    // This is the second pointer-sized piece of series data that is used
    // for various purposes.
    //
    union StubMiscUnion misc_private;

#if defined(DEBUG_SERIES_ORIGINS) || defined(DEBUG_COUNT_TICKS)
    intptr_t *guard;  // alloc => immediate free, for use by Panic_Series()
    uintptr_t tick;  // also maintains sizeof(Stub) % sizeof(REBI64) == 0
#endif
};

// These macros are superfluous here, but do more in modern builds.

#define LINK(s)     (s)->link_private
#define MISC(s)     (s)->misc_private


//=//// SERIES SUBCLASSES /////////////////////////////////////////////////=//

typedef struct StubStruct Series;

#if CPLUSPLUS_11
    struct Binary : public Series {};
    struct String : public Series {};  // derives from Binary in main branch
    struct Symbol : public Binary {};  // derives from String in main branch

    struct Array : public Series {};

    struct REBCTX : public Stub {};
    struct REBACT : public Stub {};
    struct REBMAP : public Stub {};
#else
    typedef Series Binary;
    typedef Series String;
    typedef Series Symbol;

    typedef Series Array;

    typedef Series REBCTX;
    typedef Series REBACT;
    typedef Series REBMAP;
#endif


#if !defined(DEBUG_CHECK_CASTS) || (! CPLUSPLUS_11)

    #define SER(p) \
        cast(Series*, (p))

    #define ARR(p) \
        cast(Array*, (p))

#else

    template <class T>
    inline Series* SER(T *p) {
        constexpr bool base = std::is_same<T, void>::value;

        static_assert(
            base,
            "SER() works on void"
        );

        if (base)
            assert(
                (reinterpret_cast<Series*>(p)->header.bits & (
                    NODE_FLAG_NODE | NODE_FLAG_FREE | NODE_FLAG_CELL
                )) == (
                    NODE_FLAG_NODE
                )
            );

        return reinterpret_cast<Series*>(p);
    }

    template <class T>
    inline Array* ARR(T *p) {
        constexpr bool derived = std::is_same<T, Array>::value;

        constexpr bool base = std::is_same<T, void>::value
            or std::is_same<T, Node>::value
            or std::is_same<T, Series>::value;

        static_assert(
            derived or base,
            "ARR works on void/Node/Series/Array"
        );

        if (base) {
            assert(WIDE_BYTE_OR_0(reinterpret_cast<Series*>(p)) == 0);
            assert(
                (reinterpret_cast<Series*>(p)->header.bits & (
                    NODE_FLAG_NODE
                        | NODE_FLAG_FREE
                        | NODE_FLAG_CELL
                )) == (
                    NODE_FLAG_NODE
                )
           );
        }

        return reinterpret_cast<Array*>(p);
    }

#endif


//
// Series header FLAGs (distinct from INFO bits)
//

#define SET_SER_FLAG(s,f) \
    cast(void, (s)->header.bits |= (f))

#define CLEAR_SER_FLAG(s,f) \
    cast(void, (s)->header.bits &= ~(f))

#define GET_SER_FLAG(s,f) \
    (did ((s)->header.bits & (f))) // !!! ensure it's just one flag?

#define NOT_SER_FLAG(s,f) \
    (not ((s)->header.bits & (f)))


//
// Series INFO bits (distinct from header FLAGs)
//

#define SET_SER_INFO(s,f) \
    cast(void, (s)->info.bits |= (f))

#define CLEAR_SER_INFO(s,f) \
    cast(void, (s)->info.bits &= ~(f))

#define GET_SER_INFO(s,f) \
    (did ((s)->info.bits & (f)))

#define NOT_SER_INFO(s,f) \
    (not ((s)->info.bits & (f)))


#define IS_SER_ARRAY(s) \
    (WIDE_BYTE_OR_0(s) == 0)

#define IS_SER_DYNAMIC(s) \
    (LEN_BYTE_OR_255(s) == 255)

// These are series implementation details that should not be used by most
// code.  But in order to get good inlining, they have to be in the header
// files (of the *internal* API, not of libRebol).  Generally avoid it.
//
// !!! Can't `assert((w) < MAX_SERIES_WIDE)` without triggering "range of
// type makes this always false" warning; C++ build could sense if it's a
// Byte and dodge the comparison if so.
//

#define MAX_SERIES_WIDE 0x100

INLINE Byte Series_Wide(Series* s) {
    //
    // Arrays use 0 width as a strategic choice, so that the second byte of
    // the ->info flags is 0.  See Endlike_Header() for why.
    //
    Byte wide = WIDE_BYTE_OR_0(s);
    if (wide == 0) {
        assert(IS_SER_ARRAY(s));
        return sizeof(Cell);
    }
    return wide;
}


//
// Bias is empty space in front of head:
//

INLINE REBLEN Series_Bias(Series* s) {
    assert(IS_SER_DYNAMIC(s));
    return cast(REBLEN, ((s)->content.dynamic.bias >> 16) & 0xffff);
}

INLINE REBLEN Series_Rest(Series* s) {
    if (LEN_BYTE_OR_255(s) == 255)
        return s->content.dynamic.rest;

    if (IS_SER_ARRAY(s))
        return 2; // includes info bits acting as trick "terminator"

    assert(sizeof(s->content) % Series_Wide(s) == 0);
    return sizeof(s->content) / Series_Wide(s);
}

#define MAX_SERIES_BIAS 0x1000

INLINE void SER_SET_BIAS(Series* s, REBLEN bias) {
    assert(IS_SER_DYNAMIC(s));
    s->content.dynamic.bias =
        (s->content.dynamic.bias & 0xffff) | (bias << 16);
}

INLINE void SER_ADD_BIAS(Series* s, REBLEN b) {
    assert(IS_SER_DYNAMIC(s));
    s->content.dynamic.bias += b << 16;
}

INLINE void SER_SUB_BIAS(Series* s, REBLEN b) {
    assert(IS_SER_DYNAMIC(s));
    s->content.dynamic.bias -= b << 16;
}

INLINE size_t SER_TOTAL(Series* s) {
    return (Series_Rest(s) + Series_Bias(s)) * Series_Wide(s);
}

INLINE size_t SER_TOTAL_IF_DYNAMIC(Series* s) {
    if (not IS_SER_DYNAMIC(s))
        return 0;
    return SER_TOTAL(s);
}
