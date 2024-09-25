//
//  File: %struct-stub.h
//  Summary: "Stub structure definitions preceding %tmp-internals.h"
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
// Flex is actually just a "Stub"... a small-ish fixed-size descriptor for
// Flex data.  Usually it contains a pointer to a larger allocation for the
// actual contents.  But if the Flex is small enough, the contents are
// embedded into the stub structure itself.
//
// Every TEXT!, BLOCK!, BINARY!, etc. in Rebol has a Flex.  Since Rebol does
// not depend on any data structure libraries--like C++'s std::vector--this
// means that the Flex is also used internally when there is a need for a
// dynamically growable contiguous memory structure.
//
// Flex behaves something like a "double-ended queue".  It can reserve
// capacity at both the tail and the head.  When data is taken from the head,
// it will retain that capacity...reusing it on later insertions at the head.
//
// The space at the head is called the "bias", and to save on pointer math
// per-access, the stored data pointer is actually adjusted to include the
// bias.  This biasing is backed out upon insertions at the head, and also
// must be subtracted completely to free the pointer using the address
// originally given by the allocator.
//
// A pool quickly grants and releases memory ranges that are sizeof(Stub)
// without needing to use malloc() and free() for each individual allocation.
// These nodes can also be enumerated in the pool without needing the Flex
// to be tracked via a linked list or other structure.  The garbage collector
// is one example of code that performs such an enumeration.
//
// A Flex Stub pointer will remain valid as long as outstanding references
// to the Flex exist in values visible to the GC.  On the other hand, the
// Flex's data pointer may be freed and reallocated to respond to the needs
// of resizing.  (In the future, it may be reallocated just as an idle task
// by the GC to reclaim or optimize space.)
//
//    *** THIS MEANS POINTERS INTO THE Flex_Data() FOR A MANAGED Flex
//    MUST NOT BE HELD ONTO ACROSS EVALUATIONS, WITHOUT SPECIAL PROTECTION
//    OR ACCOMMODATION.**
//
// Flex may be either manually memory managed or delegated to the garbage
// collector.  Free_Unmanaged_Flex() may only be called on manual Flex.
// See Manage_Flex()/Push_GC_Guard() for remarks on how to work safely
// with pointers to garbage-collected Flexes, to avoid having them be GC'd
// out from under the code while working with them.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Because a Stub contains a union member that embeds a Cell directly,
//   `Cell` must be fully defined before this file can compile.  Hence
//   %struct-cell.h must already be included.
//
// * For the API of operations available on Flex types, see %stub-flex.h
//
// * Array is a Flex that contains Rebol Cells.  It has many concerns
//   specific to special treatment and handling, in interaction with the
//   garbage collector as well as handling "relative vs specific" values.
//
// * Several related types (Action* for function, VarList* for context) are
//   actually stylized Arrays.  They are laid out with special values in their
//   content (e.g. at the [0] index), or by links to other Flexes in their
//   `->misc` and `->link` fields of the Flex Stub.  Hence Flexes are the basic
//   building blocks of nearly all variable-size structures in the system.
//
// * The unit size in a Flex is known as the "width".  R3-Alpha used a
//   byte for this to get from unit sizes ranging from 0-255 bytes.  Ren-C
//   uses that byte for the "Flavor" of the Stub (a name distinguishing
//   the Stub in a way parallel to a Cell's "Heart") and then maps from Flavor
//   to Size.
//


//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX_FLAG_0 - FLEX_FLAG_7 are NODE_FLAG_XXX)
//
//=////////////////////////////////////////////////////////////////////////=//

// At one time all the flags were aliased, like:
//
//     #define FLEX_FLAG_MANAGED NODE_FLAG_MANAGED
//     #define FLEX_FLAG_FREE NODE_FLAG_FREE
//     ...
//
// This created weird inconsistencies where it would make an equal amount of
// sense to pass FLEX_FLAG_MANAGED or NODE_FLAG_MANAGED, and introduces the
// risk that the checks might be performed on pointers that don't know if
// what they point at is a Cell or a Stub.  The duplication was removed, and
// now you say `Is_Node_Managed(flex)` vs. `Get_Flex_Flag(flex, MANAGED)` etc.
//
// Aliases for the NODE_FLAG_GC_ONE and NODE_FLAG_GC_TWO are kept, as there
// is no corresponding ambiguity.


//=//// FLEX_FLAG_LINK_NODE_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that a Flex's LINK() field is the `any.node`, and should
// be marked (if not null).
//
// Note: Even if this flag is not set, *link.any might still be a node*...
// just not one that should be marked.
//
#define FLEX_FLAG_LINK_NODE_NEEDS_MARK \
    NODE_FLAG_GC_ONE


//=//// FLEX_FLAG_MISC_NODE_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that a Flex's MISC() field is the `any.node`, and should
// be marked (if not null).
//
// Note: Even if this flag is not set, *misc.any might still be a node*...
// just not one that should be marked.
//
#define FLEX_FLAG_MISC_NODE_NEEDS_MARK \
    NODE_FLAG_GC_TWO


//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX <<HEADER>> FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// Flex has two places to store flags...in the "header" and in the "info".
// The following are the FLEX_FLAG_XXX that are used in the header, while
// the FLEX_INFO_XXX flags will be found in the info.
//
// ** Make_Flex() takes FLEX_FLAG_XXX as a parameter, so anything that
// controls Flex creation should be a _FLAG_ as opposed to an _INFO_! **
//
// (Other general rules might be that bits that are to be tested or set as
// a group should be in the same flag group.  Perhaps things that don't change
// for the lifetime of the Flex might prefer header to the info, too?
// Such things might help with caching.)
//

#define FLEX_FLAGS_NONE \
    0  // helps locate places that want to say "no flags"


//=//// FLEX_FLAG_8 /////////////////////////////////////////////////////=//
//
#define FLEX_FLAG_8 \
    FLAG_LEFT_BIT(8)


//=//// FLEX_FLAG_FIXED_SIZE ////////////////////////////////////////////=//
//
// This means a Flex cannot be expanded or contracted.  Values within the
// Flex are still writable (assuming it isn't otherwise locked).
//
// !!! Is there checking in all paths?  Do Flex contractions check this?
//
// One important reason for ensuring a Flex is fixed size is to avoid
// the possibility of the data pointer being reallocated.  This allows
// code to ignore the usual rule that it is unsafe to hold a pointer to
// a value in the Flex data (still might have to check for inaccessible).
//
// !!! Strictly speaking, FLEX_FLAG_NO_RELOCATE could be different
// from fixed size... if there would be a reason to reallocate besides
// changing size (such as memory compaction).  For now, just make the two
// equivalent but let the callsite distinguish the intent.
//
#define FLEX_FLAG_FIXED_SIZE \
    FLAG_LEFT_BIT(9)

#define FLEX_FLAG_DONT_RELOCATE FLEX_FLAG_FIXED_SIZE


//=//// FLEX_FLAG_POWER_OF_2 ////////////////////////////////////////////=//
//
// R3-Alpha would round some memory allocation requests up to a power of 2.
// This may well not be a good idea:
//
// http://stackoverflow.com/questions/3190146/
//
// But leaving it alone for the moment: there is a mechanical problem that the
// specific number of bytes requested for allocating Flex data is not saved.
// Only the Flex capacity measured in units is known.
//
// Hence this flag is marked on the Stub, which is enough to recreate the
// actual number of allocator bytes to release when the Flex is freed.  The
// memory is accurately tracked for GC decisions, and balances back to 0 at
// program end.
//
// Note: All R3-Alpha's Flexes had widths that were powers of 2, so this bit
// was not necessary there.
//
#define FLEX_FLAG_POWER_OF_2 \
    FLAG_LEFT_BIT(10)


//=//// FLEX_FLAG_DYNAMIC ///////////////////////////////////////////////=//
//
// A small Flex will fit the data into the Flex Stub if it is small enough.
// This flag is set when a Flex uses its `content` for tracking information
// instead of the actual data itself.
//
// It can also be passed in at Flex creation time to force an allocation to
// be dynamic.  This is because some code is more interested in performance
// gained by being able to assume where to look for the data pointer and the
// length (e.g. paramlists and context varlists/keylists).  So passing this
// flag into Flex creation routines avoids creating the optimized form.
//
// Note: At one time the USED_BYTE() of 255 was the signal for this.  But
// being able to pass in the flag to creation routines easily, and make the
// test easier with Get_Flex_Flag(), was seen as better.  Also, this means
// a dynamic Flex has an entire byte worth of free data available to use.
//
#define FLEX_FLAG_DYNAMIC \
    FLAG_LEFT_BIT(11)


//=//// FLEX_FLAG_INFO_NODE_NEEDS_MARK //////////////////////////////////=//
//
// Bits are hard to come by in a Stub, especially a singular Stub which
// uses the cell content for an arbitrary value (e.g. API handles).  The
// space for the INFO bits is thus sometimes claimed for a node ("INODE"),
// which may need marking.
//
#define FLEX_FLAG_INFO_NODE_NEEDS_MARK \
    FLAG_LEFT_BIT(12)


//=//// FLEX_FLAG_13 ////////////////////////////////////////////////////=//
//
#define FLEX_FLAG_13 \
    FLAG_LEFT_BIT(13)


//=//// FLEX_FLAG_BLACK /////////////////////////////////////////////////=//
//
// This is a generic bit for the "coloring API", e.g. Is_Flex_Black(),
// Flip_Flex_White(), etc.  These let native routines engage in marking
// and unmarking Flexes without potentially wrecking the garbage collector by
// reusing NODE_FLAG_MARKED.  Purposes could be for recursion protection or
// other features, to avoid having to make a map from Flex to bool.
//
// !!! Not clear if this belongs in the FLEX_FLAG_XXX or not, but moving
// it here for now.
//
#define FLEX_FLAG_BLACK \
    FLAG_LEFT_BIT(14)


//=//// FLEX_FLAG_15 ////////////////////////////////////////////////////=//
//
#define FLEX_FLAG_15 \
    FLAG_LEFT_BIT(15)


//=//// BITS 16-23: FLEX SUBCLASS ("FLAVOR") //////////////////////////////=//
//
// Flex subclasses use a byte to tell which kind they are.  The byte is an
// enum which is ordered in a way that offers information (e.g. all the
// arrays are in a range, all the Flexes with width of 1 are together...)
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
// These flags are those that differ based on which Flex subclass is used.
//
// This space is used currently for Array flags to store things like whether
// the array ends in a newline.  It's a hodepodge of other bits which were
// rehomed while organizing the flavor bits.  These positions now have the
// ability to be more thought out after the basics of flavors are solved.
//

#define FLEX_FLAG_24    FLAG_LEFT_BIT(24)
#define FLEX_FLAG_25    FLAG_LEFT_BIT(25)
#define FLEX_FLAG_26    FLAG_LEFT_BIT(26)
#define FLEX_FLAG_27    FLAG_LEFT_BIT(27)
#define FLEX_FLAG_28    FLAG_LEFT_BIT(28)
#define FLEX_FLAG_29    FLAG_LEFT_BIT(29)
#define FLEX_FLAG_30    FLAG_LEFT_BIT(30)
#define FLEX_FLAG_31    FLAG_LEFT_BIT(31)



//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX <<INFO>> BITS
//
//=////////////////////////////////////////////////////////////////////////=//
//
// See remarks on FLEX <<FLAG>> BITS about the two places where a Flex stores
// bits.  These are the info bits, which are more likely to be changed over
// the lifetime of the Flex--defaulting to FALSE.
//
// !!! The current main application of Flex info is a byte's worth of space
// for the Flex_Used() of Flex content that fits in the Cell area, and
// flags pertaining to locking.  The idea of "popping out" that Flex info
// upon a hold lock being taken--such that the info bits move and the slot
// holds a locking pointer--is currently being thought about.  See the INODE()
// for the beginnings of that.


//=//// FLEX_INFO_0_IS_FALSE ////////////////////////////////////////////=//
//
// The INFO bits are resident immediately after the content description, and
// in the case of singular Arrays a cell is stored in the Stub itself.  An
// array traversal might step outside the bounds, so it's easiest just to say
// the location is not a Node to avoid writing it.
//
// !!! This can be reviewed if getting another bit seems important.
//
#define FLEX_INFO_0_IS_FALSE \
    FLAG_LEFT_BIT(0)
STATIC_ASSERT(FLEX_INFO_0_IS_FALSE == NODE_FLAG_NODE);


//=//// FLEX_INFO_1 /////////////////////////////////////////////////////=//
//
#define FLEX_INFO_1 \
    FLAG_LEFT_BIT(1)


//=//// FLEX_INFO_AUTO_LOCKED ///////////////////////////////////////////=//
//
// Some operations lock Flexes automatically, e.g. to use a piece of data as
// MAP! keys.  This approach was chosen after realizing that a lot of times,
// users don't care if something they use as a key gets locked.  So instead
// of erroring by telling them they can't use an unlocked Flex as a MAP! key,
// this locks it but changes the FLEX_FLAG_HAS_FILE_LINE to implicate the
// point where the locking occurs.
//
// !!! The file-line feature is pending.
//
#define FLEX_INFO_AUTO_LOCKED \
    FLAG_LEFT_BIT(2)


//=//// FLEX_INFO_PROTECTED /////////////////////////////////////////////=//
//
// This indicates that the user had a tempoary desire to protect a Flex
// size or values from modification.  It is the usermode analogue of
// FLEX_INFO_FROZEN_DEEP, but can be reversed.
//
// Note: There is a feature in PROTECT (CELL_FLAG_PROTECTED) which protects
// a certain variable in a context from being changed.  It is similar, but
// distinct.  FLEX_INFO_PROTECTED is a protection on a Flex itself--which
// ends up affecting all values with that Flex in the payload.
//
#define FLEX_INFO_PROTECTED \
    FLAG_LEFT_BIT(3)


//=//// FLEX_INFO_FROZEN_DEEP ///////////////////////////////////////////=//
//
// Indicates that the length or values cannot be modified...ever.  It has been
// locked and will never be released from that state for its lifetime, and if
// it's an Array then everything referenced beneath it is also frozen.  This
// means that if a read-only copy of it is required, no copy needs to be made.
//
// (Contrast this with the temporary condition like caused by something
// like FLEX_INFO_HOLD or FLEX_INFO_PROTECTED.)
//
// Note: This and the other read-only Flex checks are honored by some layers
// of abstraction, but if one manages to get a raw non-const pointer into a
// value in the Flex data...then by that point it cannot be enforced.
//
#define FLEX_INFO_FROZEN_DEEP \
    FLAG_LEFT_BIT(4)


//=//// FLEX_INFO_HOLD //////////////////////////////////////////////////=//
//
// Set in the header whenever some stack-based operation wants a temporary
// hold on a Flex, to give it a protected state.  This will happen with a
// DO, or PARSE, or enumerations.  Even REMOVE-EACH will transition the Flex
// it is operating on into a HOLD state while the removal signals are being
// gathered, and apply all the removals at once before releasing the hold.
//
// It will be released when the execution is finished, which distinguishes it
// from FLEX_INFO_FROZEN_DEEP, which will never be cleared once set.
//
#define FLEX_INFO_HOLD \
    FLAG_LEFT_BIT(5)


//=//// FLEX_INFO_FROZEN_SHALLOW ////////////////////////////////////////=//
//
// A Flex can be locked permanently at its top level only, if you want.
//
#define FLEX_INFO_FROZEN_SHALLOW \
    FLAG_LEFT_BIT(6)


//=//// FLEX_INFO_7 /////////////////////////////////////////////////////=//
//
#define FLEX_INFO_7 \
    FLAG_LEFT_BIT(7)


//=//// BITS 8-15 ARE Flex_Used() FOR NON-DYNAMIC NON-ARRAYS ////////////=//

// FLEX_FLAG_DYNAMIC indicates that a Flex has a dynamically allocated
// portion, and it has a whole uintptr_t to use for the length.  However, if
// that flag is not set the payload is small, fitting in StubContentUnion
// where the allocation tracking information would be.
//
// If the data is an Array, then the length can only be 0 or 1, since the
// tracking information is the same size as a cell.  This can be encoded by
// having the cell be poisoned or non-poisoned to know the length.
//
// For Binary and other non-Arrays the length has to be stored somewhere.
// The third byte of the INFO is set aside for the purpose.
//
#define USED_BYTE(f) \
    SECOND_BYTE(&FLEX_INFO(f))

#define FLAG_USED_BYTE(len)     FLAG_SECOND_BYTE(len)


//=//// BITS 16-31 ARE SymId FOR SYMBOLS //////////////////////////////////=//
//
// These bits are currently unused by other types.  One reason to avoid using
// them is the concept that the INFO slot will be used to hold locking info
// for Flex, which would require a full pointer.
//



// ^-- STOP AT FLAG_LEFT_BIT(31) --^
//
// While 64-bit systems have another 32-bits available in the header, core
// functionality shouldn't require using them...only optimization features.

#define FLEX_INFO_MASK_NONE 0


//=////////////////////////////////////////////////////////////////////////=//
//
// FLEX STUB STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//
//
// A Stub is normally the size of two Cells (though compiling with certain
// debug flags can add tracking information).  See %struct-node.h for
// explanations of how obeying the header-in-first-slot convention allows a
// Stub to be distinguished from a Cell or a UTF-8 string and not run
// afoul of strict aliasing requirements.
//
// In order to help avoid confusion in optimizing macros that could be passed
// a Cell vs. Stub unintentionally, the header in a stub is called `->leader`,
// distinguishing it from the stub's `->header`.
//
// There are two layouts which the union can be interpreted as:
//
//      Dynamic: [leader link [allocation tracking] info misc]
//     Singular: [leader link [Cell] info misc]
//
// The singular form has space the *size* of a Cell, but can be addressed as
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
// "single-Element Array living in a Flex Stub" makes a very efficient
// implementation of an API handle to a Value.  Plus it's used notably in the
// efficient implementation of FRAME!.  They also narrow the gap in overhead
// between COMPOSE [A (B) C] vs. REDUCE ['A B 'C] such that the memory cost
// of the Array is nearly the same as just having another Cell in the array.
//
// Pairings are not Stubs, but allocated from the Stub pool in order to
// help exchange a common "currency" of allocation size more efficiently.
// They are used in the PAIR! datatype, but have other applications when
// exactly two values are needed (e.g. paths or tuples like `a/b` or `a.b`)
//
//      Pairing: [[Cell] [Cell]]  ; sizeof(Pairing) = sizeof(Stub)
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
    // for a dynamic Flex...though it would complicate the logic for biasing
    // to have to notice when you TAKE 65535 units from the head of a larger
    // Flex and need to allocate a new pointer (though this needs to be
    // done anyway, otherwise memory is wasted).
    //
    REBLEN bias;

    // Flex Stubs that do not use bias (e.g. context varlists) can use the
    // bonus slot for other information.
    //
    const Node* node;
};


struct StubDynamicStruct {
    //
    // `data` is the "head" of the Flex data.  It might not point directly
    // at the memory location that was returned from the allocator if it has
    // bias included in it.
    //
    // !!! We use `char*` here to ease debugging in systems that don't show
    // ASCII by default for unsigned characters, for when it's UTF-8 data.
    //
    char *data;

    // `used` is the count of *physical* units.  If a Flex is byte-sized
    // and holding a UTF-8 String, then this may be a size in bytes distinct
    // than the count of "logical" units, e.g. codepoints.  The actual
    // logical length in such cases will be in the MISC(length) field.
    //
    Length used;

    // `rest` is the total number of units from bias to end.  Having a
    // slightly weird name draws attention to the idea that it's not really
    // the "capacity", just the "rest of the capacity after the bias".
    //
    Length rest;

    // This is the 4th pointer on 32-bit platforms which could be used for
    // something when a Flex is dynamic.
    //
    union StubBonusUnion bonus;
};


union StubContentUnion {
    //
    // If the Flex data does not fit into the StubContent, then it must be
    // dynamically allocated.  This is the tracking structure for that
    // dynamic data allocation.
    //
    struct StubDynamicStruct dynamic;

    // If not(FLEX_FLAG_DYNAMIC), then 0 or 1 length arrays can be held in
    // the Flex Stub.  If the single Cell holds a "Poison", it's 0 length...
    // otherwise it's length 1.  This means Flex_Used() for non-dynamic
    // Arrays is technically available for other purposes.
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
    // For LIBRARY!, the file descriptor.  This is set to NULL when the
    // library is not loaded.
    //
    // !!! As with some other types, this may not need the optimization of
    // being in the Flex Stub--but be handled via user defined types
    //
    void *fd;

    // If a Node* is stored in the link field, it has to use this union
    // member for FLEX_INFO_LINK_NODE_NEEDS_MARK to see it.  To help make
    // the reference sites be unique for each purpose and still be type safe,
    // see the LINK() macro helpers.
    //
    union AnyUnion any;
};


// The `misc` field is an extra pointer-sized piece of data which is resident
// in the Flex Stub, and hence visible to all Cells that might be
// referring to the Flex.
//
union StubMiscUnion {
    //
    // See ARRAY_FLAG_FILE_LINE.  Ordinary source Arrays store the line number
    // here.  It perhaps could have some bits taken out of it, vs. being a
    // full 32-bit integer on 32-bit platforms or 64-bit integer on 64-bit
    // platforms...or have some kind of "extended line" flag which interprets
    // it as a dynamic allocation otherwise to get more bits.
    //
    LineNumber line;

    // Under UTF-8 everywhere, Strings are byte-sized...so the Flex "used"
    // is actually counting *bytes*, not logical character codepoint units.
    // Flex_Used() and String_Len() can therefore be different...String_Len()
    // on a String Flex comes from here, vs. just report the used units.
    //
    Length length;

    // some HANDLE!s use this for GC finalization
    //
    CLEANUP_CFUNC *cleaner;

    // Because a BITSET! can get very large, the negation state is stored
    // as a boolean in the Flex.  Since negating a BITSET! is intended
    // to affect all references, it has to be stored somewhere that all
    // Cells would see a change--hence the field is in the Flex.
    //
    bool negated;

    // If a Node* is stored in the misc field, it has to use this union
    // member for FLEX_INFO_MISC_NODE_NEEDS_MARK to see it.  To help make
    // the reference sites be unique for each purpose and still be type safe,
    // see the MISC() macro helpers.
    //
    union AnyUnion any;
};


// Some Flex flags imply the INFO is used not for flags, but for another
// markable pointer.  This is not legal for any Flex that needs to encode
// its Flex_Used(), so only String and Array can pull this trick...when
// they are used to implement internal structures.
//
union StubInfoUnion {
    //
    // Using a union lets us see the underlying `uintptr_t` type-punned in
    // debug builds as bytes/bits.
    //
    union AnyUnion any;
};


#if CPLUSPLUS_11
    struct StubStruct : public Node
#else
    struct StubStruct
#endif
{
    // See the description of FLEX_FLAG_XXX for the bits in this header.
    // It is in the same position as a Cell header, and the first byte
    // can be read via NODE_BYTE() to determine which it is.  It's named
    // "leader" to be distinct from a Cell's "header" to achieve a kind of
    // poor-man's macro typechecking which doesn't incur debug build costs.
    //
    union HeaderUnion leader;

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this Flex would want to be able
    // to see.  This cannot be done (easily) for properties that are held
    // in a Cell directly.
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

    // `content` is the sizeof(Cell) data for the Flex, which is thus
    // 4 platform pointers in size.  If the Flex is small enough, the header
    // contains the size in bytes and the content lives literally in these
    // bits.  If it's too large, it will instead be a pointer and tracking
    // information for another allocation.
    //
    union StubContentUnion content;

    // `info` consists of bits that could apply equally to any Flex, and
    // that may need to be tested together as a group.  Make_Flex()
    // calls presume all the info bits are initialized to zero, so any flag
    // that controls the allocation should be a FLEX_FLAG_XXX instead.
    //
    // !!! Only 32-bits are used on 64-bit platforms.  There could be some
    // interesting added caching feature or otherwise that would use
    // it, while not making any feature specifically require a 64-bit CPU.
    //
    union StubInfoUnion info;

    // This is the second pointer-sized piece of Flex data that is used
    // for various purposes, similar to link.
    //
    union StubMiscUnion misc;

  #if DEBUG_FLEX_ORIGINS || DEBUG_COUNT_TICKS
    Byte* guard;  // intentionally alloc'd and freed for use by panic()
    uintptr_t tick;  // also maintains sizeof(Stub) % sizeof(REBI64) == 0
  #endif
};


typedef Stub Flex;


// In C++, Binary, String, Array, etc. are derived from Flex.  This gives
// desirable type checking properties (like being able to pass an Array to
// a routine that needs a Flex, but not vice versa).  And it also means
// that the base class fields are available in the derived classes.
//
// In order for the inheritance to be known, these definitions cannot occur
// until Flex is fully defined.  So this is the earliest it can be done:
//
// https://stackoverflow.com/q/2159390/
//
#if CPLUSPLUS_11
    struct Binary : public Flex {};  // used by BLOB!
    struct String : public Binary {};  // UTF8-constrained Binary
    struct Symbol : public String {};  // WORD!-constrained immutable String

    struct VarList : public Flex {};  // Array is implementation detail

    struct BookmarkList : public Flex {};

    struct Map : public Flex {};  // the "pairlist" is the identity

    struct KeyList : public Flex {};
#else
    typedef Flex Binary;
    typedef Flex String;
    typedef Flex Symbol;

    typedef Flex VarList;

    typedef Flex BookmarkList;

    typedef Flex Map;

    typedef Flex KeyList;
#endif

// It may become interesting to say that a specifier can be a pairing or
// a Value* of some kind, but currently all instances are array-derived.
//
typedef Stub Specifier;


#define FLEX_MASK_SYMBOL \
    (NODE_FLAG_NODE \
        | FLAG_FLAVOR(SYMBOL) \
        | FLEX_FLAG_FIXED_SIZE \
        | NODE_FLAG_MANAGED)


// We want to be able to enumerate keys by incrementing across them.  The
// things we increment across aren't Symbol Stubs, but pointers to Symbol
// Stubs... so a Key* is a pointer to a pointer.
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
