//
//  File: %sys-rebser.h
//  Summary: {any-series! defs BEFORE %tmp-internals.h (see: %sys-series.h)}
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
// Series is actually just a "Stub"... a small-ish fixed-size descriptor for
// series data.  Usually it contains a pointer to a larger allocation for the
// actual contents.  But if the series is small enough, the contents are
// embedded into the stub structure itself.
//
// Every string, block, path, etc. in Rebol has a Series.  Since Rebol does
// not depend on any data structure libraries--like C++'s std::vector--this
// means that the Series is also used internally when there is a need for a
// dynamically growable contiguous memory structure.
//
// Series behaves something like a "double-ended queue".  It can reserve
// capacity at both the tail and the head.  When data is taken from the head,
// it will retain that capacity...reusing it on later insertions at the head.
//
// The space at the head is called the "bias", and to save on pointer math
// per-access, the stored data pointer is actually adjusted to include the
// bias.  This biasing is backed out upon insertions at the head, and also
// must be subtracted completely to free the pointer using the address
// originally given by the allocator.
//
// The Series is fixed-size, and is allocated as a "stub" from a memory pool.
// That pool quickly grants and releases memory ranges that are sizeof(Stub)
// without needing to use malloc() and free() for each individual allocation.
// These nodes can also be enumerated in the pool without needing the series
// to be tracked via a linked list or other structure.  The garbage collector
// is one example of code that performs such an enumeration.
//
// A Series stub pointer will remain valid as long as outstanding references
// to the series exist in values visible to the GC.  On the other hand, the
// series's data pointer may be freed and reallocated to respond to the needs
// of resizing.  (In the future, it may be reallocated just as an idle task
// by the GC to reclaim or optimize space.)
//
//    *** THIS MEANS POINTERS INTO THE Series_Data() FOR A MANAGED SERIES
//    MUST NOT BE HELD ONTO ACROSS EVALUATIONS, WITHOUT SPECIAL PROTECTION
//    OR ACCOMMODATION.**
//
// Series may be either manually memory managed or delegated to the garbage
// collector.  Free_Unmanaged_Series() may only be called on manual series.
// See Manage_Series()/Push_GC_Guard() for remarks on how to work safely
// with pointers to garbage-collected series, to avoid having them be GC'd
// out from under the code while working with them.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * For the forward declarations of series subclasses, see %reb-defs.h
//
// * Because a series contains a union member that embeds a Cell directly,
//   `Cell` must be fully defined before this file can compile.  Hence
//   %struct-cell.h must already be included.
//
// * For the API of operations available on Series types, see %sys-series.h
//
// * Array* is a series that contains Rebol cells.  It has many concerns
//   specific to special treatment and handling, in interaction with the
//   garbage collector as well as handling "relative vs specific" values.
//
// * Several related types (Action* for function, Context* for context) are
//   actually stylized arrays.  They are laid out with special values in their
//   content (e.g. at the [0] index), or by links to other series in their
//   `->misc` field of the series stub.  Hence series are the basic building
//   blocks of nearly all variable-size structures in the system.
//
// * The element size in a series is known as the "width".  R3-Alpha used a
//   byte for this to get from 0-255.  Ren-C uses that byte for the "flavor"
//   of the series (a unique name distinguishing series "type" in a way
//   parallel to cell "type") and then maps from flavor to size.
//


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES_FLAG_0 - SERIES_FLAG_7 are NODE_FLAG_XXX)
//
//=////////////////////////////////////////////////////////////////////////=//

// At one time all the flags were aliased, like:
//
//     #define SERIES_FLAG_MANAGED NODE_FLAG_MANAGED
//     #define SERIES_FLAG_FREE NODE_FLAG_FREE
//     ...
//
// This created weird inconsistencies where it would make an equal amount of
// sense to pass SERIES_FLAG_MANAGED or NODE_FLAG_MANAGED, and introduces the
// risk that the checks might be performed on pointers that don't know if
// what they point at is a Cell or a Stub.  The duplication was removed, and
// now you say `Is_Node_Managed(ser)` vs. `Get_Series_Flag(ser, MANAGED)` etc.
//
// Aliases for the NODE_FLAG_GC_ONE and NODE_FLAG_GC_TWO are kept, as there
// is no corresponding ambiguity.


//=//// SERIES_FLAG_LINK_NODE_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that a series's LINK() field is the `custom` node element,
// and should be marked (if not null).
//
// Note: Even if this flag is not set, *link.any might still be a node*...
// just not one that should be marked.
//
#define SERIES_FLAG_LINK_NODE_NEEDS_MARK \
    NODE_FLAG_GC_ONE


//=//// SERIES_FLAG_MISC_NODE_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that a series's MISC() field is the `custom` node element,
// and should be marked (if not null).
//
// Note: Even if this flag is not set, *misc.any might still be a node*...
// just not one that should be marked.
//
#define SERIES_FLAG_MISC_NODE_NEEDS_MARK \
    NODE_FLAG_GC_TWO


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
// ** Make_Series() takes SERIES_FLAG_XXX as a parameter, so anything that
// controls series creation should be a _FLAG_ as opposed to an _INFO_! **
//
// (Other general rules might be that bits that are to be tested or set as
// a group should be in the same flag group.  Perhaps things that don't change
// for the lifetime of the series might prefer header to the info, too?
// Such things might help with caching.)
//

#define SERIES_FLAGS_NONE \
    0  // helps locate places that want to say "no flags"


//=//// SERIES_FLAG_INACCESSIBLE //////////////////////////////////////////=//
//
// An inaccessible series is one which may still have extant references, but
// the data is no longer available.  That can happen implicitly or because
// of a manual use of the FREE operation.
//
// It would be costly if all series access operations had to check the
// accessibility bit.  Instead, the general pattern is that code that extracts
// series from values, e.g. Cell_Array(), performs a check to make sure that
// the series is accessible at the time of extraction.  Subsequent access of
// the extracted series is then unchecked.
//
#define SERIES_FLAG_INACCESSIBLE \
    FLAG_LEFT_BIT(8)


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
// a value inside the series data...it still might have to check INACCESSIBLE.
//
// !!! Strictly speaking, SERIES_FLAG_NO_RELOCATE could be different
// from fixed size... if there would be a reason to reallocate besides
// changing size (such as memory compaction).  For now, just make the two
// equivalent but let the callsite distinguish the intent.
//
#define SERIES_FLAG_FIXED_SIZE \
    FLAG_LEFT_BIT(9)

#define SERIES_FLAG_DONT_RELOCATE SERIES_FLAG_FIXED_SIZE


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
    FLAG_LEFT_BIT(10)


//=//// SERIES_FLAG_DYNAMIC ///////////////////////////////////////////////=//
//
// The optimization which uses small series will fit the data into the series
// node if it is small enough.  This flag is set when a series uses its
// `content` for tracking information instead of the actual data itself.
//
// It can also be passed in at series creation time to force an allocation to
// be dynamic.  This is because some code is more interested in performance
// gained by being able to assume where to look for the data pointer and the
// length (e.g. paramlists and context varlists/keylists).  So passing this
// flag into series creation routines avoids creating the shortened form.
//
// Note: Currently SERIES_FLAG_INACCESSIBLE overrides this, but does not
// remove the flag...e.g. there can be inaccessible contexts that carry the
// SERIES_FLAG_ALWAYS_DYNAMIC bit but no longer have an allocation.
//
// Note: At one time the USED_BYTE() of 255 was the signal for this.  But
// being able to pass in the flag to creation routines easily, and make the
// test easier with Get_Series_Flag(), was seen as better.  Also, this means
// dynamic series have an entire byte worth of free data available to use.
//
#define SERIES_FLAG_DYNAMIC \
    FLAG_LEFT_BIT(11)


//=//// SERIES_FLAG_INFO_NODE_NEEDS_MARK //////////////////////////////////=//
//
// Bits are hard to come by in a Stub, especially a singular Stub which
// uses the cell content for an arbitrary value (e.g. API handles).  The
// space for the INFO bits is thus sometimes claimed for a node ("INODE"),
// which may need marking.
//
// !!! Future plans involve being able to dynamically switch out the info
// bits for a node, e.g. to hold a lock.  Then the info bits would be moved
// to the lock--which might itself be a feed or level (to avoid making a new
// identity).  Those features are just ideas for the moment, but if they came
// to pass this bit would also be synonymous with SERIES_FLAG_HOLD.
//
#define SERIES_FLAG_INFO_NODE_NEEDS_MARK \
    FLAG_LEFT_BIT(12)


//=//// SERIES_FLAG_13 ////////////////////////////////////////////////////=//
//
#define SERIES_FLAG_13 \
    FLAG_LEFT_BIT(13)


//=//// SERIES_FLAG_BLACK /////////////////////////////////////////////////=//
//
// This is a generic bit for the "coloring API", e.g. Is_Series_Black(),
// Flip_Series_White(), etc.  These let native routines engage in marking
// and unmarking nodes without potentially wrecking the garbage collector by
// reusing NODE_FLAG_MARKED.  Purposes could be for recursion protection or
// other features, to avoid having to make a map from Series to bool.
//
// !!! Not clear if this belongs in the SERIES_FLAG_XXX or not, but moving
// it here for now.
//
#define SERIES_FLAG_BLACK \
    FLAG_LEFT_BIT(14)


//=//// SERIES_FLAG_15 ////////////////////////////////////////////////////=//
//
#define SERIES_FLAG_15 \
    FLAG_LEFT_BIT(15)


//=//// BITS 16-23: SERIES SUBCLASS ("FLAVOR") ////////////////////////////=//
//
// Series subclasses keep a byte to tell which kind they are.  The byte is an
// enum which is ordered in a way that offers information (e.g. all the
// arrays are in a range, all the series with wide size of 1 are together...)
//
// 1. In lieu of typechecking cell is-a cell, we assume the macro finding
//    a field called ->leader with .bits in it is good enough.  All methods of
//    checking seem to add overhead in the debug build that isn't worth it.
//    To help avoid accidentally passing cell, the HeaderUnion in a Cell
//    is named "header" instead of "leader".
//
#define FLAVOR_BYTE(stub) \
    THIRD_BYTE(&(stub)->leader.bits)

#define FLAG_FLAVOR_BYTE(flavor)        FLAG_THIRD_BYTE(flavor)
#define FLAG_FLAVOR(name)               FLAG_FLAVOR_BYTE(FLAVOR_##name)


//=//// BITS 24-31: SUBCLASS FLAGS ////////////////////////////////////////=//
//
// These flags are those that differ based on which series subclass is used.
//
// This space is used currently for array flags to store things like whether
// the array ends in a newline.  It's a hodepodge of other bits which were
// rehomed while organizing the flavor bits.  These positions now have the
// ability to be more thought out after the basics of flavors are solved.
//

#define SERIES_FLAG_24    FLAG_LEFT_BIT(24)
#define SERIES_FLAG_25    FLAG_LEFT_BIT(25)
#define SERIES_FLAG_26    FLAG_LEFT_BIT(26)
#define SERIES_FLAG_27    FLAG_LEFT_BIT(27)
#define SERIES_FLAG_28    FLAG_LEFT_BIT(28)
#define SERIES_FLAG_29    FLAG_LEFT_BIT(29)
#define SERIES_FLAG_30    FLAG_LEFT_BIT(30)
#define SERIES_FLAG_31    FLAG_LEFT_BIT(31)



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
// !!! The current main application of series info is a byte's worth of space
// for the Series_Used() of series content that fits in the cell area, and
// flags pertaining to locking.  The idea of "popping out" that series info
// upon a hold lock being taken--such that the info bits move and the slot
// holds a locking pointer--is currently being thught about.  See the INODE()
// for the beginnings of that.


//=//// SERIES_INFO_0_IS_FALSE ////////////////////////////////////////////=//
//
// The INFO bits are resident immediately after the content description, and
// in the case of singular arrays a node is stored in the cell itself.  An
// array traversal might step outside the bounds, so it's easiest just to say
// the location is not a node to avoid writing it.
//
// !!! This can be reviewed if getting another bit seems important.
//
#define SERIES_INFO_0_IS_FALSE \
    FLAG_LEFT_BIT(0)
STATIC_ASSERT(SERIES_INFO_0_IS_FALSE == NODE_FLAG_NODE);


//=//// SERIES_INFO_1 /////////////////////////////////////////////////////=//
//
#define SERIES_INFO_1 \
    FLAG_LEFT_BIT(1)


//=//// SERIES_INFO_AUTO_LOCKED ///////////////////////////////////////////=//
//
// Some operations lock series automatically, e.g. to use a piece of data as
// map keys.  This approach was chosen after realizing that a lot of times,
// users don't care if something they use as a key gets locked.  So instead
// of erroring by telling them they can't use an unlocked series as a map key,
// this locks it but changes the SERIES_FLAG_HAS_FILE_LINE to implicate the
// point where the locking occurs.
//
// !!! The file-line feature is pending.
//
#define SERIES_INFO_AUTO_LOCKED \
    FLAG_LEFT_BIT(2)


//=//// SERIES_INFO_PROTECTED /////////////////////////////////////////////=//
//
// This indicates that the user had a tempoary desire to protect a series
// size or values from modification.  It is the usermode analogue of
// SERIES_INFO_FROZEN_DEEP, but can be reversed.
//
// Note: There is a feature in PROTECT (CELL_FLAG_PROTECTED) which protects
// a certain variable in a context from being changed.  It is similar, but
// distinct.  SERIES_INFO_PROTECTED is a protection on a series itself--which
// ends up affecting all values with that series in the payload.
//
#define SERIES_INFO_PROTECTED \
    FLAG_LEFT_BIT(3)


//=//// SERIES_INFO_FROZEN_DEEP ///////////////////////////////////////////=//
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
#define SERIES_INFO_FROZEN_DEEP \
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
// from SERIES_INFO_FROZEN_DEEP, which will never be cleared once set.
//
#define SERIES_INFO_HOLD \
    FLAG_LEFT_BIT(5)


//=//// SERIES_INFO_FROZEN_SHALLOW ////////////////////////////////////////=//
//
// A series can be locked permanently, but only at its own top level.
//
#define SERIES_INFO_FROZEN_SHALLOW \
    FLAG_LEFT_BIT(6)


//=//// SERIES_INFO_7 /////////////////////////////////////////////////////=//
//
#define SERIES_INFO_7 \
    FLAG_LEFT_BIT(7)


//=//// BITS 8-15 ARE Series_Used() FOR NON-DYNAMIC NON-ARRAYS ////////////=//

// SERIES_FLAG_DYNAMIC indicates that a series has a dynamically allocated
// portion, and it has a whole uintptr_t to use for the length.  However, if
// that flag is not set the payload is small, fitting in StubContentUnion
// where the allocation tracking information would be.
//
// If the data is an array, then the length can only be 0 or 1, since the
// tracking information is the same size as a cell.  This can be encoded by
// having the cell be poisoned or non-poisoned to know the length.
//
// For binaries and other non-arrays the length has to be stored somewhere.
// The third byte of the INFO is set aside for the purpose.
//
// !!! Currently arrays leverage this as 0 for a terminator of the singular
// array case.  However, long term zero termination of arrays is not being
// kept as a redundancy with the length.  It is costly to update and takes
// additional space from rounding up.
//
#define USED_BYTE(s) \
    SECOND_BYTE(&SERIES_INFO(s))

#define FLAG_USED_BYTE(len)     FLAG_SECOND_BYTE(len)


//=//// BITS 16-31 ARE SymId FOR SYMBOLS //////////////////////////////////=//
//
// These bits are currently unused by other types.  One reason to avoid using
// them is the concept that the INFO slot will be used to hold locking info
// for series, which would require a full pointer.
//



// ^-- STOP AT FLAG_LEFT_BIT(31) --^
//
// While 64-bit systems have another 32-bits available in the header, core
// functionality shouldn't require using them...only optimization features.

#define SERIES_INFO_MASK_NONE 0


//=////////////////////////////////////////////////////////////////////////=//
//
// SERIES STUB STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A Stub is normally the size of two Cells (though compiling with certain
// debug flags can add tracking information).  See %sys-rebnod.h for
// explanations of how obeying the header-in-first-slot convention allows a
// Stub to be distinguished from a Cell or a UTF-8 string and not run
// afoul of strict aliasing requirements.
//
// In order to help avoid confusion in optimizing macros that could be passed
// a cell vs. stub unintentionally, the header in a stub is called "leader",
// distinguishing it from the stub's "header".
//
// There are 3 basic layouts which can be overlaid inside the union:
//
//      Dynamic: [leader link [allocation tracking] info misc]
//     Singular: [leader link [cell] info misc]
//      Pairing: [[cell] [cell]]
//
// The singular form has space the *size* of a cell, but can be addressed as
// raw bytes used for UTF-8 strings or other smallish data.  If a Stub is
// aligned on a 64-bit boundary, the internal cell should be on a 64-bit
// boundary as well, even on a 32-bit platform where the header and link are
// each 32-bits.  See ALIGN_SIZE for notes on why this is important.
//
// `info` is not the start of a "Rebol Node" (e.g. either a Stub or Cell)
// But in the singular case it is positioned right where the next cell after
// the embedded Cell would be.  To lower the risk of stepping into that
// location and thinking it is a cell, it keeps the info bit corresponding to
// NODE_FLAG_CELL clear.
//
// Singulars have widespread applications in the system.  One is that a
// "single element array living in a series node" makes a very efficient
// implementation of an API handle to a value.  Plus it's used notably in the
// efficient implementation of FRAME!.  They also narrow the gap in overhead
// between COMPOSE [A (B) C] vs. REDUCE ['A B 'C] such that the memory cost
// of the array is nearly the same as just having another value in the array.
//
// Pairings are allocated from the Stub pool instead of their own to
// help exchange a common "currency" of allocation size more efficiently.
// They are used in the PAIR! datatype, but can have other interesting
// applications when exactly two values (with no termination) are needed.
//
// Most of the time, code does not need to be concerned about distinguishing
// Pairing from the Dynamic and Singular layouts--because it already knows
// which kind it has.  Only the GC needs to be concerned when marking
// and sweeping.
//


union StubBonusUnion {
    //
    // In R3-Alpha, the bias was not a full REBLEN but was limited in range to
    // 16 bits or so.  This means 16 info bits are likely available if needed
    // for dynamic series...though it would complicate the logic for biasing
    // to have to notice when you TAKE 65535 units from the head of a larger
    // series and need to allocate a new pointer (though this needs to be
    // done anyway, otherwise memory is wasted).
    //
    REBLEN bias;

    // Series nodes that do not use bias (e.g. context varlists) can use the
    // bonus slot for other information.
    //
    const Node* node;
};


struct StubDynamicStruct {
    //
    // `data` is the "head" of the series data.  It might not point directly
    // at the memory location that was returned from the allocator if it has
    // bias included in it.
    //
    // !!! We use `char*` here to ease debugging in systems that don't show
    // ASCII by default for unsigned characters, for when it's UTF-8 data.
    //
    char *data;

    // `used` is the count of *physical* elements.  If a series is byte-sized
    // and holding a UTF-8 string, then this may be a size in bytes distinct
    // than the count of "logical" elements, e.g. codepoints.  The actual
    // logical length in such cases will be in the MISC(length) field.
    //
    Length used;

    // `rest` is the total number of units from bias to end.  Having a
    // slightly weird name draws attention to the idea that it's not really
    // the "capacity", just the "rest of the capacity after the bias".
    //
    Length rest;

    // This is the 4th pointer on 32-bit platforms which could be used for
    // something when a series is dynamic.
    //
    union StubBonusUnion bonus;
};


union StubContentUnion {
    //
    // If the series data does not fit into the StubContent, then it must be
    // dynamically allocated.  This is the tracking structure for that
    // dynamic data allocation.
    //
    struct StubDynamicStruct dynamic;

    // If not(SERIES_FLAG_DYNAMIC), then 0 or 1 length arrays can be held in
    // the series node.  If the single cell holds an END, it's 0 length...
    // otherwise it's length 1.  This means Series_Used() for non-dynamic
    // arrays is technically available for other purposes.
    //
    union {
        Cell cell;

      #if DEBUG_USE_UNION_PUNS
        char utf8_pun[sizeof(Cell)];  // debug watchlist insight into UTF-8
        REBWCHAR ucs2_pun[sizeof(Cell)/sizeof(Codepoint)];  // wchar_t insight
      #endif
    } fixed;
};


union StubLinkUnion {
    //
    // If you assign one member in a union and read from another, then that's
    // technically undefined behavior.  But this field is used as the one
    // that is "trashed" in the debug build when the series is created, and
    // hopefully it will lead to the other fields reading garbage (vs. zero)
    //
  #if !defined(NDEBUG)
    void *trash;
  #endif

    // For LIBRARY!, the file descriptor.  This is set to NULL when the
    // library is not loaded.
    //
    // !!! As with some other types, this may not need the optimization of
    // being in the Series node--but be handled via user defined types
    //
    void *fd;

    // If a Node* is stored in the link field, it has to use this union
    // member for SERIES_INFO_LINK_NODE_NEEDS_MARK to see it.  To help make
    // the reference sites be unique for each purpose and still be type safe,
    // see the LINK() macro helpers.
    //
    union AnyUnion any;
};


// The `misc` field is an extra pointer-sized piece of data which is resident
// in the series node, and hence visible to all REBVALs that might be
// referring to the series.
//
union StubMiscUnion {
    //
    // Used to preload bad data in the debug build; see notes on link.trash
    //
  #if !defined(NDEBUG)
    void *trash;
  #endif

    // See ARRAY_FLAG_FILE_LINE.  Ordinary source series store the line number
    // here.  It perhaps could have some bits taken out of it, vs. being a
    // full 32-bit integer on 32-bit platforms or 64-bit integer on 64-bit
    // platforms...or have some kind of "extended line" flag which interprets
    // it as a dynamic allocation otherwise to get more bits.
    //
    LineNumber line;

    // Under UTF-8 everywhere, strings are byte-sized...so the series "used"
    // is actually counting *bytes*, not logical character codepoint units.
    // Series_Used() and String_Len() can therefore be different...String_Len()
    // on a string series comes from here, vs. just report the used units.
    //
    Length length;

    // some HANDLE!s use this for GC finalization
    //
    CLEANUP_CFUNC *cleaner;

    // Because a bitset can get very large, the negation state is stored
    // as a boolean in the series.  Since negating a bitset is intended
    // to affect all values, it has to be stored somewhere that all
    // REBVALs would see a change--hence the field is in the series.
    //
    bool negated;

    // If a Node* is stored in the misc field, it has to use this union
    // member for SERIES_INFO_MISC_NODE_NEEDS_MARK to see it.  To help make
    // the reference sites be unique for each purpose and still be type safe,
    // see the MISC() macro helpers.
    //
    union AnyUnion any;
};


// Some series flags imply the INFO is used not for flags, but for another
// markable pointer.  This is not legal for any series that needs to encode
// its Series_Used(), so only strings and arrays can pull this trick...when
// they are used to implement internal structures.
//
union StubInfoUnion {
    //
    // Using a union lets us see the underlying `uintptr_t` type-punned in
    // debug builds as bytes/bits.
    //
    union HeaderUnion flags;

    const Node* node;

    void* trash;
};


#if CPLUSPLUS_11
    struct StubStruct : public Node
#else
    struct StubStruct
#endif
{
    // See the description of SERIES_FLAG_XXX for the bits in this header.
    // It is in the same position as a Cell header, and the first byte
    // can be read via NODE_BYTE() to determine which it is.  It's named
    // "leader" to be distinct from a Cell's "header" to achieve a kind of
    // poor-man's macro typechecking which doesn't incur debug build costs.
    //
    union HeaderUnion leader;

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this series would want to be able
    // to see.  This cannot be done (easily) for properties that are held
    // in cells directly.
    //
    // This field is in the second pointer-sized slot in the Stub node to
    // push the `content` so it is 64-bit aligned on 32-bit platforms.  This
    // is because a cell may be the StubContentUnion, and a cell assumes
    // it is on a 64-bit boundary to start with...in order to position its
    // "payload" which might need to be 64-bit aligned as well.
    //
    // Use the LINK() macro to acquire this field...don't access directly.
    //
    union StubLinkUnion link;

    // `content` is the sizeof(Cell) data for the series, which is thus
    // 4 platform pointers in size.  If the series is small enough, the header
    // contains the size in bytes and the content lives literally in these
    // bits.  If it's too large, it will instead be a pointer and tracking
    // information for another allocation.
    //
    union StubContentUnion content;

    // `info` consists of bits that could apply equally to any series, and
    // that may need to be tested together as a group.  Make_Series()
    // calls presume all the info bits are initialized to zero, so any flag
    // that controls the allocation should be a SERIES_FLAG_XXX instead.
    //
    // !!! Only 32-bits are used on 64-bit platforms.  There could be some
    // interesting added caching feature or otherwise that would use
    // it, while not making any feature specifically require a 64-bit CPU.
    //
    union StubInfoUnion info;

    // This is the second pointer-sized piece of series data that is used
    // for various purposes, similar to link.
    //
    union StubMiscUnion misc;

  #if DEBUG_SERIES_ORIGINS || DEBUG_COUNT_TICKS
    Byte* guard;  // intentionally alloc'd and freed for use by panic()
    uintptr_t tick;  // also maintains sizeof(Stub) % sizeof(REBI64) == 0
  #endif
};


// In C++, String* and Array* are derived from Series.  This gives
// desirable type checking properties (like being able to pass an array to
// a routine that needs a series, but not vice versa).  And it also means
// that the fields are available.
//
// In order for the inheritance to be known, these definitions cannot occur
// until Series is fully defined.  So this is the earliest it can be done:
//
// https://stackoverflow.com/q/2159390/
//
#if CPLUSPLUS_11
    struct Binary : public Series {};
    struct String : public Binary {};  // strings can act as binaries
    struct Symbol : public String {};  // word-constrained strings

    struct BookmarkList : public Series {};

    struct Action : public Series {};
    struct Phase : public Action {};

    struct Context : public Series {};

    struct Map : public Series {};  // the "pairlist" is the identity

    struct KeyList : public Series {};
#else
    typedef Series Binary;
    typedef Series String;
    typedef Series Symbol;

    typedef Series BookmarkList;

    typedef Series Action;
    typedef Series Phase;

    typedef Series Context;

    typedef Series Map;

    typedef Series KeyList;
#endif


#define SERIES_MASK_SYMBOL \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(SYMBOL) \
        | SERIES_FLAG_FIXED_SIZE \
        | NODE_FLAG_MANAGED)


// We want to be able to enumerate keys by incrementing across them.  The
// things we increment across aren't Symbol stubs, but pointers to Symbol
// stubs... so a Key* is a pointer to a pointer.
//
typedef const Symbol* Key;


//=//// DON'T PUT ANY CODE (OR MACROS THAT MAY NEED CODE) IN THIS FILE! ///=//
//
// The %tmp-internals.h file has not been included, and hence none of the
// prototypes (even for things like Panic_Core()) are available.
//
// Even if a macro seems like it doesn't need code right at this moment, you
// might want to put some instrumentation into it, and that becomes a pain of
// manual forward declarations.
//
// So keep this file limited to structs and constants.  It's too long already.
//
//=////////////////////////////////////////////////////////////////////////=//
