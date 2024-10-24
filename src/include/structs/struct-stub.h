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
// Stubs are small fixed-size structures, that are the basic building block
// of GC-trackable entities in the system.  They are tailored for implementing
// the "Flex" resizable vector-like type...see %struct-flex.h...but are also
// used for other purposes (all Flex are Stubs, but not all Stubs are Flex).
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
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * Because a Stub contains a union member that embeds a Cell directly,
//   `Cell` must be fully defined before this file can compile.  Hence
//   %struct-cell.h must already be included.
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


//=//// BITS 16-23: STUB SUBCLASS ("FLAVOR") //////////////////////////////=//
//
// Stub subclasses use a byte to tell which kind they are.  The byte is an
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
// STUB STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//


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
