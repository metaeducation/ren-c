//
//  file: %struct-stub.h
//  summary: "Stub structure definitions preceding %tmp-internals.h"
//  project: "Rebol 3 Interpreter and Run-time (Ren-C branch)"
//  homepage: https://github.com/metaeducation/ren-c/
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
// the "Flex" resizable vector-like type (see %struct-flex.h).  But while all
// Flex are Stubs, not all Stubs are Flex...some use bits for other purposes.
//
// A Stub is typically 8 platform pointers in size (though certain debug flags
// expand the size to add tracking information).  It is defined as a union
// with with two different layouts:
//
//      Dynamic: [leader link [allocation-tracking] info misc]
//      Compact: [leader link [-sizeof(Cell)-data-] info misc]
//
// Choosing this size means that the same memory Pool that holds Stubs can
// also hold GC-trackable entities representing two 4-platform-pointer-Cells:
//
//      Pairing: [[-------Cell--------] [-------Cell--------]]
//
// A Compact Stub has data space that fits a Cell, but can also be addressed
// as raw bytes used for UTF-8 strings or other smallish data.  If a Stub is
// aligned on a 64-bit boundary, a Compact Stub's Cell should be on a 64-bit
// boundary as well, even on a 32-bit platform where the header and link are
// each 32-bits.  See ALIGN_SIZE for notes on why this is important.
//
// Compact Stubs have widespread applications in the system.  One is that a
// "single-Cell living in a Compact Stub" offers an efficient way to implement
// a tracking entity for API Value handles.  They also narrow the gap in
// overhead between COMPOSE [A (B) C] vs. REDUCE ['A B 'C] such that memory
// cost of a 1-element Array only adds 8 platform pointers.
//
//=//// NOTES /////////////////////////////////////////////////////////////=//
//
// * In order to help avoid confusion in optimizing macros that could be
//   passed a Cell vs. Stub unintentionally, the header in a Stub is called
//   `->leader`, distinguishing it from the stub's `->header`.
//
// * See %struct-base.h for how obeying the header-in-first-slot convention
//   allows a Stub to be distinguished from a Cell or a UTF-8 string, and not
//   run afoul of strict aliasing requirements!
//
// * While sizeof(Pairing) = sizeof(Stub), Pairings are not Stubs.  They are
//   used in the PAIR! datatype, but have other applications when exactly two
//   elements are needed (e.g. paths or tuples like `a/b` or `a.b`)
//
// * Because a Stub contains a union member that embeds a Cell directly,
//   `Cell` must be fully defined before this file can compile.  Hence
//   %struct-cell.h must already be included.
//


#define STUB_MASK_0  0  // help locate places that treat zero leader specially


//=////////////////////////////////////////////////////////////////////////=//
//
// BITS 0-7: TAKEN FOR BASE_FLAG_XXX
//
//=////////////////////////////////////////////////////////////////////////=//

// At one time all the flags were aliased, like:
//
//     #define STUB_FLAG_MANAGED  BASE_FLAG_MANAGED
//     #define STUB_FLAG_FREE  BASE_FLAG_UNREADABLE
//     ...
//
// This created weird inconsistencies where it would make an equal amount of
// sense to pass STUB_FLAG_MANAGED or BASE_FLAG_MANAGED, and introduces the
// risk that the checks might be performed on pointers that don't know if
// what they point at is a Cell or a Stub.  The duplication was removed, and
// now say `Is_Base_Managed(stub)` vs. `Get_Stub_Flag(stub, MANAGED)` etc.
//
// Aliases for the BASE_FLAG_GC_ONE and BASE_FLAG_GC_TWO are kept, as there
// is no corresponding ambiguity.


//=//// STUB_FLAG_LINK_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that the Stub.link.node field is in use, and should be
// marked (if not null).
//
// Note: Even if this flag is not set, *link.base might still be assigned*,
// just not to a Base that needs to be marked.
//
#define STUB_FLAG_LINK_NEEDS_MARK \
    BASE_FLAG_GC_ONE


//=//// STUB_FLAG_MISC_NEEDS_MARK //////////////////////////////////=//
//
// This indicates that the Stub.misc.node field is in use, and should be
// marked (if not null).
//
// Note: Even if this flag is not set, *misc.base might still be assigned*,
// just not to a Base that needs to be marked.
//
#define STUB_FLAG_MISC_NEEDS_MARK \
    BASE_FLAG_GC_TWO


//=////////////////////////////////////////////////////////////////////////=//
//
// BITS 8-15: STUB SUBCLASS ("FLAVOR") STORED IN "TASTE" BYTE
//
//=////////////////////////////////////////////////////////////////////////=//

// Stub subclasses use a byte to tell which kind they are.  The byte is an
// enum which is ordered in a way that offers information (e.g. all the
// Stubs that hold Cells are in a range, all the Flexes with width of 1
// are together...)
//
// The byte is called the TASTE_BYTE and not FLAVOR_BYTE, because the latter
// would make it look like one of the values in the Flavor enumerated type.
// (In fact, FLAVOR_BYTES is specifically one of the Flavor values.)  Taste
// is a weird name (weirder than Flavor?) but you don't see it often since
// usually Stub_Flavor() is used to get the value, and FLAG_FLAVOR() in the
// process of setting it.
//
// Note: Flavor does not have an analogue to TYPE_0 and ExtraHeart, where
// extensions can take over something like Stub.misc to get MiscFlavor and
// uniquely identify their extension Stubs.  Instead they have to use the
// generic FLAVOR_CELLS, FLAVOR_POINTERS, and FLAVOR_BYTES.  This gives them
// freedom in terms of how to use Stub.misc, Stub.link, Stub.info, and
// Stub.bonus ... but there's no identity mechanism standardized that would
// distinguish one extension's Stubs from another.
//
// 1. In lieu of typechecking stub is-a Stub, we assume the macro finding
//    a field called ->leader with .bits in it is good enough.  All methods of
//    checking seem to add overhead in the checked build that isn't worth it.
//    To help avoid accidentally passing Cell, the HeaderUnion in a Stub
//    is named "leader" instead of "header".

#define TASTE_BYTE(stub) \
    SECOND_BYTE(&(stub)->leader.bits)  // assume has ->leader means Stub [1]

#define FLAG_TASTE_BYTE(flavor)         FLAG_SECOND_BYTE(flavor)
#define FLAG_FLAVOR(name)               FLAG_TASTE_BYTE(FLAVOR_##name)


//=////////////////////////////////////////////////////////////////////////=//
//
// BITS 16-23: STUB (AND FLEX STUB) LEADER FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//

// These relatively scarce flags are shared with Flex as being flags that
// would apply to all Flex, regardless of subclass.  It would be technically
// possible for non-Flex Stubs to have alternate purposes for any FLEX_FLAG
// in this range, but it's simpler if they do whatever they do with a
// flag applicable to their subclass.


//=//// STUB_FLAG_INFO_NEEDS_MARK ////////////////////////////////////=//
//
// Bits are hard to come by in a Stub, especially a Compact Stub which
// uses the cell content for an arbitrary value (e.g. API handles).  The
// space for the INFO bits is thus sometimes claimed for a node
//
// This indicates that the Stub.info.base field is in use, and should be
// marked (if not null).
//
// Note: Even if this flag is not set, *info.base might still be assigned*,
// just not to a Base that needs to be marked.
//
#define STUB_FLAG_INFO_NEEDS_MARK \
    FLAG_LEFT_BIT(16)


//=//// STUB_FLAG_DYNAMIC /////////////////////////////////////////////////=//
//
// (Note: While only Flex Stubs will set this flag, it is considered a Stub
// flag and not a Flex flag, in order to make handling of the case where a
// Stub contains a cell payload more uniform.)
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
// test easier with Get_Stub_Flag(), was seen as better.  Also, this means
// a dynamic Flex has an entire byte worth of free data available to use.
//
#define STUB_FLAG_DYNAMIC \
    FLAG_LEFT_BIT(17)


//=//// STUB_FLAG_BLACK /////////////////////////////////////////////////=//
//
// This is a generic bit for the "coloring API", e.g. Is_Stub_Black(),
// Flip_Stub_White(), etc.  These let native routines engage in marking
// and unmarking Flexes without potentially wrecking the garbage collector by
// reusing BASE_FLAG_MARKED.  Purposes could be for recursion protection or
// other features, to avoid having to make a map from Stub to bool.
//
#define STUB_FLAG_BLACK \
    FLAG_LEFT_BIT(18)


//=//// STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY ////////////////////////////////=//
//
// When a stub gets GC'd, it may need to do something before it goes away.
//
// Diminish_Stub() uses this flag to indicate whether it has to bother running
// a switch() statement on the Stub_Flavor() to see if there's any handling
// for that flavor.  And if it runs the switch() statement but doesn't have
// a case for that Flavor, it assumes that the type wants to run an arbitrary
// function in Stub.misc.stub_cleaner
//
// (Note that there is also MISC_HANDLE_CLEANER(stub), which is a similar
// feature but the callback takes a Cell pointer instead of a Stub pointer.
// This prevents the need to have a StubCleaner that uses up the misc just
// to call a function that takes a Cell which would have to be stored
// somewhere else.  Hence FLAVOR_HANDLE has an instance in the switch() of
// Diminish_Stub() that does this call, vs using MISC_STUB_CLEANER.)
//
#define STUB_FLAG_CLEANS_UP_BEFORE_GC_DECAY \
    FLAG_LEFT_BIT(19)


//=//// STUB_FLAG_20 //////////////////////////////////////////////////////=//
//
#define STUB_FLAG_20 \
    FLAG_LEFT_BIT(20)


//=//// STUB_FLAG_21 ////////////////////////////////////////////////////=//
//
#define STUB_FLAG_21 \
    FLAG_LEFT_BIT(21)


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
    FLAG_LEFT_BIT(22)


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
    FLAG_LEFT_BIT(23)

#define FLEX_FLAG_DONT_RELOCATE FLEX_FLAG_FIXED_SIZE

#define Fixed(pointer)  pointer  // commentary for non-movable pointer


//=////////////////////////////////////////////////////////////////////////=//
//
// BITS 24-31: STUB SUBCLASS FLAGS
//
//=////////////////////////////////////////////////////////////////////////=//

// These flags are those that differ based on which Stub Flavor is used.
//
// This space is used currently for Array flags to store things like whether
// the array ends in a newline.  It's a hodepodge of other bits which were
// rehomed while organizing the flavor bits.  These positions now have the
// ability to be more thought out after the basics of flavors are solved.
//
// The bits are pushed out of the range of generic Flex flags to be safe.
// But if more than 8 bits are needed for a non-Flex Stub, then it is
// possible to reuse a Flex flag... if truly necessary (!)

#define STUB_SUBCLASS_FLAG_24    FLAG_LEFT_BIT(24)
#define STUB_SUBCLASS_FLAG_25    FLAG_LEFT_BIT(25)
#define STUB_SUBCLASS_FLAG_26    FLAG_LEFT_BIT(26)
#define STUB_SUBCLASS_FLAG_27    FLAG_LEFT_BIT(27)
#define STUB_SUBCLASS_FLAG_28    FLAG_LEFT_BIT(28)
#define STUB_SUBCLASS_FLAG_29    FLAG_LEFT_BIT(29)
#define STUB_SUBCLASS_FLAG_30    FLAG_LEFT_BIT(30)
#define STUB_SUBCLASS_FLAG_31    FLAG_LEFT_BIT(31)



//=////////////////////////////////////////////////////////////////////////=//
//
// STUB STRUCTURE DEFINITION
//
//=////////////////////////////////////////////////////////////////////////=//

typedef struct {
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
    // logical length in such cases is in MISC_STRING_NUM_CODEPOINTS
    //
    Length used;

    // `rest` is the total number of units from bias to end.  Having a
    // slightly weird name draws attention to the idea that it's not really
    // the "capacity", just the "rest of the capacity after the bias".
    //
    Length rest;

    // This is the 4th pointer on 32-bit platforms which could be used for
    // something when a Flex is dynamic.  It is the "bias" when a Flex needs
    // to maintain how much the data pointer is offset from the allocation.
    //
    UintptrUnion bonus;
} StubDynamicStruct;


typedef union {
    //
    // If the Flex data does not fit into the StubContent, then it must be
    // dynamically allocated.  This is the tracking structure for that
    // dynamic data allocation.
    //
    StubDynamicStruct dynamic;

    // If not(STUB_FLAG_DYNAMIC), then 0 or 1 length arrays can be held in
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
} StubContentUnion;


#if CPLUSPLUS_11
    struct StubStruct : public Base  // Note: empty base class optimization
#else
    struct StubStruct
#endif
{
    // See the description of FLEX_FLAG_XXX for the bits in this header.
    // It is in the same position as a Cell header, and the first byte
    // can be read via BASE_BYTE() to determine which it is.  It's named
    // "leader" to be distinct from a Cell's "header" to achieve a kind of
    // poor-man's macro typechecking which doesn't incur checked build costs.
    //
    HeaderUnion leader;

    // The `link` field is generally used for pointers to something that
    // when updated, all references to this Flex would want to be able
    // to see.  This cannot be done (easily) for properties that are held
    // in a Cell directly.
    //
    // This field is in the second pointer-sized slot in the Stub, picked to
    // push the `content` so it is 64-bit aligned on 32-bit platforms.  This
    // is because a Cell may be the StubContentUnion, and a cell assumes
    // it is on a 64-bit boundary to start with...in order to position its
    // "payload" which might need to be 64-bit aligned as well.
    //
    UintptrUnion link;

    // `content` is the sizeof(Cell) data for the Flex, which is thus
    // 4 platform pointers in size.  If the Flex is small enough, the header
    // contains the size in bytes and the content lives literally in these
    // bits.  If it's too large, it will instead be a pointer and tracking
    // information for another allocation.
    //
    StubContentUnion content;

    // If STUB_FLAG_INFO_NEEDS_MARK, then the `info.node` field is marked
    // by the garbage collector.
    //
    // Otherwise it is used for 32-bits [1] of FLEX_INFO_XXX flags, and other
    // optional data.  (For instance, a Symbol Stub stores its optional SymId
    // in this space).  Make_Flex() calls presume all the info bits are
    // initialized to zero, so any flag that controls the allocation should be
    // a FLEX_FLAG_XXX instead.
    //
    // 1. Only 32-bits are used on 64-bit platforms.  There could be some
    //    interesting added caching feature or otherwise that would use
    //    it, while not making any feature specifically require a 64-bit CPU.
    //
    UintptrUnion info;

    // This is the second pointer-sized piece of Flex data that is used
    // for various purposes, similar to link.
    //
    UintptrUnion misc;

  #if DEBUG_STUB_ORIGINS
    Byte* guard;  // intentionally alloc'd and freed for use by crash()
    uintptr_t tick;  // also maintains sizeof(Stub) % sizeof(REBI64) == 0
  #endif
};


//=//// DON'T PUT ANY CODE (OR MACROS THAT MAY NEED CODE) IN THIS FILE! ///=//
//
// The %tmp-internals.h file has not been included, and hence none of the
// prototypes (even for things like Crash_Core()) are available.
//
// Even if a macro seems like it doesn't need code right at this moment, you
// might want to put some instrumentation into it, and that becomes a pain of
// manual forward declarations.
//
// So keep this file limited to structs and constants.  It's too long already.
//
//=////////////////////////////////////////////////////////////////////////=//
